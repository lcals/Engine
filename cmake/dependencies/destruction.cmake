# NVIDIA Blast 5.1: official CPU graph/damage and editor-only fracture authoring.
include_guard(GLOBAL)
set(_blast "${PROJECT_SOURCE_DIR}/third_party/blast")
if(NOT EXISTS "${_blast}/source/sdk/lowlevel/NvBlastActor.cpp")
    message(FATAL_ERROR "Blast is missing; run git submodule update --init third_party/blast")
endif()

# Stage the Foundation headers and affected authoring files. The source checkout stays clean;
# hashes prevent repeat copies/patching from invalidating incremental builds.
find_package(Git REQUIRED)
set(_blast_patch "${PROJECT_SOURCE_DIR}/third_party/patches/blast/portable-authoring.patch")
set(_blast_staged "${CMAKE_CURRENT_BINARY_DIR}/source/blast")
file(
    GLOB_RECURSE _blast_stage_files CONFIGURE_DEPENDS
    RELATIVE "${_blast}"
    "${_blast}/source/shared/NsFoundation/include/*.h")
list(APPEND _blast_stage_files "source/sdk/extensions/authoring/NvBlastExtApexSharedParts.cpp"
     "source/sdk/extensions/authoring/NvBlastExtAuthoringBondGeneratorImpl.h"
     "source/sdk/extensions/authoring/VHACD/src/FloatMath.cpp")
set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${_blast_patch}")
file(SHA256 "${_blast_patch}" _blast_fingerprint)
foreach(_relative IN LISTS _blast_stage_files)
    file(SHA256 "${_blast}/${_relative}" _hash)
    string(APPEND _blast_fingerprint "${_hash}\n")
endforeach()
set(_blast_stamp "${_blast_staged}/.engine-patch-inputs")
set(_blast_existing "")
if(EXISTS "${_blast_stamp}")
    file(READ "${_blast_stamp}" _blast_existing)
endif()
foreach(_relative IN LISTS _blast_stage_files)
    if(NOT EXISTS "${_blast_staged}/${_relative}")
        set(_blast_existing "")
    endif()
endforeach()
if(NOT _blast_existing STREQUAL _blast_fingerprint)
    foreach(_relative IN LISTS _blast_stage_files)
        configure_file("${_blast}/${_relative}" "${_blast_staged}/${_relative}" COPYONLY)
    endforeach()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --no-index "${_blast_patch}"
        WORKING_DIRECTORY "${_blast_staged}"
        RESULT_VARIABLE _result
        ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Cannot prepare Blast portability patch: ${_error}")
    endif()
    file(WRITE "${_blast_stamp}" "${_blast_fingerprint}")
endif()

# Source groups match upstream premake5.lua. PhysX, CUDA, stress solver and toolkit are unused.
file(GLOB _blast_lowlevel CONFIGURE_DEPENDS "${_blast}/source/sdk/lowlevel/*.cpp")
file(GLOB _blast_common CONFIGURE_DEPENDS "${_blast}/source/sdk/common/*.cpp")
file(GLOB _blast_globals CONFIGURE_DEPENDS "${_blast}/source/sdk/globals/*.cpp")
file(GLOB _blast_shaders CONFIGURE_DEPENDS "${_blast}/source/sdk/extensions/shaders/*.cpp")
add_library(engine_blast STATIC ${_blast_lowlevel} ${_blast_common} ${_blast_globals}
                                ${_blast_shaders})
add_library(engine::blast ALIAS engine_blast)
target_include_directories(
    engine_blast SYSTEM
    PUBLIC "${_blast}/include" "${_blast}/include/lowlevel" "${_blast}/include/globals"
           "${_blast}/include/shared/NvFoundation" "${_blast}/include/extensions/shaders"
    PRIVATE "${_blast}/source/sdk/common" "${_blast}/source/sdk/lowlevel"
            "${_blast_staged}/source/shared/NsFoundation/include")
target_compile_features(engine_blast PUBLIC cxx_std_20)
# Upstream defines this empty outside MSVC despite exposing C APIs. Keep declarations consistent.
target_compile_definitions(engine_blast PUBLIC "NV_C_EXPORT=extern \"C\"")
target_compile_definitions(engine_blast PUBLIC "$<$<CONFIG:Debug>:_DEBUG>")
set_target_properties(engine_blast PROPERTIES POSITION_INDEPENDENT_CODE ON)
engine_apply_compiler_cache(engine_blast)
if(MSVC)
    target_compile_options(engine_blast PRIVATE /EHs-c-)
else()
    target_compile_options(engine_blast PRIVATE -fno-exceptions -fno-rtti)
endif()

if("editor/destruction" IN_LIST ENGINE_SELECTED_PLUGINS)
    find_package(Boost CONFIG REQUIRED COMPONENTS multiprecision)
    file(GLOB _blast_authoring CONFIGURE_DEPENDS "${_blast}/source/sdk/extensions/authoring/*.cpp")
    file(GLOB _blast_authoring_common CONFIGURE_DEPENDS
         "${_blast}/source/sdk/extensions/authoringCommon/*.cpp")
    file(GLOB _blast_vhacd CONFIGURE_DEPENDS
         "${_blast}/source/sdk/extensions/authoring/VHACD/src/*.cpp")
    list(REMOVE_ITEM _blast_authoring
         "${_blast}/source/sdk/extensions/authoring/NvBlastExtApexSharedParts.cpp")
    list(APPEND _blast_authoring
         "${_blast_staged}/source/sdk/extensions/authoring/NvBlastExtApexSharedParts.cpp")
    list(REMOVE_ITEM _blast_vhacd
         "${_blast}/source/sdk/extensions/authoring/VHACD/src/FloatMath.cpp")
    list(APPEND _blast_vhacd
         "${_blast_staged}/source/sdk/extensions/authoring/VHACD/src/FloatMath.cpp")
    add_library(engine_blast_authoring STATIC ${_blast_authoring} ${_blast_authoring_common}
                                              ${_blast_vhacd})
    add_library(engine::blast_authoring ALIAS engine_blast_authoring)
    target_include_directories(
        engine_blast_authoring SYSTEM
        PUBLIC "${_blast}/include/extensions/authoring"
               "${_blast}/include/extensions/authoringCommon"
        PRIVATE "${_blast_staged}/source/sdk/extensions/authoring"
                "${_blast}/include/extensions/assetutils"
                "${_blast}/source/sdk/extensions/authoring"
                "${_blast}/source/sdk/extensions/authoringCommon"
                "${_blast}/source/sdk/extensions/authoring/VHACD/inc"
                "${_blast}/source/sdk/extensions/authoring/VHACD/src"
                "${_blast}/source/sdk/extensions/authoring/VHACD/public"
                "${_blast}/source/sdk/common"
                "${_blast_staged}/source/shared/NsFoundation/include")
    target_link_libraries(
        engine_blast_authoring
        PUBLIC engine::blast
        PRIVATE Boost::multiprecision)
    target_compile_features(engine_blast_authoring PUBLIC cxx_std_20)
    set_target_properties(engine_blast_authoring PROPERTIES POSITION_INDEPENDENT_CODE ON)
    engine_apply_compiler_cache(engine_blast_authoring)
endif()
