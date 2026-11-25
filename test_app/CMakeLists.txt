# Unity test app: builds the firmware component plus main/test, and runs every TEST_CASE at boot.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../main"
    "${CMAKE_CURRENT_LIST_DIR}/../main/test")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(OpenComfortTests)
