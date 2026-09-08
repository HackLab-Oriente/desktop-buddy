cmake_minimum_required(VERSION 3.16)

# Buddy Zero — framework seed + PoC testbed.
# One target: esp32s3. The colour face keeps a PSRAM sprite cache and the
# local model needs the RAM, neither of which the classic ESP32 has.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(buddy)
