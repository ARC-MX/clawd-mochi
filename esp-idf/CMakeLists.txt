# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(clawd_mochi)

# Bundle data/ into the "storage" LittleFS partition; `idf.py flash` writes it
# alongside the bootloader / partition table / app. data/ holds the web UI plus
# the swappable theme (data/theme/*.caf + manifest.txt).
littlefs_create_partition_image(storage ${CMAKE_SOURCE_DIR}/data FLASH_IN_PROJECT)
