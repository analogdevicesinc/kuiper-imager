find_package(Qt6 6.4 REQUIRED COMPONENTS Core)

# WIN32: GUI subsystem on Windows, so UAC elevation flashes no console window (the
# helper talks to the GUI over a named pipe, not stdio). Ignored elsewhere. Qt6::Core
# links Qt6::EntryPointPrivate (WinMain -> main) automatically for such targets.
qt_add_executable(kuiper-imager-helper WIN32
    main.cpp
    Channel.hpp
    Channel.cpp)

target_include_directories(kuiper-imager-helper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kuiper-imager-helper PRIVATE kuiper::kuiper Qt6::Core)

if(MSVC)
    # Embed requireAdministrator in the linker-generated manifest: self-documents
    # intent, shows the UAC shield, and elevates even if launched directly
    # (defense in depth). Driving the UAC level through the linker avoids the
    # dual-manifest conflict a separate .rc RT_MANIFEST can cause.
    target_link_options(kuiper-imager-helper PRIVATE
        "/MANIFESTUAC:level='requireAdministrator'")
endif()

# The helper is not a user-facing command. On Unix it goes to libexec, where the
# GUI's baked-in fallback and the polkit action expect it (KUIPER_HELPER_INSTALL_PATH
# at the top level). On Windows it ships beside the GUI exe, where the GUI's
# resolver looks for it.
if(WIN32)
    install(TARGETS kuiper-imager-helper RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
    install(TARGETS kuiper-imager-helper RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
endif()
