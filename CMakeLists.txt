cmake_minimum_required(VERSION 3.19)


project(WaylandInputWindow
    HOMEPAGE_URL "https://github.com/NikitkoCent/WaylandInputWindow"
    LANGUAGES C CXX
)


# All the required packages
find_package(Threads REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Wayland REQUIRED
    COMPONENTS Client
               Scanner
               Protocols
)
get_target_property(WaylandScannerPath Wayland::Scanner LOCATION)

# TODO: find libxkbcommon


# ============================ Generating sources for the used Wayland extension protocols ============================
if (EXISTS "${Wayland_Protocols_DIR}/stable/xdg-shell/xdg-shell.xml")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wayland-scanner/xdg-shell/include" "${CMAKE_CURRENT_BINARY_DIR}/wayland-scanner/xdg-shell/src")
    execute_process(
        COMMAND "${WaylandScannerPath}" "private-code"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wayland-scanner/xdg-shell"
        TIMEOUT 2
        INPUT_FILE "${Wayland_Protocols_DIR}/stable/xdg-shell/xdg-shell.xml"
        OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/wayland-scanner/xdg-shell/src/xdg-shell.c"
        ECHO_ERROR_VARIABLE
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND "${WaylandScannerPath}" "client-header"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/wayland-scanner/xdg-shell"
        TIMEOUT 2
        INPUT_FILE "${Wayland_Protocols_DIR}/stable/xdg-shell/xdg-shell.xml"
        OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/wayland-scanner/xdg-shell/include/xdg-shell.h"
        ECHO_ERROR_VARIABLE
        COMMAND_ERROR_IS_FATAL ANY
    )
    add_library(WaylandExXdgShell STATIC
        "${CMAKE_CURRENT_BINARY_DIR}/wayland-scanner/xdg-shell/src/xdg-shell.c"
    )
    target_include_directories(WaylandExXdgShell SYSTEM
        PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/wayland-scanner/xdg-shell/include"
    )
    set_target_properties(WaylandExXdgShell PROPERTIES
        LINKER_LANGUAGE C
    )
    target_link_libraries(WaylandExXdgShell
        PUBLIC Wayland::Client
    )
else ()
    message(FATAL_ERROR "Couldn't find the xdg-shell protocol at ${Wayland_Protocols_DIR}/stable/xdg-shell/xdg-shell.xml")
endif ()
# =====================================================================================================================


add_executable(WaylandInputWindow
    main.cpp
    utilities.h
)

set_target_properties(WaylandInputWindow PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if ((CMAKE_CXX_COMPILER_ID STREQUAL "Clang") OR (CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
    target_compile_options(WaylandInputWindow
        PRIVATE -Wall        # basic set of warnings
        PRIVATE -Wextra      # additional warnings
        PRIVATE -pedantic    # modern C++ inspections
        PRIVATE -Werror      # treat all warnings as errors
    )
endif()

target_compile_definitions(WaylandInputWindow
    PRIVATE "CMAKE_PROJECT_PATH=\"${PROJECT_SOURCE_DIR}\""
)

target_link_libraries(WaylandInputWindow
    PRIVATE Threads::Threads
    PRIVATE Wayland::Client
    PRIVATE WaylandExXdgShell
    # TODO: find the library first
    PRIVATE xkbcommon
)
