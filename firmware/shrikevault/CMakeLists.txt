# ShrikeVault — the wallet firmware (ESP-IDF).
#
# Build:   idf.py set-target esp32s3 && idf.py build
# Flash:   idf.py -p COM19 flash monitor
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(shrikevault)
