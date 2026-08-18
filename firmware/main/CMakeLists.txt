idf_component_register(
  SRCS "main.cpp"
  REQUIRES bus senses expressions brain webui berry_host markov nvs_flash esp_driver_gpio esp_timer
)
