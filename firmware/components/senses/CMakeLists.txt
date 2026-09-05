idf_component_register(
  SRCS "touch_sense.cpp" "rc522.cpp"
  INCLUDE_DIRS "include"
  # The public header needs none of the drivers; keeping them private stops
  # their headers leaking into every consumer of senses.h.
  REQUIRES bus
  PRIV_REQUIRES esp_driver_touch_sens esp_driver_spi esp_driver_gpio esp_timer
)
