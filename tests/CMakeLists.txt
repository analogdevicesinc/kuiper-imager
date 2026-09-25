include(FetchContent)

# doctest, pinned and fetched only when tests are enabled — hermetic, no system
# package, and it never touches the .deb build.
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
)
FetchContent_MakeAvailable(doctest)

# Portable unit suite: in-memory fakes, no device, no root. Runs everywhere.
add_executable(kuiper_tests
    doctest_main.cpp
    guard_test.cpp
    drive_service_test.cpp
    capabilities_test.cpp
    layout_test.cpp
    configuration_service_test.cpp
    image_service_test.cpp)
target_link_libraries(kuiper_tests PRIVATE kuiper::kuiper doctest::doctest)
target_include_directories(kuiper_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME kuiper_tests COMMAND kuiper_tests)

# Linux loopback integration test: exercises the real backend over a loop device.
# Needs root (losetup), so it is labelled `integration` — CI runs it in a separate
# root step and the default run excludes it with `ctest -LE integration`.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(kuiper_linux_it integration/linux_loopback_it.cpp)
    target_link_libraries(kuiper_linux_it PRIVATE kuiper::kuiper doctest::doctest)
    target_include_directories(kuiper_linux_it PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME kuiper_linux_it COMMAND kuiper_linux_it)
    set_tests_properties(kuiper_linux_it PROPERTIES LABELS integration)
endif()
