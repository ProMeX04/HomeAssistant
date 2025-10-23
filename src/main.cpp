#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <algorithm>

// ---- Network configuration ----
constexpr char kWifiSsid[] = "Nguyen Van Hai";
constexpr char kWifiPassword[] = "0964822864";
constexpr char kMqttHost[] = "192.168.1.5";
constexpr uint16_t kMqttPort = 1883;
constexpr char kDeviceId[] = "automation";
constexpr char kDeviceName[] = "Automation Controller";
constexpr char kDeviceLocation[] = "Living Room";
constexpr char kBaseTopic[] = "homeassistant";
constexpr char kHumiditySensorId[] = "humidity";
constexpr char kLightSensorId[] = "light";

constexpr gpio_num_t kLedPin = GPIO_NUM_2;
constexpr gpio_num_t kHumiditySensorPin = GPIO_NUM_34;
constexpr gpio_num_t kLightSensorPin = GPIO_NUM_35;

constexpr uint32_t kSensorIntervalMs = 10000;
bool dataCollectionEnabled = true;
bool ledIsOn = false;
unsigned long lastSensorPostMs = 0;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
String clientId;

void publishDeviceStateSnapshot(const char *status = nullptr);
void publishSensorReading(const char *sensorId, const char *sensorName, const char *metric, float value, const char *unit);

String telemetryTopic()
{
    return String(kBaseTopic) + "/" + kDeviceId + "/telemetry";
}

String commandTopic()
{
    return String(kBaseTopic) + "/" + kDeviceId + "/command";
}

String stateTopic()
{
    return String(kBaseTopic) + "/" + kDeviceId + "/state";
}

void connectWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(kWifiSsid, kWifiPassword);
    Serial.printf("Connecting to %s", kWifiSsid);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print('.');
    }
    Serial.printf("\nConnected. IP: %s\n", WiFi.localIP().toString().c_str());

    String mac = WiFi.macAddress();
    mac.replace(":", "");
    clientId = String(kDeviceId) + "-" + mac;
}

void ensureMqttConnected()
{
    if (mqttClient.connected())
    {
        return;
    }
    while (!mqttClient.connected())
    {
        Serial.printf("Connecting to MQTT broker %s:%u as %s\n", kMqttHost, kMqttPort, clientId.c_str());
        if (mqttClient.connect(clientId.c_str()))
        {
            mqttClient.subscribe(commandTopic().c_str(), 1);
            Serial.println("MQTT connected and subscribed to command topic");
            publishDeviceStateSnapshot("online");
        }
        else
        {
            Serial.printf("MQTT connect failed rc=%d, retrying...\n", mqttClient.state());
            delay(2000);
        }
    }
}

float analogToPercent(int rawValue)
{
    constexpr float kMax = 4095.0f;
    float clamped = std::max(0, std::min(rawValue, 4095));
    return (clamped / kMax) * 100.0f;
}

float readHumidityPercent()
{
    int raw = analogRead(kHumiditySensorPin);
    return analogToPercent(raw);
}

float readLightPercent()
{
    int raw = analogRead(kLightSensorPin);
    return analogToPercent(raw);
}

void publishDeviceStateSnapshot(const char *status)
{
    if (!mqttClient.connected())
    {
        return;
    }

    const char *statusValue = status ? status : "online";
    const char *ledState = ledIsOn ? "on" : "off";
    const char *collectionState = dataCollectionEnabled ? "true" : "false";

    // Simplified state payload - essential device info only
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"type\":\"state\",\"deviceId\":\"%s\",\"deviceName\":\"%s\",\"location\":\"%s\","
             "\"topicTelemetry\":\"%s\",\"topicCommand\":\"%s\",\"topicState\":\"%s\",\"status\":\"%s\","
             "\"led\":\"%s\",\"collectionEnabled\":%s}",
             kDeviceId, kDeviceName, kDeviceLocation, telemetryTopic().c_str(), commandTopic().c_str(), stateTopic().c_str(),
             statusValue, ledState, collectionState);

    if (mqttClient.publish(stateTopic().c_str(), payload, true))
    {
        Serial.printf("Published device state: %s\n", payload);
    }
    else
    {
        Serial.println("Failed to publish device state");
    }
}

void publishSensorReading(const char *sensorId, const char *sensorName, const char *metric, float value,
                          const char *unit)
{
    if (!mqttClient.connected())
    {
        return;
    }

    // Simplified payload for sensor readings - only essential data
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"type\":\"sensor\",\"deviceId\":\"%s\",\"sensorId\":\"%s\",\"metric\":\"%s\",\"value\":%.1f,\"unit\":\"%s\"}",
             kDeviceId, sensorId, metric, value, unit);

    if (mqttClient.publish(telemetryTopic().c_str(), payload, false))
    {
        Serial.printf("Published %s reading: %.1f %s\n", metric, value, unit);
    }
    else
    {
        Serial.printf("Failed to publish %s reading\n", metric);
    }
}

void reportSensorData()
{
    if (!dataCollectionEnabled || WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    ensureMqttConnected();
    if (!mqttClient.connected())
    {
        return;
    }

    float humidity = readHumidityPercent();
    float light = readLightPercent();
    publishSensorReading(kHumiditySensorId, "Độ ẩm", "humidity", humidity, "%");
    publishSensorReading(kLightSensorId, "Ánh sáng", "light", light, "%");
}

String extractJsonString(const String &payload, const char *key)
{
    String searchKey = String("\"") + key + "\":";
    int start = payload.indexOf(searchKey);
    if (start < 0)
    {
        return "";
    }
    start += searchKey.length();
    while (start < payload.length() && (payload[start] == ' ' || payload[start] == '\"'))
    {
        if (payload[start] == '\"')
        {
            start++;
            int end = payload.indexOf('\"', start);
            if (end < 0)
            {
                return "";
            }
            return payload.substring(start, end);
        }
        start++;
    }
    int end = start;
    while (end < payload.length() && payload[end] != ',' && payload[end] != '}' && payload[end] != ' ')
    {
        end++;
    }
    return payload.substring(start, end);
}

void applyCommand(const String &command, const String &payload)
{
    if (command == "set_led")
    {
        String state = extractJsonString(payload, "state");
        bool turnOn = state.equalsIgnoreCase("on") || state == "1";
        ledIsOn = turnOn;
        digitalWrite(kLedPin, ledIsOn ? HIGH : LOW);
        Serial.printf("LED state set to %s\n", ledIsOn ? "ON" : "OFF");
    }
    else if (command == "set_collection")
    {
        String state = extractJsonString(payload, "state");
        dataCollectionEnabled = !(state.equalsIgnoreCase("off") || state == "0");
        Serial.printf("Data collection %s\n", dataCollectionEnabled ? "enabled" : "paused");
    }
    else
    {
        Serial.printf("Unknown command '%s'\n", command.c_str());
        return;
    }

    publishDeviceStateSnapshot();
}

void handleMqttMessage(char *topic, byte *payload, unsigned int length)
{
    String topicStr(topic);
    String expected = commandTopic();
    if (!topicStr.equals(expected))
    {
        return;
    }
    String body;
    body.reserve(length + 1);
    for (unsigned int i = 0; i < length; ++i)
    {
        body += static_cast<char>(payload[i]);
    }
    String command = extractJsonString(body, "command");
    if (command.length() == 0)
    {
        Serial.println("MQTT message missing command");
        return;
    }
    Serial.printf("MQTT command received: %s\n", body.c_str());
    applyCommand(command, body);
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    pinMode(kLedPin, OUTPUT);
    digitalWrite(kLedPin, LOW);
    pinMode(kHumiditySensorPin, INPUT);
    pinMode(kLightSensorPin, INPUT);

    connectWiFi();

    mqttClient.setServer(kMqttHost, kMqttPort);
    mqttClient.setBufferSize(512);
    mqttClient.setCallback(handleMqttMessage);
    randomSeed(micros());
}

void loop()
{
    unsigned long now = millis();

    ensureMqttConnected();
    mqttClient.loop();

    if (now - lastSensorPostMs >= kSensorIntervalMs)
    {
        lastSensorPostMs = now;
        reportSensorData();
    }

    delay(10);
}
