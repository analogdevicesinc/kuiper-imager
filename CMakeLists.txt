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
option(KUIPER_IMAGER_PACKAGE "Enable CPack packaging config (.deb)" OFF)

add_subdirectory(src/core)
add_subdirectory(src/cli)

if(KUIPER_IMAGER_GUI)
    add_subdirectory(src/gui)
endif()

message(STATUS "kuiper-imager ${PROJECT_VERSION} | GUI: ${KUIPER_IMAGER_GUI}")

if(KUIPER_IMAGER_PACKAGE)
    set(CPACK_PACKAGE_VENDOR "Analog Devices")
    set(CPACK_PACKAGE_CONTACT "Alisa-Dariana Roman <alisa.roman@analog.com>")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_STRIP_FILES ON)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(CPACK_PACKAGE_NAME "kuiper-imager-cli")
        set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION} (CLI)")
        set(CPACK_PACKAGE_DESCRIPTION
            "kli fetches, flashes, and configures Kuiper Linux images for Analog\nDevices hardware. This is the command-line front-end; it links the distro's\nQt 6 Core at runtime.")

        set(CPACK_GENERATOR "DEB")
        set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
        set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
        set(CPACK_DEBIAN_PACKAGE_HOMEPAGE
            "https://github.com/analogdevicesinc/kuiper-imager")
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    elseif(WIN32)
        set(CPACK_PACKAGE_NAME "KuiperImager")
        set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
        set(CPACK_PACKAGE_INSTALL_DIRECTORY "KuiperImager")
        set(CPACK_PACKAGE_FILE_NAME "kuiper-imager-setup-${PROJECT_VERSION}-win64")
        set(CPACK_PACKAGE_EXECUTABLES "kuiper-imager;ADI Kuiper Imager")

        set(CPACK_GENERATOR "NSIS")
        set(CPACK_NSIS_DISPLAY_NAME "ADI Kuiper Imager")
        set(CPACK_NSIS_PACKAGE_NAME "ADI Kuiper Imager")
        set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")
        set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
        set(CPACK_NSIS_MODIFY_PATH ON)
        # Installed binaries land flat in the install root (no per-target subdir).
        set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    endif()

    include(CPack)
endif()
