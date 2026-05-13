# ShrikeVault — P0 board bring-up app (ESP-IDF)
#
# Build:   idf.py set-target esp32s3 && idf.py build
# Flash:   idf.py -p COM19 flash monitor   (hold BOOT while plugging USB-C the first time)
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(shrikevault_bringup)
