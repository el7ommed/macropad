cmake_minimum_required(VERSION 3.16.0)
set(EXCLUDE_COMPONENTS led_strip espressif__led_strip)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(macropad)
