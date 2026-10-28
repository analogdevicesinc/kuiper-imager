find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

# Compile theme/theme.qrc (the generated stylesheet bundle) into the binary.
set(CMAKE_AUTORCC ON)

qt_add_executable(kuiper-imager
    main.cpp
    MainWindow.cpp
    MainWindow.hpp
    theme/ThemeManager.cpp
    theme/ThemeManager.hpp
    widgets/HmcButton.cpp
    widgets/HmcButton.hpp
    theme/theme.qrc)

target_link_libraries(kuiper-imager PRIVATE kuiper::kuiper Qt6::Core Qt6::Widgets)
