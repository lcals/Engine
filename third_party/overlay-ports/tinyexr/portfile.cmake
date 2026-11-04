vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO syoyo/tinyexr
    REF "v${VERSION}"
    SHA512 850b812de7d42f202021fa52ced7d6d7c23998d599d337c55ba895797dfa7d3916a1ecf5ff13d797c0bee14886bc85645812290e08e35c35833ed65a6f9cdf42
    HEAD_REF master
    PATCHES
        fixtargets.patch
)

vcpkg_cmake_configure(
    SOURCE_PATH ${SOURCE_PATH}
    OPTIONS
        -DTINYEXR_BUILD_SAMPLE=OFF
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(INSTALL ${SOURCE_PATH}/README.md DESTINATION ${CURRENT_PACKAGES_DIR}/share/${PORT} RENAME copyright)
