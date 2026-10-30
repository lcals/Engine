include_guard(GLOBAL)

if("runtime/script" IN_LIST ENGINE_SELECTED_PLUGINS AND ENGINE_SCRIPT_BACKEND STREQUAL "quickjs")
    find_package(qjs CONFIG REQUIRED GLOBAL)
endif()

# 统一的准备入口：QuickJS 由 vcpkg 在 configure 阶段构建，无额外准备步骤。
add_custom_target(engine_script_prepare)
if(TARGET engine_v8_prepare)
    add_dependencies(engine_script_prepare engine_v8_prepare)
endif()
