idf_component_register(
  SRCS "face_model.cpp" "mood_model.cpp" "round_face.cpp" "led_ring.cpp"
  INCLUDE_DIRS "include" "."
  REQUIRES bus LovyanGFX
  # driver/spi_common.h reaches us only transitively, through LovyanGFX's own
  # COMPONENT_REQUIRES. Upstream is mid-migration off the `driver` umbrella,
  # so name it here: senses/CMakeLists.txt already does.
  PRIV_REQUIRES esp_driver_spi
)
# The eye renderer is a per-pixel inner loop; the project builds -Os globally,
# which is right for the firmware as a whole and wrong here. Measured 33%
# faster on the spike.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
