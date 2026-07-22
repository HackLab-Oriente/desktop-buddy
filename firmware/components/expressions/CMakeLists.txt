idf_component_register(
  SRCS "round_face.cpp" "face_model.cpp" "oled_face.cpp" "led_mood.cpp"
  INCLUDE_DIRS "include"
  REQUIRES bus esp_driver_ledc esp_driver_i2c esp_driver_spi esp_driver_gpio esp_lcd
)
