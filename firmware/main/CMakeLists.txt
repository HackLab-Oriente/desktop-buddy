idf_component_register(
  SRCS "main.cpp"
  REQUIRES bus senses expressions brain webui berry_host nvs_flash
  # main.cpp includes these directly; they resolved only through IDF's common
  # requires, which is a dependency that breaks on a reshuffle and reads as a
  # toolchain bug when it does.
  PRIV_REQUIRES esp_littlefs espressif__cjson esp_log
)
