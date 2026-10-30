# 在 project() 初始化 vcpkg 前选择可选核心依赖。
option(ENGINE_ENABLE_TRACY "Compile the Tracy performance client" OFF)
list(REMOVE_ITEM VCPKG_MANIFEST_FEATURES tracy)
if(ENGINE_ENABLE_TRACY)
    if(NOT ENGINE_ENABLE_RUNTIME_DEPS)
        message(FATAL_ERROR "Tracy requires ENGINE_ENABLE_RUNTIME_DEPS")
    endif()
    list(APPEND VCPKG_MANIFEST_FEATURES tracy)
endif()
