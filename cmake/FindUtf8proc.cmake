# FindUtf8proc.cmake — modern module-mode finder for utf8proc
#
# Defines the imported target:
#   Utf8proc::Utf8proc
#
# Result variables:
#   Utf8proc_FOUND
#   Utf8proc_VERSION
#
# Preferred over the legacy `unofficial-utf8proc` CONFIG package, which
# is the vcpkg-specific name.  This module finds utf8proc on Homebrew,
# Debian/Ubuntu (libutf8proc-dev), Fedora, and from-source installs.

include(FindPackageHandleStandardArgs)

find_path(Utf8proc_INCLUDE_DIR
    NAMES utf8proc.h
    PATH_SUFFIXES utf8proc)

find_library(Utf8proc_LIBRARY
    NAMES utf8proc libutf8proc)

# Best-effort version extraction. utf8proc.h exposes UTF8PROC_VERSION_*.
set(Utf8proc_VERSION "")
if(Utf8proc_INCLUDE_DIR AND EXISTS "${Utf8proc_INCLUDE_DIR}/utf8proc.h")
    file(STRINGS "${Utf8proc_INCLUDE_DIR}/utf8proc.h" _utf8proc_version_lines
        LIMIT_COUNT 8
        REGEX "^#[ \t]*define[ \t]+UTF8PROC_VERSION_(MAJOR|MINOR|PATCH)")
    set(_ver "")
    foreach(_part MAJOR MINOR PATCH)
        foreach(_line ${_utf8proc_version_lines})
            if(_line MATCHES "UTF8PROC_VERSION_${_part}[ \t]+([0-9]+)")
                if(_ver STREQUAL "")
                    set(_ver "${CMAKE_MATCH_1}")
                else()
                    set(_ver "${_ver}.${CMAKE_MATCH_1}")
                endif()
                break()
            endif()
        endforeach()
    endforeach()
    set(Utf8proc_VERSION "${_ver}")
endif()

find_package_handle_standard_args(Utf8proc
    REQUIRED_VARS Utf8proc_LIBRARY Utf8proc_INCLUDE_DIR
    VERSION_VAR   Utf8proc_VERSION)

if(Utf8proc_FOUND AND NOT TARGET Utf8proc::Utf8proc)
    add_library(Utf8proc::Utf8proc UNKNOWN IMPORTED)
    set_target_properties(Utf8proc::Utf8proc PROPERTIES
        IMPORTED_LOCATION             "${Utf8proc_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Utf8proc_INCLUDE_DIR}")
endif()

mark_as_advanced(Utf8proc_INCLUDE_DIR Utf8proc_LIBRARY)
