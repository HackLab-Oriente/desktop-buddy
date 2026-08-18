idf_component_register(SRCS "voice.c" INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2s esp_driver_gpio esp_http_client
                                esp-tls esp_timer)
