cmake_minimum_required(VERSION 3.16)

# Buddy Zero — framework seed + PoC testbed.
# Two targets, both supported:
#   esp32s3  the real buddy — colour face with a PSRAM sprite cache, local model
#   esp32    classic DevKit V1 — same colour face rendered in bands (no PSRAM),
#            4 MB partition table, no local model. See firmware/README.md.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(buddy)
