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
)


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
)
