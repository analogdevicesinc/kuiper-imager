find_package(Qt6 6.4 REQUIRED COMPONENTS Core)
find_package(LibArchive REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

configure_file(src/version.cpp.in
               ${CMAKE_CURRENT_BINARY_DIR}/version.cpp @ONLY)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(KUIPER_PLATFORM_SOURCES src/platform/linux/LinuxDriveBackend.cpp)
elseif(WIN32)
    set(KUIPER_PLATFORM_SOURCES src/platform/windows/WindowsDriveBackend.cpp)
elseif(APPLE)
    set(KUIPER_PLATFORM_SOURCES src/platform/macos/MacOSDriveBackend.cpp)
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

add_library(kuiper STATIC
    src/error.cpp
    src/DriveService.cpp
    src/Layout.cpp
    src/ConfigurationService.cpp
    src/ImageService.cpp
    src/http/CurlHttpClient.cpp
    src/image/GitHubActionsProvider.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
    ${KUIPER_PLATFORM_SOURCES})

target_include_directories(kuiper PUBLIC include)

target_compile_definitions(kuiper PRIVATE _FILE_OFFSET_BITS=64)

target_link_libraries(kuiper PUBLIC Qt6::Core
                              PRIVATE LibArchive::LibArchive OpenSSL::Crypto
                                      CURL::libcurl)

add_library(kuiper::kuiper ALIAS kuiper)
