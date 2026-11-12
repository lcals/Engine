include_guard(GLOBAL)
if(NOT IOS OR NOT ENGINE_ENABLE_RUNTIME_DEPS OR NOT ENGINE_BUILD_APP)
    return()
endif()
if(ENGINE_ENABLE_EDITOR_DEPS OR ENGINE_ENABLE_ASSET_PIPELINE_DEPS)
    message(FATAL_ERROR "The iOS runtime excludes editor and asset import libraries")
endif()
if(ENGINE_SCRIPT_BACKEND STREQUAL "v8")
    message(FATAL_ERROR "The iOS runtime does not support the V8 backend")
endif()
if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR "The iOS runtime requires one arm64 SDK target")
endif()
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "17.0")
    message(FATAL_ERROR "The iOS runtime requires a deployment target of 17.0 or newer")
endif()
if(CMAKE_OSX_SYSROOT MATCHES "[Ii][Pp]hone[Ss]imulator|iphonesimulator")
    set(ENGINE_IOS_SDK simulator)
else()
    set(ENGINE_IOS_SDK device)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(ENGINE_EXPORT_PROJECT "${PROJECT_SOURCE_DIR}/projects/showcase" CACHE PATH "Exported project")
set(ENGINE_EXPORT_PRODUCT "engine-showcase" CACHE STRING "Project application identity")
set(ENGINE_EXPORT_MANIFEST "settings/editor-resource-build.toml" CACHE STRING "Project build manifest")
if(NOT ENGINE_EXPORT_PRODUCT MATCHES "^[a-z][a-z0-9_-]*$")
    message(FATAL_ERROR "ENGINE_EXPORT_PRODUCT must be a lowercase application identity")
endif()
set(ENGINE_IOS_BUNDLE_IDENTIFIER "org.engine.${ENGINE_EXPORT_PRODUCT}" CACHE STRING "iOS bundle identifier")
if(NOT ENGINE_IOS_BUNDLE_IDENTIFIER MATCHES "^[A-Za-z0-9-]+(\\.[A-Za-z0-9-]+)+$")
    message(FATAL_ERROR "The iOS bundle identifier must be a reverse DNS identifier")
endif()
set(ENGINE_IOS_EXECUTABLE "${ENGINE_EXPORT_PRODUCT}")
if(ENGINE_ENABLE_TEST_SUPPORT)
    string(APPEND ENGINE_IOS_EXECUTABLE "-test")
    string(APPEND ENGINE_IOS_BUNDLE_IDENTIFIER ".test")
endif()
add_executable(engine_project_app MACOSX_BUNDLE platforms/ios/main.cpp src/runtime_entry.cpp)
set_target_properties(engine_project_app PROPERTIES OUTPUT_NAME "${ENGINE_IOS_EXECUTABLE}"
    MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/platforms/ios/templates/info.plist.in"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
target_include_directories(engine_project_app PRIVATE "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src/engine")
target_link_libraries(engine_project_app PRIVATE engine::runtime engine::app engine::log
    SDL3::SDL3 fmt::fmt)
target_compile_definitions(engine_project_app PRIVATE ENGINE_HAS_APP_SERVICES=1
    ENGINE_START_PACKAGED_GAMEPLAY=1 ENGINE_PROJECT_APPLICATION_NAME="${ENGINE_EXPORT_PRODUCT}")
engine_link_resource_plugin(engine_project_app)
engine_link_script_plugin(engine_project_app)
engine_link_animation_retarget_plugin(engine_project_app)
engine_link_character_plugin(engine_project_app)
engine_link_navigation_crowd_plugin(engine_project_app)
engine_link_effects_plugin(engine_project_app)
engine_link_terrain_plugin(engine_project_app)
engine_link_destruction_plugin(engine_project_app)
if(ENGINE_ENABLE_TEST_SUPPORT)
    target_link_libraries(engine_project_app PRIVATE engine::runtime_mcp_tools
        nlohmann_json::nlohmann_json)
    target_compile_definitions(engine_project_app PRIVATE ENGINE_HAS_RUNTIME_MCP_ENDPOINT=1
        JSON_NOEXCEPTION=1)
endif()
engine_configure_runtime_settings(engine_project_app)
engine_apply_options(engine_project_app)
engine_enable_profiling(engine_project_app)
string(REPLACE ";" "\",\"" _ios_plugins "${ENGINE_SELECTED_PLUGINS}")
if(ENGINE_ENABLE_TEST_SUPPORT)
    set(_ios_test true)
else()
    set(_ios_test false)
endif()
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/ios-config.json" CONTENT
"{\n  \"platform\": \"ios\",\n  \"arch\": \"arm64\",\n  \"sdk\": \"${ENGINE_IOS_SDK}\",\n  \"product\": \"${ENGINE_EXPORT_PRODUCT}\",\n  \"executable\": \"${ENGINE_IOS_EXECUTABLE}\",\n  \"bundleIdentifier\": \"${ENGINE_IOS_BUNDLE_IDENTIFIER}\",\n  \"configuration\": \"$<LOWER_CASE:$<CONFIG>>\",\n  \"minimumOS\": \"${CMAKE_OSX_DEPLOYMENT_TARGET}\",\n  \"scriptBackend\": \"${ENGINE_SCRIPT_BACKEND}\",\n  \"testSupport\": ${_ios_test},\n  \"plugins\": [\"${_ios_plugins}\"]\n}\n")
set_property(TARGET engine_project_app APPEND PROPERTY LINK_DEPENDS
    "${PROJECT_SOURCE_DIR}/platforms/ios/export.py" "${PROJECT_BINARY_DIR}/ios-config.json")
add_custom_command(TARGET engine_project_app POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/platforms/ios/export.py" record
        --config "${PROJECT_BINARY_DIR}/ios-config.json"
        --bundle "$<TARGET_BUNDLE_DIR:engine_project_app>"
        --output "${PROJECT_BINARY_DIR}/ios-build.json" VERBATIM)
set(ENGINE_IOS_ASSET_BUILDER "" CACHE FILEPATH "Native asset_build_app")
set(ENGINE_IOS_SIGN_IDENTITY "" CACHE STRING "Local iOS code signing identity")
set(ENGINE_IOS_PROVISIONING_PROFILE "" CACHE FILEPATH "Local provisioning profile")
set(ENGINE_IOS_ICON "" CACHE FILEPATH "Compiled application asset catalog")
set(ENGINE_IOS_PRIVACY_MANIFEST "" CACHE FILEPATH "Application privacy manifest")
option(ENGINE_IOS_EXPORT_IPA "Export a signed device IPA" OFF)
if(ENGINE_IOS_ASSET_BUILDER)
    set(_ios_export_args)
    foreach(_ios_pair SIGN_IDENTITY PROVISIONING_PROFILE ICON PRIVACY_MANIFEST)
        if(ENGINE_IOS_${_ios_pair})
            string(TOLOWER "${_ios_pair}" _ios_option)
            string(REPLACE "_" "-" _ios_option "${_ios_option}")
            list(APPEND _ios_export_args "--${_ios_option}" "${ENGINE_IOS_${_ios_pair}}")
        endif()
    endforeach()
    if(ENGINE_ENABLE_TEST_SUPPORT)
        list(APPEND _ios_export_args --test-package)
    endif()
    if(ENGINE_IOS_EXPORT_IPA)
        list(APPEND _ios_export_args --ipa)
    endif()
    add_custom_target(engine_export_ios
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/platforms/ios/export.py" export
            --project "${ENGINE_EXPORT_PROJECT}" --builder "${ENGINE_IOS_ASSET_BUILDER}"
            --bundle "$<TARGET_BUNDLE_DIR:engine_project_app>"
            --receipt "${PROJECT_BINARY_DIR}/ios-build.json" --product "${ENGINE_EXPORT_PRODUCT}"
            --configuration "$<LOWER_CASE:$<CONFIG>>" --sdk "${ENGINE_IOS_SDK}"
            --manifest "${ENGINE_EXPORT_MANIFEST}" ${_ios_export_args}
            --dependency-licenses "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share"
            --output "${PROJECT_SOURCE_DIR}/output/ios/arm64/$<LOWER_CASE:$<CONFIG>>/runtime/${ENGINE_IOS_SDK}/${ENGINE_IOS_EXECUTABLE}"
        DEPENDS engine_project_app USES_TERMINAL VERBATIM)
endif()
