find_package(pxr CONFIG REQUIRED GLOBAL)

add_library(engine_usd_sdk INTERFACE)
get_target_property(_engine_usd_type usd TYPE)
if(WIN32 AND _engine_usd_type STREQUAL "STATIC_LIBRARY")
    # 插件的注册代码已进入静态归档，宿主显式链接所有已启用的 USD 库。
    target_link_libraries(engine_usd_sdk INTERFACE ${PXR_LIBRARIES})
else()
    target_link_libraries(engine_usd_sdk INTERFACE usdGeom)
endif()
