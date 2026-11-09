include_guard(GLOBAL)

function(engine_add_editor_schema)
    set(_names)
    if("editor/modeling" IN_LIST ENGINE_SELECTED_PLUGINS)
        list(APPEND _names modeling)
    endif()
    if("editor/destruction" IN_LIST ENGINE_SELECTED_PLUGINS)
        list(APPEND _names destruction_authoring)
    endif()
    if(NOT _names)
        return()
    endif()

    set(_schema_dir "${PROJECT_SOURCE_DIR}/include/schemas/editor")
    set(_runtime_schema_dir "${PROJECT_SOURCE_DIR}/include/schemas/runtime")
    set(_include_root "${PROJECT_BINARY_DIR}/generated/include/engine")
    set(_cpp_dir "${_include_root}/editor/schema")
    set(_ts_dir "${PROJECT_BINARY_DIR}/generated/editor/typescript")
    set(_ts_aggregate "${PROJECT_BINARY_DIR}/generated/editor/typescript-aggregate")
    set(_ts_schema "${PROJECT_BINARY_DIR}/generated/editor/all.fbs")
    set(_stamp "${PROJECT_BINARY_DIR}/generated/editor/schema.stamp")
    set(_ts_barrel "${_ts_dir}/engine/schema.ts")
    set(_schemas)
    set(_cpp_headers)
    set(_ts_content "")
    foreach(_name IN LISTS _names)
        list(APPEND _schemas "${_schema_dir}/${_name}.fbs")
        list(APPEND _cpp_headers "${_cpp_dir}/${_name}_generated.h")
        string(APPEND _ts_content "include \"${_name}.fbs\";\n")
    endforeach()
    string(APPEND _ts_content "namespace engine.schema;\n")
    file(CONFIGURE OUTPUT "${_ts_schema}" CONTENT "${_ts_content}" @ONLY)

    add_custom_command(
        OUTPUT ${_cpp_headers} "${_stamp}" "${_ts_barrel}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_cpp_dir}" "${_ts_dir}"
        COMMAND "${ENGINE_FLATC_EXECUTABLE}" --cpp --gen-object-api --gen-mutable --gen-compare
                --scoped-enums --include-prefix runtime/schema/ -I "${_runtime_schema_dir}"
                -I "${_schema_dir}" -o "${_cpp_dir}" ${_schemas}
        COMMAND "${ENGINE_FLATC_EXECUTABLE}" --ts --gen-all --gen-object-api --ts-entry-points
                -I "${_runtime_schema_dir}" -I "${_schema_dir}" -o "${_ts_aggregate}" "${_ts_schema}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "${_ts_aggregate}" "${_ts_dir}"
        COMMAND "${ENGINE_FLATC_EXECUTABLE}" --ts --gen-object-api --ts-omit-entrypoint
                -I "${_runtime_schema_dir}" -I "${_schema_dir}" -o "${_ts_dir}" ${_schemas}
        COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
        DEPENDS ${_schemas} "${_runtime_schema_dir}/common.fbs" "${_ts_schema}"
                "${ENGINE_FLATC_EXECUTABLE}"
        COMMENT "Generating editor C++ and TypeScript data from FlatBuffers"
        VERBATIM)
    add_custom_target(engine_editor_schema DEPENDS ${_cpp_headers} "${_stamp}" "${_ts_barrel}")
    add_dependencies(engine_editor_schema engine_scene_schema)
    add_library(engine_editor_data INTERFACE ${_schemas})
    add_library(engine::editor_data ALIAS engine_editor_data)
    add_dependencies(engine_editor_data engine_editor_schema)
    target_include_directories(engine_editor_data INTERFACE "$<BUILD_INTERFACE:${_include_root}>")
    target_link_libraries(engine_editor_data INTERFACE engine::scene_data)
endfunction()
