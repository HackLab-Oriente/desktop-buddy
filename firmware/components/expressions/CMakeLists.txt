idf_component_register(
  SRCS "oled_face.cpp" "led_mood.cpp"
  INCLUDE_DIRS "include"
  REQUIRES bus esp_driver_ledc esp_driver_i2c
)
