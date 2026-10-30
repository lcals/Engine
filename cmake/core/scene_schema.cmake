# FlatBuffers 编译器始终在宿主运行，生成的类型在目标平台编译。
function(engine_validate_host_flatc result candidate)
    execute_process(
        COMMAND "${candidate}" --version
        RESULT_VARIABLE _result
        OUTPUT_QUIET ERROR_QUIET
        TIMEOUT 10)
    if(NOT _result EQUAL 0)
        set(${result}
            FALSE
            PARENT_SCOPE)
    endif()
endfunction()

function(engine_add_scene_schema)
    if(TARGET engine_scene_data)
        return()
    endif()
    set(ENGINE_FLATC_EXECUTABLE
        ""
        CACHE FILEPATH "Host FlatBuffers compiler executable")
    if(NOT ENGINE_FLATC_EXECUTABLE)
        set(_flatc_hints "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/flatbuffers")
        # vcpkg 自动选择宿主 triplet 时不会向 CMake 导出该变量；从已安装工具中查找。
        # 逐个执行 --version，跳过不能在宿主运行的目标平台工具。
        if(VCPKG_INSTALLED_DIR AND NOT VCPKG_HOST_TRIPLET)
            file(
                GLOB _flatc_installed_tools
                LIST_DIRECTORIES TRUE
                "${VCPKG_INSTALLED_DIR}/*/tools/flatbuffers")
            list(APPEND _flatc_hints ${_flatc_installed_tools})
        endif()
        unset(_engine_host_flatc CACHE)
        unset(_engine_host_flatc)
        find_program(
            _engine_host_flatc
            NAMES flatc
            HINTS ${_flatc_hints} VALIDATOR engine_validate_host_flatc NO_CACHE
            NO_CMAKE_FIND_ROOT_PATH)
        if(_engine_host_flatc)
            set(ENGINE_FLATC_EXECUTABLE
                "${_engine_host_flatc}"
                CACHE FILEPATH "Host FlatBuffers compiler executable" FORCE)
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
                set(ENGINE_FLATC_EXECUTABLE
                    "${_flatc}"
                    CACHE FILEPATH "Host FlatBuffers compiler executable" FORCE)
            endif()
        endif()
    endif()
    if(NOT ENGINE_FLATC_EXECUTABLE OR NOT EXISTS "${ENGINE_FLATC_EXECUTABLE}")
        message(
            FATAL_ERROR
                "Scene data requires host flatc. Install host flatbuffers or set ENGINE_FLATC_EXECUTABLE."
        )
    endif()
    execute_process(
        COMMAND "${ENGINE_FLATC_EXECUTABLE}" --version
        RESULT_VARIABLE _flatc_result
        OUTPUT_QUIET ERROR_QUIET
        TIMEOUT 10)
    if(NOT _flatc_result EQUAL 0)
        message(
            FATAL_ERROR
                "ENGINE_FLATC_EXECUTABLE cannot run on this host: ${ENGINE_FLATC_EXECUTABLE}")
    endif()

    set(_schema_dir "${PROJECT_SOURCE_DIR}/include/schemas/runtime")
    set(_include_root "${PROJECT_BINARY_DIR}/generated/include/engine")
    set(_cpp_dir "${_include_root}/runtime/schema")
    set(_ts_dir "${PROJECT_BINARY_DIR}/generated/typescript")
    set(_schemas)
    set(_cpp_headers)
    foreach(
        _name
        common
        scene
        mesh
        model
        skeleton
        animation
        modeling
        shader
        material
        texture
        audio
        font
        ui
        asset_catalog
        resource_header)
        list(APPEND _schemas "${_schema_dir}/${_name}.fbs")
        list(APPEND _cpp_headers "${_cpp_dir}/${_name}_generated.h")
    endforeach()
    # 聚合入口只用于生成统一 TS 导出，避免同 namespace 的独立 schema 相互覆盖 barrel。
    set(_ts_schema "${PROJECT_BINARY_DIR}/generated/all.fbs")
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
    file(
        WRITE "${_ts_schema}"
        "include \"common.fbs\";\ninclude \"scene.fbs\";\ninclude \"mesh.fbs\";\ninclude \"model.fbs\";\ninclude \"skeleton.fbs\";\ninclude \"animation.fbs\";\ninclude \"modeling.fbs\";\ninclude \"shader.fbs\";\ninclude \"material.fbs\";\ninclude \"texture.fbs\";\ninclude \"audio.fbs\";\ninclude \"font.fbs\";\ninclude \"ui.fbs\";\ninclude \"asset_catalog.fbs\";\ninclude \"resource_header.fbs\";\nnamespace engine.schema;\nroot_type SceneData;\n"
    )
    set(_stamp "${PROJECT_BINARY_DIR}/generated/schema.stamp")
    set(_ts_aggregate "${PROJECT_BINARY_DIR}/generated/typescript-aggregate")
    set(_ts_barrel "${_ts_dir}/engine/schema.ts")
    add_custom_command(
        OUTPUT ${_cpp_headers} "${_stamp}" "${_ts_barrel}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_cpp_dir}" "${_ts_dir}"
        COMMAND "${ENGINE_FLATC_EXECUTABLE}" --cpp --gen-object-api --gen-mutable --gen-compare
                --scoped-enums -I "${_schema_dir}" -o "${_cpp_dir}" ${_schemas}
        COMMAND "${ENGINE_FLATC_EXECUTABLE}" --ts --gen-all --gen-object-api --ts-entry-points -I
                "${_schema_dir}" -o "${_ts_aggregate}" "${_ts_schema}"
        COMMAND "${ENGINE_FLATC_EXECUTABLE}" --ts --gen-object-api --ts-entry-points -I
                "${_schema_dir}" -o "${_ts_dir}" ${_schemas}
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_ts_aggregate}/engine/schema.ts"
                "${_ts_barrel}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
        DEPENDS ${_schemas} "${ENGINE_FLATC_EXECUTABLE}"
        COMMENT "Generating shared C++ and TypeScript asset data from FlatBuffers"
        VERBATIM)
    add_custom_target(engine_scene_schema DEPENDS ${_cpp_headers} "${_stamp}" "${_ts_barrel}")
    add_library(engine_scene_data INTERFACE)
    add_library(engine::scene_data ALIAS engine_scene_data)
    add_dependencies(engine_scene_data engine_scene_schema)
    target_include_directories(engine_scene_data INTERFACE "$<BUILD_INTERFACE:${_include_root}>")
    target_link_libraries(engine_scene_data INTERFACE flatbuffers::flatbuffers)
endfunction()
