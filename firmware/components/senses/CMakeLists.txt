idf_component_register(
  SRCS "touch_sense.cpp" "rc522.cpp"
  INCLUDE_DIRS "include"
  REQUIRES bus esp_driver_touch_sens esp_driver_spi esp_driver_gpio
)
