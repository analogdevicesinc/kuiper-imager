cmake_minimum_required(VERSION 3.22)

project(kuiper-imager
    VERSION 2.0.0
    DESCRIPTION "ADI Kuiper Imager"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(KUIPER_IMAGER_GUI "Build the Qt Widgets GUI front-end" ON)

add_subdirectory(src/core)
add_subdirectory(src/cli)

message(STATUS "kuiper-imager ${PROJECT_VERSION} | GUI: ${KUIPER_IMAGER_GUI}")
