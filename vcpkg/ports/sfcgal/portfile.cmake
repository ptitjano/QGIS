vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://gitlab.com/sfcgal/SFCGAL.git
    REF c3480a458da7d2da4429d75a372697080c5ce24a
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DSFCGAL_BUILD_TESTS=OFF
        -DSFCGAL_BUILD_EXAMPLES=OFF
        -DSFCGAL_WITH_EIGEN=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/SFCGAL)
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
