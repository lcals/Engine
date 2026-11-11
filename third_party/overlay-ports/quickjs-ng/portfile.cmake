if(VCPKG_TARGET_IS_WINDOWS)
    vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
endif()

vcpkg_from_github(
    OUT_SOURCE_PATH
    SOURCE_PATH
    REPO
    quickjs-ng/quickjs
    REF
    v${VERSION}
    SHA512
    8ed91ecca1a509dd67bdb57ca3fda237b1502710043dbbea1335a95e054e4bf1ef0c4c4eba7869d59e2b2bbf6faf2a1c1394a05f231ee78f6d981e9c5460cef0
    HEAD_REF
    master
    PATCHES
    module-precheck.patch)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        libc QJS_BUILD_LIBC
)

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}" OPTIONS ${FEATURE_OPTIONS})

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/qjs PACKAGE_NAME qjs)

if(VCPKG_TARGET_IS_EMSCRIPTEN)
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin")
else()
    vcpkg_copy_tools(TOOL_NAMES qjs qjsc AUTO_CLEAN)
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST
    "${SOURCE_PATH}/LICENSE"
    "${SOURCE_PATH}/libunicode-table.h"
)
