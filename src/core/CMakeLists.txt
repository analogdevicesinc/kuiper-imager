find_package(Qt6 6.8 REQUIRED COMPONENTS Core)

configure_file(src/version.cpp.in
               ${CMAKE_CURRENT_BINARY_DIR}/version.cpp @ONLY)

add_library(kuiper STATIC
    src/error.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/version.cpp)

target_include_directories(kuiper PUBLIC include)

target_link_libraries(kuiper PUBLIC Qt6::Core)

add_library(kuiper::kuiper ALIAS kuiper)
