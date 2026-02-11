# FindUtf8proc.cmake - Find utf8proc library
# This module finds the utf8proc library and defines:
#   unofficial-utf8proc_FOUND - true if utf8proc was found
#   unofficial-utf8proc_INCLUDE_DIRS - include directories
#   unofficial-utf8proc_LIBRARIES - libraries to link
#   unofficial-utf8proc_VERSION - version string

# Try to find utf8proc using common installation paths
set(UTF8PROC_POSSIBLE_PATHS
    /opt/homebrew/opt/utf8proc
    /usr/local
    /usr
)

# Find include directory
foreach(SEARCH_PATH ${UTF8PROC_POSSIBLE_PATHS})
    if(EXISTS "${SEARCH_PATH}/include/utf8proc.h")
        set(utf8proc_INCLUDE_DIR "${SEARCH_PATH}/include")
        break()
    endif()
endforeach()

# Find library directory
foreach(SEARCH_PATH ${UTF8PROC_POSSIBLE_PATHS})
    if(EXISTS "${SEARCH_PATH}/lib/libutf8proc.dylib" OR
       EXISTS "${SEARCH_PATH}/lib/libutf8proc.a" OR
       EXISTS "${SEARCH_PATH}/lib64/libutf8proc.dylib")
        set(utf8proc_LIBRARY_DIR "${SEARCH_PATH}/lib")
        break()
    endif()
endforeach()

# Set found flag
if(utf8proc_INCLUDE_DIR AND utf8proc_LIBRARY_DIR)
    set(unofficial-utf8proc_FOUND TRUE)
    set(unofficial-utf8proc_INCLUDE_DIRS "${utf8proc_INCLUDE_DIR}")
    set(unofficial-utf8proc_LIBRARY_DIRS "${utf8proc_LIBRARY_DIR}")

    # Find the library file
    find_library(UTF8PROC_LIBRARY
        NAMES utf8proc
        PATHS ${utf8proc_LIBRARY_DIR}
    )

    if(UTF8PROC_LIBRARY)
        set(unofficial-utf8proc_LIBRARIES ${UTF8PROC_LIBRARY})
    endif()

    message(STATUS "utf8proc: Found at ${utf8proc_INCLUDE_DIR} and ${utf8proc_LIBRARY_DIR}")
else()
    set(unofficial-utf8proc_FOUND FALSE)
    message(STATUS "utf8proc: NOT FOUND (try: brew install utf8proc)")
endif()
