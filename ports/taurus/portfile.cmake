vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO lutaml/taurus
    REF "v${VERSION}"
    SHA512 0
    HEAD_REF main
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        iconv   TAURUS_ENABLE_ICONV
        cli     TAURUS_BUILD_CLI
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DTAURUS_BUILD_CLI=OFF
        -DTAURUS_BUILD_BENCHMARKS=OFF
        -DBUILD_TESTING=OFF
        -DTAURUS_ENABLE_UTF8PROC=ON
        ${FEATURE_OPTIONS}
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(
    PACKAGE_NAME taurus
    CONFIG_PATH lib/cmake/taurus
)

vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
