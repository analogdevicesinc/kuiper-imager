find_package(Qt6 6.8 REQUIRED COMPONENTS Core)

add_executable(kli main.cpp)

target_link_libraries(kli PRIVATE kuiper::kuiper Qt6::Core)
