# Dependencies used by the editor and standalone asset pipeline tool.
find_package(unofficial-sqlite3 CONFIG REQUIRED GLOBAL)
find_package(efsw CONFIG REQUIRED GLOBAL)
find_package(basisu CONFIG REQUIRED GLOBAL)
find_package(meshoptimizer CONFIG REQUIRED GLOBAL)
find_package(flatbuffers CONFIG REQUIRED GLOBAL)
find_package(tomlplusplus CONFIG REQUIRED GLOBAL)
find_package(SDL3 CONFIG REQUIRED GLOBAL)

if("editor/asset_import" IN_LIST ENGINE_SELECTED_PLUGINS)
    find_package(Freetype REQUIRED GLOBAL)
    set(_engine_movie_tool_hints "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/ffmpeg")
    if(VCPKG_INSTALLED_DIR AND NOT VCPKG_HOST_TRIPLET)
        file(GLOB _engine_movie_tool_directories LIST_DIRECTORIES TRUE
             "${VCPKG_INSTALLED_DIR}/*/tools/ffmpeg")
        list(APPEND _engine_movie_tool_hints ${_engine_movie_tool_directories})
    endif()
    function(engine_validate_movie_tool result candidate)
        execute_process(COMMAND "${candidate}" -version RESULT_VARIABLE _movie_status
                        OUTPUT_QUIET ERROR_QUIET TIMEOUT 10)
        if(NOT _movie_status EQUAL 0)
            set(${result} FALSE PARENT_SCOPE)
        endif()
    endfunction()
    foreach(_tool IN ITEMS FFMPEG FFPROBE)
        string(TOLOWER "${_tool}" _name)
        if(NOT ENGINE_${_tool}_EXECUTABLE)
            find_program(ENGINE_${_tool}_EXECUTABLE NAMES "${_name}"
                         HINTS ${_engine_movie_tool_hints} VALIDATOR engine_validate_movie_tool
                         NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
        endif()
        if(NOT ENGINE_${_tool}_EXECUTABLE OR NOT EXISTS "${ENGINE_${_tool}_EXECUTABLE}")
            message(FATAL_ERROR "Missing host ${_name}; prepare the movie-import vcpkg feature or explicitly configure ENGINE_${_tool}_EXECUTABLE")
        endif()
        execute_process(COMMAND "${ENGINE_${_tool}_EXECUTABLE}" -version RESULT_VARIABLE _status
                        OUTPUT_QUIET ERROR_QUIET TIMEOUT 10)
        if(NOT _status EQUAL 0)
            message(FATAL_ERROR "The selected host ${_name} is not executable: ${ENGINE_${_tool}_EXECUTABLE}")
        endif()
    endforeach()
    execute_process(COMMAND "${ENGINE_FFMPEG_EXECUTABLE}" -hide_banner -encoders
                    RESULT_VARIABLE _movie_encoders_status OUTPUT_VARIABLE _movie_encoders
                    ERROR_QUIET TIMEOUT 10)
    if(NOT _movie_encoders_status EQUAL 0 OR NOT _movie_encoders MATCHES "libvpx-vp9")
        message(FATAL_ERROR "The selected host ffmpeg must provide libvpx-vp9")
    endif()
endif()

# bgfx 的 shader、texture、geometry 编译器在构建主机上运行。
find_program(ENGINE_BGFX_SHADERC_EXECUTABLE NAMES shaderc REQUIRED)
find_program(ENGINE_BGFX_TEXTUREC_EXECUTABLE NAMES texturec REQUIRED)
find_program(ENGINE_BGFX_GEOMETRYC_EXECUTABLE NAMES geometryc REQUIRED)
