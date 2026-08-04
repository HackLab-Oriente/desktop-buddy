idf_component_register(
  SRCS "face_model.cpp" "round_face.cpp" "led_ring.cpp"
  INCLUDE_DIRS "include"
  REQUIRES bus esp_driver_spi esp_driver_gpio esp_lcd
)
