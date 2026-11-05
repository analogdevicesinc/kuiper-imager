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

# Desktop integration for the GUI (Linux only): a .desktop entry and a hicolor
# icon so the app appears in menus. Authored once under packaging/linux/ and
# installed here so `cmake --install` is complete — the same files feed both the
# .deb and a future AppImage. The icon/desktop basename is the reverse-DNS app id.
if(KUIPER_IMAGER_GUI AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES packaging/linux/com.analogdevices.kuiperimager.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/linux/com.analogdevices.kuiperimager.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
endif()

message(STATUS "kuiper-imager ${PROJECT_VERSION} | GUI: ${KUIPER_IMAGER_GUI} | helper: ${KUIPER_IMAGER_HELPER}")

# Debian packaging is driven by the native debhelper tree in debian/ (dh with the
# cmake build system), not CPack: it builds once with the GUI on and splits the
# install tree into the kuiper-imager-cli and kuiper-imager binary packages. See
# debian/rules and debian/*.install.
