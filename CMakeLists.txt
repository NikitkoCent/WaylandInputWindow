cmake_minimum_required(VERSION 3.14)

project(WaylandInputWindow
    HOMEPAGE_URL "https://github.com/NikitkoCent/WaylandInputWindow"
    LANGUAGES CXX
)

find_package(Threads REQUIRED)

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

# TODO:
# 1. Find the wayland-client library
# 2. Provide its header path directory to the target

target_link_libraries(WaylandInputWindow
    PRIVATE Threads::Threads
    PRIVATE wayland-client # TODO: use the variable provided as a result of finding the wayland-client library
)
