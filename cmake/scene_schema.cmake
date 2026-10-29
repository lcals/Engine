# FlatBuffers 编译器始终在宿主运行，生成的类型在目标平台编译。
function(engine_add_scene_schema)
    if(TARGET engine_scene_data)
        return()
    endif()
    set(ENGINE_FLATC_EXECUTABLE "" CACHE FILEPATH "Host FlatBuffers compiler executable")
    if(NOT ENGINE_FLATC_EXECUTABLE)
        find_program(_engine_host_flatc NAMES flatc
            HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/flatbuffers"
            NO_CMAKE_FIND_ROOT_PATH)
        if(_engine_host_flatc)
            set(ENGINE_FLATC_EXECUTABLE "${_engine_host_flatc}" CACHE FILEPATH
                "Host FlatBuffers compiler executable" FORCE)
        elseif(NOT CMAKE_CROSSCOMPILING AND TARGET flatbuffers::flatc)
            foreach(_configuration RELEASE RELWITHDEBINFO DEBUG NOCONFIG)
                get_target_property(_flatc flatbuffers::flatc IMPORTED_LOCATION_${_configuration})
                if(_flatc AND EXISTS "${_flatc}")
                    break()
                endif()
            endforeach()
            if(NOT _flatc OR NOT EXISTS "${_flatc}")
                get_target_property(_flatc flatbuffers::flatc IMPORTED_LOCATION)
            endif()
            if(_flatc AND EXISTS "${_flatc}")
                set(ENGINE_FLATC_EXECUTABLE "${_flatc}" CACHE FILEPATH
                    "Host FlatBuffers compiler executable" FORCE)
            endif()
        endif()
    endif()
    if(NOT ENGINE_FLATC_EXECUTABLE OR NOT EXISTS "${ENGINE_FLATC_EXECUTABLE}")
        message(FATAL_ERROR "Scene data requires host flatc. Install host flatbuffers or set ENGINE_FLATC_EXECUTABLE.")
    endif()
    execute_process(COMMAND "${ENGINE_FLATC_EXECUTABLE}" --version
        RESULT_VARIABLE _flatc_result OUTPUT_QUIET ERROR_QUIET TIMEOUT 10)
    if(NOT _flatc_result EQUAL 0)
        message(FATAL_ERROR "ENGINE_FLATC_EXECUTABLE cannot run on this host: ${ENGINE_FLATC_EXECUTABLE}")
    endif()

    set(_schema "${PROJECT_SOURCE_DIR}/schemas/runtime/scene.fbs")
    set(_include_root "${PROJECT_BINARY_DIR}/generated/include/engine")
    set(_cpp_dir "${_include_root}/runtime/schema")
    set(_ts_dir "${PROJECT_BINARY_DIR}/generated/typescript")
    set(_cpp_header "${_cpp_dir}/scene_generated.h")
    set(_ts_outputs "${_ts_dir}/engine/schema.ts")
    foreach(_name vec2 vec3 vec4 color mesh-kind transform-data camera-data light-data
            float-value vec2-value vec3-value vec4-value material-value
            shader-parameter shader-data material-parameter material-data
            mesh-renderer-data component-payload component-data node-info node-data scene-info
            scene-data)
        list(APPEND _ts_outputs "${_ts_dir}/engine/schema/${_name}.ts")
    endforeach()
    add_custom_command(OUTPUT "${_cpp_header}" ${_ts_outputs}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_cpp_dir}" "${_ts_dir}"
        COMMAND "${ENGINE_FLATC_EXECUTABLE}" --cpp --gen-object-api --gen-mutable --gen-compare
            --scoped-enums -o "${_cpp_dir}" "${_schema}"
        COMMAND "${ENGINE_FLATC_EXECUTABLE}" --ts --gen-object-api --ts-entry-points
            -o "${_ts_dir}" "${_schema}"
        DEPENDS "${_schema}" "${ENGINE_FLATC_EXECUTABLE}"
        COMMENT "Generating shared C++ and TypeScript scene data from FlatBuffers"
        VERBATIM)
    add_custom_target(engine_scene_schema DEPENDS "${_cpp_header}" ${_ts_outputs})
    add_library(engine_scene_data INTERFACE)
    add_library(engine::scene_data ALIAS engine_scene_data)
    add_dependencies(engine_scene_data engine_scene_schema)
    target_include_directories(engine_scene_data INTERFACE "$<BUILD_INTERFACE:${_include_root}>")
    target_link_libraries(engine_scene_data INTERFACE flatbuffers::flatbuffers)
endfunction()
