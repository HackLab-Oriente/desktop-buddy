idf_component_register(SRCS "main.cpp" "run.c" "runq8.c" INCLUDE_DIRS "."
                       REQUIRES esp_partition spi_flash esp_timer espressif__esp-dsp)
# The transformer is a float inner loop; -Os would be the wrong trade here.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -ffast-math -funroll-loops -Wno-unknown-pragmas -Wno-error=unknown-pragmas -Wno-error=maybe-uninitialized)
