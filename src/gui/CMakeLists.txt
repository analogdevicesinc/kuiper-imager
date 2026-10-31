find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Concurrent)

qt_standard_project_setup()

# Compile theme/theme.qrc (the generated stylesheet bundle) into the binary.
set(CMAKE_AUTORCC ON)

qt_add_executable(kuiper-imager
    main.cpp
    ConsoleWindow.cpp
    ConsoleWindow.hpp
    task/BackgroundTask.cpp
    task/BackgroundTask.hpp
    dialogs/ConfirmWriteDialog.cpp
    dialogs/ConfirmWriteDialog.hpp
    dialogs/ReleasePickerDialog.cpp
    dialogs/ReleasePickerDialog.hpp
    dialogs/ConfigureDialog.cpp
    dialogs/ConfigureDialog.hpp
    util/Format.hpp
    util/Repolish.hpp
    theme/ThemeManager.cpp
    theme/ThemeManager.hpp
    widgets/HmcButton.cpp
    widgets/HmcButton.hpp
    widgets/HmcCard.cpp
    widgets/HmcCard.hpp
    widgets/HmcContainerHeader.cpp
    widgets/HmcContainerHeader.hpp
    widgets/HmcTag.cpp
    widgets/HmcTag.hpp
    widgets/HmcProgressBar.cpp
    widgets/HmcProgressBar.hpp
    widgets/HmcSegmentedControl.cpp
    widgets/HmcSegmentedControl.hpp
    widgets/HmcCheckBox.cpp
    widgets/HmcCheckBox.hpp
    widgets/HmcBanner.cpp
    widgets/HmcBanner.hpp
    widgets/HmcSpinner.cpp
    widgets/HmcSpinner.hpp
    theme/theme.qrc)

target_include_directories(kuiper-imager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(kuiper-imager PRIVATE kuiper::kuiper Qt6::Core Qt6::Widgets Qt6::Concurrent)
