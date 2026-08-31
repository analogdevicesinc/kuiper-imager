find_package(Qt6 6.4 REQUIRED COMPONENTS Core)

add_executable(kli main.cpp)

target_link_libraries(kli PRIVATE kuiper::kuiper Qt6::Core)

include(GNUInstallDirs)
install(TARGETS kli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
