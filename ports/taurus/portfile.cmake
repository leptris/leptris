# For overlay port testing, use the local repository source
get_filename_component(SOURCE_PATH "${CURRENT_PORT_DIR}/../.." ABSOLUTE)

# Determine build type based on linkage
if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    set(BUILD_SHARED ON)
    set(BUILD_STATIC OFF)
else()
    set(BUILD_SHARED OFF)
    set(BUILD_STATIC ON)
endif()

# Native CMake build on all platforms (Windows, Linux, macOS, FreeBSD)
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DTAURUS_BUILD_CLI=OFF
        -DTAURUS_BUILD_BENCHMARKS=OFF
        -DTAURUS_BUILD_MAN_PAGES=OFF
        -DTAURUS_ENABLE_UTF8PROC=ON
        -DTAURUS_ENABLE_ICONV=ON
        -DBUILD_TESTING=OFF
        -DTAURUS_ENABLE_ASAN=OFF
        -DTAURUS_ENABLE_FUZZING=OFF
        -DTAURUS_BUILD_DOCS=OFF
)

vcpkg_cmake_install()

# Fix CMake config file paths
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/taurus)

# Copy PDB files (Windows only)
vcpkg_copy_pdbs()

# Remove duplicate files
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

# Install copyright
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
