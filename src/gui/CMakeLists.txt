find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Concurrent Network)

qt_standard_project_setup()

# Compile theme/theme.qrc (the generated stylesheet bundle) into the binary.
set(CMAKE_AUTORCC ON)

qt_add_executable(kuiper-imager
    main.cpp
    ConsoleWindow.cpp
    ConsoleWindow.hpp
    task/BackgroundTask.cpp
    task/BackgroundTask.hpp
    task/HelperProcess.cpp
    task/HelperProcess.hpp
    task/IHelperSession.hpp
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

# Per-OS elevation session behind IHelperSession: pkexec+stdio on Unix,
# UAC("runas")+named-pipe on Windows. Only the host platform's session is compiled.
if(WIN32)
    target_sources(kuiper-imager PRIVATE
        task/UacHelperSession.cpp
        task/UacHelperSession.hpp)
    # Shell32: ShellExecuteEx("runas"). Advapi32: the helper pipe's security
    # descriptor (SDDL) and the current user's SID.
    target_link_libraries(kuiper-imager PRIVATE Shell32 Advapi32)
else()
    target_sources(kuiper-imager PRIVATE
        task/PkexecHelperSession.cpp
        task/PkexecHelperSession.hpp)
endif()

target_include_directories(kuiper-imager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Baked-in fallback path to the privileged write helper (pkexec target). The
# resolver prefers an env override and the build-tree sibling; this is the
# installed location. Set at the top level (Unix only; on Windows the helper is
# always found beside the GUI exe).
if(DEFINED KUIPER_HELPER_INSTALL_PATH)
    target_compile_definitions(kuiper-imager
        PRIVATE KUIPER_HELPER_PATH="${KUIPER_HELPER_INSTALL_PATH}")
endif()

target_link_libraries(kuiper-imager PRIVATE kuiper::kuiper Qt6::Core Qt6::Widgets Qt6::Concurrent Qt6::Network)

include(GNUInstallDirs)
install(TARGETS kuiper-imager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
