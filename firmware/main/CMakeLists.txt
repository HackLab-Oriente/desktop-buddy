idf_component_register(
  SRCS "main.cpp"
  REQUIRES bus senses expressions pack brain webui berry_host nvs_flash
)

# Flash packs/zero into the `storage` partition as a prepared LittleFS image.
#
# Without this the partition is empty on a fresh board, so berry_host finds no
# /flash/reflexes/main.be and the buddy boots inert -- which is #22. It also
# makes the pack real: expressions and moods come off flash rather than out of
# the C++ fallback tables, so editing packs/zero and reflashing changes the
# buddy without touching firmware.
littlefs_create_partition_image(storage ../../packs/zero FLASH_IN_PROJECT)
