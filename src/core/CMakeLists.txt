find_package(Qt6 6.4 REQUIRED COMPONENTS Core)
find_package(LibArchive REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

configure_file(src/version.cpp.in
               ${CMAKE_CURRENT_BINARY_DIR}/version.cpp @ONLY)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(KUIPER_PLATFORM_SOURCES
        src/platform/linux/LinuxDriveBackend.cpp
        src/customize/LinuxImageCustomizer.cpp)
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
    src/customize/FeatureCatalog.cpp
    src/customize/CustomizePlanner.cpp
    src/customize/QProcessCommandRunner.cpp
    src/customize/ImageCustomizerFactory.cpp
    assets/kuiper_assets.qrc
    src/ipc/HelperProtocol.cpp
    src/ImageService.cpp
    src/http/CurlHttpClient.cpp
    src/image/GitHubActionsProvider.cpp
    src/image/StableReleaseProvider.cpp
    src/platform/common/AlignedRawDevice.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
    ${KUIPER_PLATFORM_SOURCES})

# Compile the embedded asset resources (assets/kuiper_assets.qrc) into the lib.
set_target_properties(kuiper PROPERTIES AUTORCC ON)

target_include_directories(kuiper PUBLIC include PRIVATE src)

target_compile_definitions(kuiper PRIVATE _FILE_OFFSET_BITS=64)

target_link_libraries(kuiper PUBLIC Qt6::Core
                              PRIVATE LibArchive::LibArchive OpenSSL::Crypto
                                      CURL::libcurl)

# Native platform libraries for the per-OS drive backend.
if(WIN32)
    # setupapi: disk enumeration; cfgmgr32: CM_Request_Device_Eject (safe removal).
    target_link_libraries(kuiper PRIVATE setupapi cfgmgr32)
endif()

add_library(kuiper::kuiper ALIAS kuiper)
