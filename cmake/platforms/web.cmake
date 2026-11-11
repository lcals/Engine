include_guard(GLOBAL)
if(NOT EMSCRIPTEN OR NOT ENGINE_ENABLE_RUNTIME_DEPS OR NOT ENGINE_BUILD_APP)
    return()
endif()
if(ENGINE_ENABLE_EDITOR_DEPS OR ENGINE_ENABLE_ASSET_PIPELINE_DEPS OR ENGINE_ENABLE_TEST_SUPPORT)
    message(FATAL_ERROR "The Web runtime excludes editor libraries and test injection endpoints")
endif()
if(ENGINE_SCRIPT_BACKEND STREQUAL "v8")
    message(FATAL_ERROR "The Web runtime requires the QuickJS script backend")
endif()
foreach(_web_required runtime/resource_graph runtime/script)
    if(NOT _web_required IN_LIST ENGINE_SELECTED_PLUGINS)
        message(FATAL_ERROR "The Web game runtime requires ${_web_required}")
    endif()
endforeach()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# Git 子项目同样使用共享 Wasm 内存；vcpkg 依赖由 Web triplet 提供该选项。
foreach(_web_dependency engine_blast engine_bgfx_debugdraw engine_effects_backend
        Effekseer EffekseerRendererCommon ozz_animation ozz_animation_offline ozz_base ozz_geometry)
    if(TARGET ${_web_dependency})
        target_compile_options(${_web_dependency} PRIVATE -pthread)
    endif()
endforeach()
set(_web_support "${PROJECT_BINARY_DIR}/web-support")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/platforms/web/prepare.py"
    --sdk "$ENV{EMSCRIPTEN_ROOT}" --output "${_web_support}"
    RESULT_VARIABLE _web_prepare_result)
if(NOT _web_prepare_result EQUAL 0)
    message(FATAL_ERROR "Cannot prepare the pinned Web runtime support")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/platforms/web/toolchain.json"
    "${PROJECT_SOURCE_DIR}/platforms/web/prepare.py"
    "${PROJECT_SOURCE_DIR}/third_party/emscripten/idbfs-atomic-sync.patch")

set(ENGINE_EXPORT_PROJECT "${PROJECT_SOURCE_DIR}/projects/showcase" CACHE PATH "Exported project")
set(ENGINE_EXPORT_PRODUCT "engine-showcase" CACHE STRING "Project application identity")
if(NOT ENGINE_EXPORT_PRODUCT MATCHES "^[a-z][a-z0-9_-]*$")
    message(FATAL_ERROR "ENGINE_EXPORT_PRODUCT must be a lowercase application identity")
endif()
function(engine_add_web_runtime target)
    add_executable(${target} platforms/web/main.cpp src/runtime_entry.cpp ${ARGN})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME engine SUFFIX ".mjs")
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}/src/engine" "${PROJECT_SOURCE_DIR}/platforms")
    target_link_libraries(${target} PRIVATE engine::runtime engine::app engine::log
        engine::render engine::world engine::storage SDL3::SDL3 fmt::fmt)
    target_compile_definitions(${target} PRIVATE ENGINE_HAS_APP_SERVICES=1
        ENGINE_START_PACKAGED_GAMEPLAY=1 ENGINE_PROJECT_APPLICATION_NAME="${ENGINE_EXPORT_PRODUCT}")
    engine_link_resource_plugin(${target})
    engine_link_script_plugin(${target})
    engine_link_animation_retarget_plugin(${target})
    engine_link_character_plugin(${target})
    engine_link_navigation_crowd_plugin(${target})
    engine_link_effects_plugin(${target})
    engine_link_terrain_plugin(${target})
    engine_link_destruction_plugin(${target})
    engine_configure_runtime_settings(${target})
    engine_apply_options(${target})
    target_compile_options(${target} PRIVATE --use-port=emdawnwebgpu
        -Wno-dollar-in-identifier-extension)
    target_link_options(${target} PRIVATE
        "--js-library=${_web_support}/src/lib/libidbfs.js"
        "-sMODULARIZE=1" "-sEXPORT_ES6=1" "-sINVOKE_RUN=0" "-sEXIT_RUNTIME=0"
        "-sPROXY_TO_PTHREAD=1" "-sOFFSCREENCANVAS_SUPPORT=1"
        "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas" "-sPTHREAD_POOL_SIZE=8"
        "-sALLOW_MEMORY_GROWTH=1" "-sINITIAL_MEMORY=134217728" "-sMAXIMUM_MEMORY=2147483648"
        "-sSTACK_SIZE=4194304" "-sDEFAULT_PTHREAD_STACK_SIZE=2097152"
        "-sASYNCIFY=1" "-sASYNCIFY_STACK_SIZE=65536"
        "-sEXPORTED_FUNCTIONS=['_main','_engine_web_request_exit','_engine_web_storage_open','_engine_web_storage_close','_emscripten_proxy_finish']"
        "-sEXPORTED_RUNTIME_METHODS=['FS','IDBFS','callMain','PThread']")
endfunction()
engine_add_web_runtime(engine_project_app)

string(REPLACE ";" "\",\"" _web_plugins "${ENGINE_SELECTED_PLUGINS}")
file(READ "${PROJECT_SOURCE_DIR}/platforms/web/toolchain.json" _web_toolchain)
string(JSON _web_sdk_version GET "${_web_toolchain}" emscripten)
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/web-config.json" CONTENT
"{\n  \"platform\": \"web\",\n  \"arch\": \"wasm32\",\n  \"product\": \"${ENGINE_EXPORT_PRODUCT}\",\n  \"configuration\": \"$<LOWER_CASE:$<CONFIG>>\",\n  \"scriptBackend\": \"quickjs\",\n  \"testSupport\": false,\n  \"emscripten\": \"${_web_sdk_version}\",\n  \"plugins\": [\"${_web_plugins}\"],\n  \"files\": [\"engine.mjs\", \"engine.wasm\"]\n}\n")
set_property(TARGET engine_project_app APPEND PROPERTY LINK_DEPENDS
    "${_web_support}/src/lib/libidbfs.js" "${PROJECT_BINARY_DIR}/web-config.json"
    "${PROJECT_SOURCE_DIR}/platforms/web/write_build_info.py")
add_custom_command(TARGET engine_project_app POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/platforms/web/write_build_info.py"
        --config "${PROJECT_BINARY_DIR}/web-config.json" --module "$<TARGET_FILE:engine_project_app>"
        --output "${PROJECT_BINARY_DIR}/web-build.json" VERBATIM)

set(ENGINE_WEB_ASSET_BUILDER "" CACHE FILEPATH "Native asset_build_app with WGSL support")
set(ENGINE_EXPORT_MANIFEST "settings/editor-resource-build.toml" CACHE STRING "Project build manifest")
if(ENGINE_WEB_ASSET_BUILDER)
    find_program(ENGINE_WEB_NODE NAMES node REQUIRED NO_CMAKE_FIND_ROOT_PATH)
    set(ENGINE_WEB_FLATC "${ENGINE_FLATC_EXECUTABLE}" CACHE FILEPATH "Native FlatBuffers compiler")
    if(NOT EXISTS "${ENGINE_WEB_FLATC}")
        message(FATAL_ERROR "Web export requires the native ENGINE_WEB_FLATC executable")
    endif()
    add_custom_target(engine_export_web
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/platforms/web/export.py"
            --project "${ENGINE_EXPORT_PROJECT}" --builder "${ENGINE_WEB_ASSET_BUILDER}"
            --runtime-dir "${PROJECT_BINARY_DIR}" --product "${ENGINE_EXPORT_PRODUCT}"
            --configuration "$<LOWER_CASE:$<CONFIG>>" --manifest "${ENGINE_EXPORT_MANIFEST}"
            --flatc "${ENGINE_WEB_FLATC}" --node "${ENGINE_WEB_NODE}"
            --dependency-licenses "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share"
            --output "${PROJECT_SOURCE_DIR}/output/web/wasm32/$<LOWER_CASE:$<CONFIG>>/runtime/${ENGINE_EXPORT_PRODUCT}"
        DEPENDS engine_project_app USES_TERMINAL VERBATIM)
endif()

# 此目标只验证普通运行时截图能力，不增加运行端点或临时脚本接口。
if(ENGINE_BUILD_TESTS)
    engine_add_web_runtime(engine_web_render_test tests/runtime/render/web_render_test.cpp)
    set_target_properties(engine_web_render_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/web-render-test")
    target_compile_definitions(engine_web_render_test PRIVATE ENGINE_WEB_RENDER_TEST=1)
endif()
