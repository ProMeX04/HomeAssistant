#include "driver/i2s.h"
#include "esp_timer.h"
#include "lwip/def.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <algorithm>
#include <array>
#include <cstring>

// ---- Network configuration ----
constexpr char kWifiSsid[] = "Nguyen Van Hai";
constexpr char kWifiPassword[] = "0964822864";
constexpr char kServerHost[] = "192.168.1.2"; // Server IP/domain (DNS hoặc IP)
constexpr uint16_t kServerPort = 5000;        // UDP port server lắng nghe
constexpr uint16_t kLocalUdpPort = 6000;      // Cổng UDP thiết bị sử dụng

// ---- Trigger configuration ----
constexpr gpio_num_t kTriggerPin = GPIO_NUM_14; // Công tắc bật ghi (đổi theo phần cứng)
constexpr bool kTriggerActiveLow = true;        // Công tắc nối GND khi bật → dùng PULLUP

// ---- Audio configuration ----
constexpr i2s_port_t kI2SPort = I2S_NUM_0;
constexpr uint32_t kSampleRate = 16000; // Hz
constexpr size_t kFrameSamples = 256;   // 16 ms @16 kHz để giảm độ trễ/bộ nhớ
constexpr i2s_bits_per_sample_t kBitsPerSample = I2S_BITS_PER_SAMPLE_32BIT;
constexpr i2s_channel_fmt_t kChannelFormat = I2S_CHANNEL_FMT_ONLY_LEFT;
constexpr i2s_comm_format_t kCommFormat = static_cast<i2s_comm_format_t>(
    I2S_COMM_FORMAT_STAND_I2S | I2S_COMM_FORMAT_I2S_MSB);

int32_t i2sRawBuffer[kFrameSamples];
int16_t pcmBuffer[kFrameSamples];
WiFiUDP udpClient;
IPAddress serverIp;
bool serverResolved = false;
uint32_t packetSequence = 0;
bool isStreamingEnabled = false;

bool readTriggerState() {
    int level = digitalRead(kTriggerPin);
    if (kTriggerActiveLow) {
        return level == LOW;
    }
    return level == HIGH;
}

void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(kWifiSsid, kWifiPassword);
    Serial.printf("Connecting to %s", kWifiSsid);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }
    Serial.printf("\nConnected. IP: %s\n", WiFi.localIP().toString().c_str());
}

void resolveServerIp() {
    if (WiFi.hostByName(kServerHost, serverIp)) {
        serverResolved = true;
        Serial.printf("Resolved server %s -> %s\n", kServerHost, serverIp.toString().c_str());
    } else {
        serverResolved = false;
        Serial.printf("Failed to resolve server %s\n", kServerHost);
    }
}

void configureI2S() {
    i2s_config_t i2s_config = {};
    i2s_config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
    i2s_config.sample_rate = kSampleRate;
    i2s_config.bits_per_sample = kBitsPerSample;
    i2s_config.channel_format = kChannelFormat;
    i2s_config.communication_format = kCommFormat;
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_config.dma_buf_count = 4;
    i2s_config.dma_buf_len = kFrameSamples;
    i2s_config.use_apll = false;
    i2s_config.tx_desc_auto_clear = false;
    i2s_config.fixed_mclk = 0;

    i2s_pin_config_t pin_config = {};
    pin_config.bck_io_num = 26;   // INMP441 SCK
    pin_config.ws_io_num = 25;    // INMP441 L/R
    pin_config.data_out_num = -1; // Not used (RX only)
    pin_config.data_in_num = 33;  // INMP441 DOUT

    ESP_ERROR_CHECK(i2s_driver_install(kI2SPort, &i2s_config, 0, nullptr));
    ESP_ERROR_CHECK(i2s_set_pin(kI2SPort, &pin_config));
    ESP_ERROR_CHECK(i2s_set_clk(kI2SPort, kSampleRate, kBitsPerSample, I2S_CHANNEL_MONO));
}

void streamAudioFrame() {
    if (!serverResolved) {
        return;
    }

    size_t bytesRead = 0;
    esp_err_t result = i2s_read(kI2SPort, reinterpret_cast<void *>(i2sRawBuffer),
                                sizeof(i2sRawBuffer), &bytesRead, 10 / portTICK_PERIOD_MS);
    if (result != ESP_OK || bytesRead == 0) {
        return;
    }

    size_t sampleCount = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < sampleCount; ++i) {
        int32_t sample = i2sRawBuffer[i] >> 14; // Convert 32-bit to ~18-bit
        sample = std::max(-32768, std::min(32767, sample));
        pcmBuffer[i] = static_cast<int16_t>(sample);
    }

    const uint32_t timestampMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    const uint32_t sequenceNet = htonl(packetSequence++);
    const uint32_t timestampNet = htonl(timestampMs);
    const uint16_t sampleCountNet = htons(static_cast<uint16_t>(sampleCount));
    const uint16_t sampleRateNet = htons(static_cast<uint16_t>(kSampleRate));

    std::array<uint8_t, 12> header{};
    memcpy(header.data(), &sequenceNet, sizeof(sequenceNet));
    memcpy(header.data() + 4, &timestampNet, sizeof(timestampNet));
    memcpy(header.data() + 8, &sampleCountNet, sizeof(sampleCountNet));
    memcpy(header.data() + 10, &sampleRateNet, sizeof(sampleRateNet));

    const uint8_t *payload = reinterpret_cast<uint8_t *>(pcmBuffer);
    const size_t payloadBytes = sampleCount * sizeof(int16_t);

    if (udpClient.beginPacket(serverIp, kServerPort)) {
        udpClient.write(header.data(), header.size());
        udpClient.write(payload, payloadBytes);
        udpClient.endPacket();
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    pinMode(kTriggerPin, kTriggerActiveLow ? INPUT_PULLUP : INPUT);
    connectWiFi();
    configureI2S();
    udpClient.begin(kLocalUdpPort);
    resolveServerIp();
}

void loop() {
    if (!serverResolved && WiFi.status() == WL_CONNECTED) {
        resolveServerIp();
    }
    bool shouldStream = readTriggerState();
    if (shouldStream != isStreamingEnabled) {
        isStreamingEnabled = shouldStream;
        if (isStreamingEnabled) {
            packetSequence = 0;
            i2s_zero_dma_buffer(kI2SPort);
            Serial.println("Trigger active → bắt đầu stream âm thanh");
        } else {
            Serial.println("Trigger inactive → tạm dừng stream");
        }
    }

    if (isStreamingEnabled) {
        streamAudioFrame();
    } else {
        delay(5);
    }
}
