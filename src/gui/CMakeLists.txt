find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

qt_add_executable(kuiper-imager
    main.cpp
    MainWindow.cpp
    MainWindow.hpp)

target_link_libraries(kuiper-imager PRIVATE kuiper::kuiper Qt6::Core Qt6::Widgets)

include(GNUInstallDirs)
install(TARGETS kuiper-imager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(WIN32)
    # Bundle the Qt runtime (DLLs + plugins) next to the installed exe so the
    # NSIS installer produces a self-contained app (see top-level CMakeLists.txt).
    find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt
        HINTS "${QT6_INSTALL_PREFIX}/bin" "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        install(CODE "
            execute_process(COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                --release --no-translations --no-system-d3d-compiler --no-opengl-sw
                \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/kuiper-imager.exe\")
        ")
    else()
        message(WARNING "windeployqt not found; the Windows installer will be missing Qt runtime DLLs")
    endif()
endif()
