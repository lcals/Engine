include_guard(GLOBAL)

function(_engine_architecture_targets directory output)
    get_property(
        _targets
        DIRECTORY "${directory}"
        PROPERTY BUILDSYSTEM_TARGETS)
    get_property(
        _imported
        DIRECTORY "${directory}"
        PROPERTY IMPORTED_TARGETS)
    list(APPEND _targets ${_imported})
    get_property(
        _children
        DIRECTORY "${directory}"
        PROPERTY SUBDIRECTORIES)
    foreach(_child IN LISTS _children)
        _engine_architecture_targets("${_child}" _nested)
        list(APPEND _targets ${_nested})
    endforeach()
    list(REMOVE_DUPLICATES _targets)
    set(${output}
        "${_targets}"
        PARENT_SCOPE)
endfunction()

function(_engine_architecture_quote input output)
    string(REPLACE "\\" "\\\\" _value "${input}")
    string(REPLACE "\"" "\\\"" _value "${_value}")
    string(REPLACE "\n" "\\n" _value "${_value}")
    string(REPLACE "\r" "\\r" _value "${_value}")
    string(REPLACE "\t" "\\t" _value "${_value}")
    set(${output}
        "\"${_value}\""
        PARENT_SCOPE)
endfunction()

# 在本仓库所有目标创建后调用。导出实际目标属性，不扫描 CMake 文本推测依赖。
# 每个配置的列表由生成器求值；Python 检查器读取 sources / links / interface_links。
function(engine_export_architecture output_directory)
    _engine_architecture_targets("${PROJECT_SOURCE_DIR}" _all_targets)
    set(_manifest "[]")
    set(_index 0)
    foreach(_target IN LISTS _all_targets)
        if(NOT _target MATCHES "^engine_" OR NOT TARGET "${_target}")
            continue()
        endif()
        get_target_property(_type ${_target} TYPE)
        if(_type STREQUAL "UTILITY")
            continue()
        endif()
        get_target_property(_source_directory ${_target} SOURCE_DIR)
        set(_record "{}")
        foreach(_field IN ITEMS name source_directory)
            if(_field STREQUAL "name")
                _engine_architecture_quote("${_target}" _quoted)
            else()
                _engine_architecture_quote("${_source_directory}" _quoted)
            endif()
            string(JSON _record SET "${_record}" "${_field}" "${_quoted}")
        endforeach()
        string(JSON _record SET "${_record}" aliases "{}")
        set(_sources "")
        foreach(_property IN ITEMS SOURCES INTERFACE_SOURCES)
            get_target_property(_value ${_target} ${_property})
            if(_value)
                list(APPEND _sources ${_value})
            endif()
        endforeach()
        file(
            GENERATE
            OUTPUT "${output_directory}/$<CONFIG>/${_target}.sources"
            CONTENT "$<JOIN:$<TARGET_GENEX_EVAL:${_target},${_sources}>,\n>\n" TARGET ${_target})
        foreach(_property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(_links ${_target} ${_property})
            if(NOT _links)
                set(_links "")
            endif()
            # 只提取候选名字后向 CMake 查询真实 TARGET / ALIASED_TARGET，不能猜别名。
            # 先移除 $<LINK_ONLY: 等表达式开头，避免把 LINK_ONLY:engine::name 当作别名。
            string(REGEX REPLACE "\\$<[A-Za-z0-9_]+:" " " _candidates "${_links}")
            string(REGEX MATCHALL "[A-Za-z_][A-Za-z0-9_.:+-]*" _names "${_candidates}")
            foreach(_name IN LISTS _names)
                if(TARGET "${_name}")
                    get_target_property(_canonical "${_name}" ALIASED_TARGET)
                    if(NOT _canonical)
                        set(_canonical "${_name}")
                    endif()
                    _engine_architecture_quote("${_canonical}" _quoted)
                    string(
                        JSON
                        _record
                        SET
                        "${_record}"
                        aliases
                        "${_name}"
                        "${_quoted}")
                endif()
            endforeach()
            # LINK_ONLY 是链接期保留项；file(GENERATE) 不具有链接求值上下文。
            # 将该包装转为恒真分支，保留其完整嵌套内容及其他生成器条件。
            string(REPLACE "$<LINK_ONLY:" "$<1:" _links "${_links}")
            if(_property STREQUAL "LINK_LIBRARIES")
                set(_suffix links)
            else()
                set(_suffix interface_links)
            endif()
            file(
                GENERATE
                OUTPUT "${output_directory}/$<CONFIG>/${_target}.${_suffix}"
                CONTENT "$<JOIN:$<TARGET_GENEX_EVAL:${_target},${_links}>,\n>\n" TARGET ${_target})
        endforeach()
        string(JSON _manifest SET "${_manifest}" ${_index} "${_record}")
        math(EXPR _index "${_index} + 1")
    endforeach()
    file(MAKE_DIRECTORY "${output_directory}")
    file(WRITE "${output_directory}/targets.json" "${_manifest}\n")
endfunction()
