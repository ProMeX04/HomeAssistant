idf_component_register(SRCS "main_ws.c" "wifi_helper.c"
                    INCLUDE_DIRS "."
                    REQUIRES audio_pipeline audio_sal audio_hal esp_peripherals audio_stream audio_board esp-sr audio_recorder esp_http_client esp_websocket_client nvs_flash esp_wifi input_key_service)
