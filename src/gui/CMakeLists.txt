find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

qt_add_executable(kuiper-imager
    main.cpp
    MainWindow.cpp
    MainWindow.hpp)

target_link_libraries(kuiper-imager PRIVATE kuiper::kuiper Qt6::Core Qt6::Widgets)
