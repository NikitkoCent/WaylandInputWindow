# TODO: better docs
# TODO: hide and/or clear all internal variables except the ones mentioned in the docs
# TODO: use find_package_message
#
# Try to find various Wayland components on a Unix system
#
# This will define:
#
#           Wayland_FOUND           - TRUE if all the REQUIRED components have been found ; FALSE otherwise
#
#   The following components are supported:
#     - "Protocols" defines:
#           Wayland_Protocols_FOUND - TRUE if a directory with the Wayland extension protocols .xml files has
#                                     been found ; FALSE otherwise
#           Wayland_Protocols_DIR   - Path to the dir containing the Wayland extension protocols .xml files
#     - "Scanner" defines:
#           Wayland::Scanner        - an IMPORTED executable target, its LOCATION property specifies the path to the
#                                     wayland-scanner executable
#     - "Server" defines:
#           Wayland::Server         - an IMPORTED library target pointing to libwayland-server and propagating the
#                                     required compiler and linker options through
#                                     target_link_libraries(<your_target> Wayland::Server)
#     - "Client" defines:
#           Wayland::Client         - an IMPORTED library target pointing to libwayland-client and propagating the
#                                     required compiler and linker options through
#                                     target_link_libraries(<your_target> Wayland::Client)
#     - "Cursor" defines:
#           Wayland::Cursor         - an IMPORTED library target pointing to libwayland-cursor and propagating the
#                                     required compiler and linker options through
#                                     target_link_libraries(<your_target> Wayland::Cursor)
#     - "Egl" defines:
#           Wayland::Egl            - an IMPORTED library target pointing to libwayland-egl and propagating the
#                                     required compiler and linker options through
#                                     target_link_libraries(<your_target> Wayland::Egl)


# ============================================ Invocation parameters check ============================================
if (Wayland_FIND_VERSION_RANGE)
    message(FATAL_ERROR "The Wayland package doesn't have a version (only its components do), but a version range (\"${Wayland_FIND_VERSION_RANGE}\") has been requested.")
endif()

if (Wayland_FIND_VERSION)
    if (Wayland_FIND_VERSION_EXACT)
        message(FATAL_ERROR "The Wayland package doesn't have a version (only its components do), but the EXACT match to a version (=${Wayland_FIND_VERSION}) has been requested.")
    else ()
        message(WARNING "The package's requested version (=${Wayland_FIND_VERSION}) is ignored because the package doesn't have a version, only its components do.")
    endif ()
endif ()

if (NOT Wayland_FIND_COMPONENTS)
    message(FATAL_ERROR "No components to search have been specified.")
endif ()
# =====================================================================================================================

if (Wayland_FIND_QUIETLY)
    set(Wayland_QUIETOREMPTY "QUIET")
else ()
    set(Wayland_QUIETOREMPTY "")
endif ()

# Use pkg-config to get the paths to libraries and headers directories
find_package(PkgConfig ${Wayland_QUIETOREMPTY})

foreach (WLCOMPONENT ${Wayland_FIND_COMPONENTS})
    # libwayland-scanner -> Wayland::Scanner
    if (WLCOMPONENT STREQUAL "Scanner")
        if (NOT TARGET Wayland::Scanner)
            if (PKG_CONFIG_FOUND)
                pkg_check_modules(PKGCONFIG_WAYLANDSCANNER ${Wayland_QUIETOREMPTY} wayland-scanner)
            endif ()

            if (PKGCONFIG_WAYLANDSCANNER_FOUND EQUAL 1)
                #message("PKGCONFIG_WAYLANDSCANNER_LIBRARIES=${PKGCONFIG_WAYLANDSCANNER_LIBRARIES}")
                #message("PKGCONFIG_WAYLANDSCANNER_LINK_LIBRARIES=${PKGCONFIG_WAYLANDSCANNER_LINK_LIBRARIES}")
                #message("PKGCONFIG_WAYLANDSCANNER_LIBRARY_DIRS=${PKGCONFIG_WAYLANDSCANNER_LIBRARY_DIRS}")
                #message("PKGCONFIG_WAYLANDSCANNER_LDFLAGS=${PKGCONFIG_WAYLANDSCANNER_LDFLAGS}")
                #message("PKGCONFIG_WAYLANDSCANNER_INCLUDE_DIRS=${PKGCONFIG_WAYLANDSCANNER_INCLUDE_DIRS}")
                #message("PKGCONFIG_WAYLANDSCANNER_CFLAGS=${PKGCONFIG_WAYLANDSCANNER_CFLAGS}")
                #message("PKGCONFIG_WAYLANDSCANNER_CFLAGS_OTHER=${PKGCONFIG_WAYLANDSCANNER_CFLAGS_OTHER}")
                #message("PKGCONFIG_WAYLANDSCANNER_VERSION=${PKGCONFIG_WAYLANDSCANNER_VERSION}")
                #message("PKGCONFIG_WAYLANDSCANNER_PREFIX=${PKGCONFIG_WAYLANDSCANNER_PREFIX}")
                #message("PKGCONFIG_WAYLANDSCANNER_INCLUDEDIR=${PKGCONFIG_WAYLANDSCANNER_INCLUDEDIR}")
                #message("PKGCONFIG_WAYLANDSCANNER_LIBDIR=${PKGCONFIG_WAYLANDSCANNER_LIBDIR}")

                pkg_get_variable(PKGCONFIG_WAYLANDSCANNER_EXECUTABLE wayland-scanner wayland_scanner)
                #message("PKGCONFIG_WAYLANDSCANNER_EXECUTABLE=${PKGCONFIG_WAYLANDSCANNER_EXECUTABLE}")
            endif ()

            if ( (NOT DEFINED PKGCONFIG_WAYLANDSCANNER_EXECUTABLE) OR (PKGCONFIG_WAYLANDSCANNER_EXECUTABLE STREQUAL "") )
                find_program(PKGCONFIG_WAYLANDSCANNER_EXECUTABLE wayland-scanner)
            endif ()

            #message("PKGCONFIG_WAYLANDSCANNER_EXECUTABLE=${PKGCONFIG_WAYLANDSCANNER_EXECUTABLE}")

            if (EXISTS "${PKGCONFIG_WAYLANDSCANNER_EXECUTABLE}")
                add_executable(Wayland::Scanner IMPORTED)

                set_target_properties(Wayland::Scanner PROPERTIES
                    IMPORTED_LOCATION "${PKGCONFIG_WAYLANDSCANNER_EXECUTABLE}"
                    VERSION "${PKGCONFIG_WAYLANDSCANNER_VERSION}"
                )
            elseif (NOT Wayland_FIND_QUIETLY)
                message(STATUS "The Wayland::Scanner component (wayland-scanner) hasn't been found")
            endif ()
        endif ()

    # libwayland-server -> Wayland::Server
    elseif (WLCOMPONENT STREQUAL "Server")
        if (NOT TARGET Wayland::Server)
            if (PKG_CONFIG_FOUND)
                pkg_check_modules(PKGCONFIG_WAYLANDSERVER ${Wayland_QUIETOREMPTY} wayland-server)
            endif ()

            if (PKGCONFIG_WAYLANDSERVER_FOUND EQUAL 1)
                #message("PKGCONFIG_WAYLANDSERVER_LIBRARIES=${PKGCONFIG_WAYLANDSERVER_LIBRARIES}")
                #message("PKGCONFIG_WAYLANDSERVER_LINK_LIBRARIES=${PKGCONFIG_WAYLANDSERVER_LINK_LIBRARIES}")
                #message("PKGCONFIG_WAYLANDSERVER_LIBRARY_DIRS=${PKGCONFIG_WAYLANDSERVER_LIBRARY_DIRS}")
                #message("PKGCONFIG_WAYLANDSERVER_LDFLAGS=${PKGCONFIG_WAYLANDSERVER_LDFLAGS}")
                #message("PKGCONFIG_WAYLANDSERVER_INCLUDE_DIRS=${PKGCONFIG_WAYLANDSERVER_INCLUDE_DIRS}")
                #message("PKGCONFIG_WAYLANDSERVER_CFLAGS=${PKGCONFIG_WAYLANDSERVER_CFLAGS}")
                #message("PKGCONFIG_WAYLANDSERVER_CFLAGS_OTHER=${PKGCONFIG_WAYLANDSERVER_CFLAGS_OTHER}")
                #message("PKGCONFIG_WAYLANDSERVER_VERSION=${PKGCONFIG_WAYLANDSERVER_VERSION}")
                #message("PKGCONFIG_WAYLANDSERVER_PREFIX=${PKGCONFIG_WAYLANDSERVER_PREFIX}")
                #message("PKGCONFIG_WAYLANDSERVER_INCLUDEDIR=${PKGCONFIG_WAYLANDSERVER_INCLUDEDIR}")
                #message("PKGCONFIG_WAYLANDSERVER_LIBDIR=${PKGCONFIG_WAYLANDSERVER_LIBDIR}")

                add_library(Wayland::Server SHARED IMPORTED)

                set_target_properties(Wayland::Server PROPERTIES
                    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                    IMPORTED_LOCATION "${PKGCONFIG_WAYLANDSERVER_LINK_LIBRARIES}"

                    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${PKGCONFIG_WAYLANDSERVER_INCLUDE_DIRS}"
                    INTERFACE_COMPILE_OPTIONS "${PKGCONFIG_WAYLANDSERVER_CFLAGS}"

                    INTERFACE_LINK_DIRECTORIES "${PKGCONFIG_WAYLANDSERVER_LIBRARY_DIRS}"
                    INTERFACE_LINK_LIBRARIES "${PKGCONFIG_WAYLANDSERVER_LINK_LIBRARIES}"
                    INTERFACE_LINK_OPTIONS "${PKGCONFIG_WAYLANDSERVER_LDFLAGS}"

                    VERSION "${PKGCONFIG_WAYLANDSERVER_VERSION}"
                )
            elseif (NOT Wayland_FIND_QUIETLY)
                message(STATUS "The Wayland::Server component (wayland-server) hasn't been found")
            endif ()
        endif ()

    # libwayland-client -> Wayland::Client
    elseif (WLCOMPONENT STREQUAL "Client")
        if (NOT TARGET Wayland::Client)
            if (PKG_CONFIG_FOUND)
                pkg_check_modules(PKGCONFIG_WAYLANDCLIENT ${Wayland_QUIETOREMPTY} wayland-client)
            endif ()

            if (PKGCONFIG_WAYLANDCLIENT_FOUND EQUAL 1)
                #message("PKGCONFIG_WAYLANDCLIENT_LIBRARIES=${PKGCONFIG_WAYLANDCLIENT_LIBRARIES}")
                #message("PKGCONFIG_WAYLANDCLIENT_LINK_LIBRARIES=${PKGCONFIG_WAYLANDCLIENT_LINK_LIBRARIES}")
                #message("PKGCONFIG_WAYLANDCLIENT_LIBRARY_DIRS=${PKGCONFIG_WAYLANDCLIENT_LIBRARY_DIRS}")
                #message("PKGCONFIG_WAYLANDCLIENT_LDFLAGS=${PKGCONFIG_WAYLANDCLIENT_LDFLAGS}")
                #message("PKGCONFIG_WAYLANDCLIENT_INCLUDE_DIRS=${PKGCONFIG_WAYLANDCLIENT_INCLUDE_DIRS}")
                #message("PKGCONFIG_WAYLANDCLIENT_CFLAGS=${PKGCONFIG_WAYLANDCLIENT_CFLAGS}")
                #message("PKGCONFIG_WAYLANDCLIENT_CFLAGS_OTHER=${PKGCONFIG_WAYLANDCLIENT_CFLAGS_OTHER}")
                #message("PKGCONFIG_WAYLANDCLIENT_VERSION=${PKGCONFIG_WAYLANDCLIENT_VERSION}")
                #message("PKGCONFIG_WAYLANDCLIENT_PREFIX=${PKGCONFIG_WAYLANDCLIENT_PREFIX}")
                #message("PKGCONFIG_WAYLANDCLIENT_INCLUDEDIR=${PKGCONFIG_WAYLANDCLIENT_INCLUDEDIR}")
                #message("PKGCONFIG_WAYLANDCLIENT_LIBDIR=${PKGCONFIG_WAYLANDCLIENT_LIBDIR}")

                add_library(Wayland::Client SHARED IMPORTED)

                set_target_properties(Wayland::Client PROPERTIES
                    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                    IMPORTED_LOCATION "${PKGCONFIG_WAYLANDCLIENT_LINK_LIBRARIES}"

                    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${PKGCONFIG_WAYLANDCLIENT_INCLUDE_DIRS}"
                    INTERFACE_COMPILE_OPTIONS "${PKGCONFIG_WAYLANDCLIENT_CFLAGS}"

                    INTERFACE_LINK_DIRECTORIES "${PKGCONFIG_WAYLANDCLIENT_LIBRARY_DIRS}"
                    INTERFACE_LINK_LIBRARIES "${PKGCONFIG_WAYLANDCLIENT_LINK_LIBRARIES}"
                    INTERFACE_LINK_OPTIONS "${PKGCONFIG_WAYLANDCLIENT_LDFLAGS}"

                    VERSION "${PKGCONFIG_WAYLANDCLIENT_VERSION}"
                )
            elseif (NOT Wayland_FIND_QUIETLY)
                message(STATUS "The Wayland::Client component (wayland-client) hasn't been found")
            endif ()
        endif ()

    # libwayland-cursor -> Wayland::Cursor
    elseif (WLCOMPONENT STREQUAL "Cursor")
        if (NOT TARGET Wayland::Cursor)
            if (PKG_CONFIG_FOUND)
                pkg_check_modules(PKGCONFIG_WAYLANDCURSOR ${Wayland_QUIETOREMPTY} wayland-cursor)
            endif ()

            if (PKGCONFIG_WAYLANDCURSOR_FOUND EQUAL 1)
                #message("PKGCONFIG_WAYLANDCURSOR_LIBRARIES=${PKGCONFIG_WAYLANDCURSOR_LIBRARIES}")
                #message("PKGCONFIG_WAYLANDCURSOR_LINK_LIBRARIES=${PKGCONFIG_WAYLANDCURSOR_LINK_LIBRARIES}")
                #message("PKGCONFIG_WAYLANDCURSOR_LIBRARY_DIRS=${PKGCONFIG_WAYLANDCURSOR_LIBRARY_DIRS}")
                #message("PKGCONFIG_WAYLANDCURSOR_LDFLAGS=${PKGCONFIG_WAYLANDCURSOR_LDFLAGS}")
                #message("PKGCONFIG_WAYLANDCURSOR_INCLUDE_DIRS=${PKGCONFIG_WAYLANDCURSOR_INCLUDE_DIRS}")
                #message("PKGCONFIG_WAYLANDCURSOR_CFLAGS=${PKGCONFIG_WAYLANDCURSOR_CFLAGS}")
                #message("PKGCONFIG_WAYLANDCURSOR_CFLAGS_OTHER=${PKGCONFIG_WAYLANDCURSOR_CFLAGS_OTHER}")
                #message("PKGCONFIG_WAYLANDCURSOR_VERSION=${PKGCONFIG_WAYLANDCURSOR_VERSION}")
                #message("PKGCONFIG_WAYLANDCURSOR_PREFIX=${PKGCONFIG_WAYLANDCURSOR_PREFIX}")
                #message("PKGCONFIG_WAYLANDCURSOR_INCLUDEDIR=${PKGCONFIG_WAYLANDCURSOR_INCLUDEDIR}")
                #message("PKGCONFIG_WAYLANDCURSOR_LIBDIR=${PKGCONFIG_WAYLANDCURSOR_LIBDIR}")

                add_library(Wayland::Cursor SHARED IMPORTED)

                set_target_properties(Wayland::Cursor PROPERTIES
                    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                    IMPORTED_LOCATION "${PKGCONFIG_WAYLANDCURSOR_LINK_LIBRARIES}"

                    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${PKGCONFIG_WAYLANDCURSOR_INCLUDE_DIRS}"
                    INTERFACE_COMPILE_OPTIONS "${PKGCONFIG_WAYLANDCURSOR_CFLAGS}"

                    INTERFACE_LINK_DIRECTORIES "${PKGCONFIG_WAYLANDCURSOR_LIBRARY_DIRS}"
                    INTERFACE_LINK_LIBRARIES "${PKGCONFIG_WAYLANDCURSOR_LINK_LIBRARIES}"
                    INTERFACE_LINK_OPTIONS "${PKGCONFIG_WAYLANDCURSOR_LDFLAGS}"

                    VERSION "${PKGCONFIG_WAYLANDCURSOR_VERSION}"
                )
            elseif (NOT Wayland_FIND_QUIETLY)
                message(STATUS "The Wayland::Cursor component (wayland-cursor) hasn't been found")
            endif ()
        endif ()

    # libwayland-egl -> Wayland::Egl
    elseif (WLCOMPONENT STREQUAL "Egl")
        if (NOT TARGET Wayland::Egl)
            if (PKG_CONFIG_FOUND)
                pkg_check_modules(PKGCONFIG_WAYLANDEGL ${Wayland_QUIETOREMPTY} wayland-egl)
            endif ()

            if (PKGCONFIG_WAYLANDEGL_FOUND EQUAL 1)
                #message("PKGCONFIG_WAYLANDEGL_LIBRARIES=${PKGCONFIG_WAYLANDEGL_LIBRARIES}")
                #message("PKGCONFIG_WAYLANDEGL_LINK_LIBRARIES=${PKGCONFIG_WAYLANDEGL_LINK_LIBRARIES}")
                #message("PKGCONFIG_WAYLANDEGL_LIBRARY_DIRS=${PKGCONFIG_WAYLANDEGL_LIBRARY_DIRS}")
                #message("PKGCONFIG_WAYLANDEGL_LDFLAGS=${PKGCONFIG_WAYLANDEGL_LDFLAGS}")
                #message("PKGCONFIG_WAYLANDEGL_INCLUDE_DIRS=${PKGCONFIG_WAYLANDEGL_INCLUDE_DIRS}")
                #message("PKGCONFIG_WAYLANDEGL_CFLAGS=${PKGCONFIG_WAYLANDEGL_CFLAGS}")
                #message("PKGCONFIG_WAYLANDEGL_CFLAGS_OTHER=${PKGCONFIG_WAYLANDEGL_CFLAGS_OTHER}")
                #message("PKGCONFIG_WAYLANDEGL_VERSION=${PKGCONFIG_WAYLANDEGL_VERSION}")
                #message("PKGCONFIG_WAYLANDEGL_PREFIX=${PKGCONFIG_WAYLANDEGL_PREFIX}")
                #message("PKGCONFIG_WAYLANDEGL_INCLUDEDIR=${PKGCONFIG_WAYLANDEGL_INCLUDEDIR}")
                #message("PKGCONFIG_WAYLANDEGL_LIBDIR=${PKGCONFIG_WAYLANDEGL_LIBDIR}")

                add_library(Wayland::Egl SHARED IMPORTED)

                set_target_properties(Wayland::Egl PROPERTIES
                    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                    IMPORTED_LOCATION "${PKGCONFIG_WAYLANDEGL_LINK_LIBRARIES}"

                    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${PKGCONFIG_WAYLANDEGL_INCLUDE_DIRS}"
                    INTERFACE_COMPILE_OPTIONS "${PKGCONFIG_WAYLANDEGL_CFLAGS}"

                    INTERFACE_LINK_DIRECTORIES "${PKGCONFIG_WAYLANDEGL_LIBRARY_DIRS}"
                    INTERFACE_LINK_LIBRARIES "${PKGCONFIG_WAYLANDEGL_LINK_LIBRARIES}"
                    INTERFACE_LINK_OPTIONS "${PKGCONFIG_WAYLANDEGL_LDFLAGS}"

                    VERSION "${PKGCONFIG_WAYLANDEGL_VERSION}"
                )
            elseif (NOT Wayland_FIND_QUIETLY)
                message(STATUS "The Wayland::Egl component (wayland-egl) hasn't been found")
            endif ()
        endif ()

    elseif (WLCOMPONENT STREQUAL "Protocols")
        if (NOT DEFINED PKGCONFIG_WAYLANDPROTOCOLS_DIR)
            if (PKG_CONFIG_FOUND)
                pkg_get_variable(PKGCONFIG_WAYLANDPROTOCOLS_DIR wayland-protocols pkgdatadir)
                message("PKGCONFIG_WAYLANDPROTOCOLS_DIR=${PKGCONFIG_WAYLANDPROTOCOLS_DIR}")
            endif ()
        endif ()

    elseif (NOT Wayland_FIND_QUIETLY)
        message(STATUS "Skipping unknown component \"${WLCOMPONENT}\"...")
    endif ()
endforeach ()


# ==================================== Setting Wayland_<Component>_FOUND variables ====================================
if (TARGET Wayland::Scanner)
    set(Wayland_Scanner_FOUND TRUE)
else ()
    set(Wayland_Scanner_FOUND FALSE)
endif ()

if (TARGET Wayland::Server)
    set(Wayland_Server_FOUND TRUE)
else ()
    set(Wayland_Server_FOUND FALSE)
endif ()

if (TARGET Wayland::Client)
    set(Wayland_Client_FOUND TRUE)
else ()
    set(Wayland_Client_FOUND FALSE)
endif ()

if (TARGET Wayland::Cursor)
    set(Wayland_Cursor_FOUND TRUE)
else ()
    set(Wayland_Cursor_FOUND FALSE)
endif ()

if (TARGET Wayland::Egl)
    set(Wayland_Egl_FOUND TRUE)
else ()
    set(Wayland_Egl_FOUND FALSE)
endif ()

if (IS_DIRECTORY "${PKGCONFIG_WAYLANDPROTOCOLS_DIR}")
    set(Wayland_Protocols_FOUND TRUE)
    string(REGEX REPLACE "^/+(.+)$" "/\\1" Wayland_Protocols_DIR "${PKGCONFIG_WAYLANDPROTOCOLS_DIR}")
else ()
    set(Wayland_Protocols_FOUND FALSE)
    unset(Wayland_Protocols_DIR)
endif ()
# =====================================================================================================================

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Wayland
    FOUND_VAR Wayland_FOUND
    HANDLE_COMPONENTS
)
