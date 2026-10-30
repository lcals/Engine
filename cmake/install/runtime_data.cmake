# 宿主和测试只部署运行所需数据；不会重新准备上游依赖。
include("${CMAKE_CURRENT_LIST_DIR}/usd_data.cmake")

function(engine_copy_script_data target)
    if(TARGET engine_script AND NOT ANDROID)
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ENGINE_V8_ICU_DATA}"
                    "$<TARGET_FILE_DIR:${target}>/icudtl.dat"
            VERBATIM)
    endif()
endfunction()
foreach(_engine_host engine_app editor_app)
    if(TARGET ${_engine_host})
        engine_copy_script_data(${_engine_host})
    endif()
endforeach()

if(TARGET engine_usd_import)
    foreach(_engine_host editor_app asset_build_app mcp_app)
        if(TARGET ${_engine_host})
            engine_copy_usd_data(${_engine_host})
        endif()
    endforeach()
endif()
