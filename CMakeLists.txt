cmake_minimum_required(VERSION 3.5)

option(OPENCOMFORT_MATTER_PROFILE "Include the isolated ESP-Matter component" OFF)
if(OPENCOMFORT_MATTER_PROFILE)
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/matter_components/opencomfort_matter")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(OpenComfort)