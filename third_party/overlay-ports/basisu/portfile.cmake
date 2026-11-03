vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

# Basis Universal v2_50, commit 9bebe16726b3a61c8c213eeee3b7cffb462ef34e.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO BinomialLLC/basis_universal
    REF "v2_50"
    SHA512 d0ed371c2fc20d0ecff075fec60203e6992393a127c99fd52330fe519ffb27ce5786520998d87e055d47bab09b23c6afc3267db8f1aa633a8ed4f596c92627f9
    HEAD_REF master
    PATCHES
        export-cmake-config.diff
        android-library.patch
        quiet-library-output.patch
)
file(REMOVE_RECURSE "${SOURCE_PATH}/zstd")

set(BASISU_BUILD_TOOLS ON)
if(VCPKG_TARGET_IS_ANDROID OR VCPKG_TARGET_IS_IOS)
    set(BASISU_BUILD_TOOLS OFF)
endif()
set(BASISU_SSE OFF)
if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64" OR VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
    set(BASISU_SSE ON)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DCMAKE_CXX_STANDARD=17
        -DBASISU_SYSTEM_ZSTD=ON
        -DBASISU_EXAMPLES=OFF
        -DBASISU_SSE=${BASISU_SSE}
        -DBASISU_BUILD_TOOLS=${BASISU_BUILD_TOOLS}
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/basisu)

if(BASISU_BUILD_TOOLS)
    vcpkg_copy_tools(TOOL_NAMES "basisu" AUTO_CLEAN)
endif()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(COMMENT [[
basis_universal is provided under Apache-2.0 license terms.
But it includes third-party components with different licenses.]]
    FILE_LIST
        "${SOURCE_PATH}/.reuse/dep5"
        "${SOURCE_PATH}/LICENSE"
        "${SOURCE_PATH}/LICENSES/BSD-3-clause.txt"
        "${SOURCE_PATH}/LICENSES/MIT.txt"
)
