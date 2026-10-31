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

include(CMakeDependentOption)

option(KUIPER_IMAGER_GUI "Build the Qt Widgets GUI front-end" ON)
# The helper exists only to elevate the GUI's writes, so it follows the GUI: a
# CLI-only build (cli-only, deb-linux) neither builds nor packages it.
cmake_dependent_option(KUIPER_IMAGER_HELPER
    "Build the privileged write helper used by the GUI for elevation" ON
    "KUIPER_IMAGER_GUI" OFF)
option(KUIPER_IMAGER_PACKAGE "Enable CPack packaging config (.deb)" OFF)

include(GNUInstallDirs)
# Unix: installed path of the privileged helper — shared by the helper install
# rule, the GUI's baked-in fallback, and the polkit action. Set before the
# subdirectories so src/gui can bake it in. Windows has no such path: the helper
# ships beside the GUI exe and is found there.
if(NOT WIN32)
    set(KUIPER_HELPER_INSTALL_PATH
        "${CMAKE_INSTALL_FULL_LIBEXECDIR}/kuiper-imager-helper"
        CACHE INTERNAL "Installed path of kuiper-imager-helper")
endif()

add_subdirectory(src/core)
add_subdirectory(src/cli)

if(KUIPER_IMAGER_HELPER)
    add_subdirectory(src/helper)
endif()

if(KUIPER_IMAGER_GUI)
    add_subdirectory(src/gui)
endif()

# Polkit action: lets pkexec show a branded prompt and cache auth (auth_admin_keep)
# for exactly the helper binary path. Linux only.
if(KUIPER_IMAGER_HELPER AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    configure_file(
        packaging/com.analogdevices.kuiperimager.policy.in
        ${CMAKE_CURRENT_BINARY_DIR}/com.analogdevices.kuiperimager.policy @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/com.analogdevices.kuiperimager.policy
            DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
    # polkit reads actions only from /usr/share/polkit-1/actions. Under any other
    # prefix pkexec still works, but with its generic prompt and no auth caching.
    if(NOT CMAKE_INSTALL_PREFIX STREQUAL "/usr")
        message(STATUS "Note: polkit only reads /usr/share/polkit-1/actions; with "
                       "prefix ${CMAKE_INSTALL_PREFIX} pkexec falls back to its "
                       "generic prompt. Use -DCMAKE_INSTALL_PREFIX=/usr for packages.")
    endif()
endif()

message(STATUS "kuiper-imager ${PROJECT_VERSION} | GUI: ${KUIPER_IMAGER_GUI} | helper: ${KUIPER_IMAGER_HELPER}")

if(KUIPER_IMAGER_PACKAGE)
    set(CPACK_PACKAGE_NAME "kuiper-imager-cli")
    set(CPACK_PACKAGE_VENDOR "Analog Devices")
    set(CPACK_PACKAGE_CONTACT "Alisa-Dariana Roman <alisa.roman@analog.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION} (CLI)")
    set(CPACK_PACKAGE_DESCRIPTION
        "kli fetches, flashes, and configures Kuiper Linux images for Analog\nDevices hardware. This is the command-line front-end; it links the distro's\nQt 6 Core at runtime.")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_STRIP_FILES ON)

    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE
        "https://github.com/analogdevicesinc/kuiper-imager")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

    include(CPack)
endif()
