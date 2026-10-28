# Dependencies used by the editor and standalone asset pipeline tool.
find_package(Taskflow CONFIG REQUIRED GLOBAL)
find_package(unofficial-sqlite3 CONFIG REQUIRED GLOBAL)
find_package(efsw CONFIG REQUIRED GLOBAL)
find_package(pxr CONFIG REQUIRED GLOBAL)
find_package(basisu CONFIG REQUIRED GLOBAL)
find_package(meshoptimizer CONFIG REQUIRED GLOBAL)
find_package(flatbuffers CONFIG REQUIRED GLOBAL)
find_package(tomlplusplus CONFIG REQUIRED GLOBAL)

# bgfx 的 shader、texture、geometry 编译器在构建主机上运行。
find_program(ENGINE_BGFX_SHADERC_EXECUTABLE NAMES shaderc REQUIRED)
find_program(ENGINE_BGFX_TEXTUREC_EXECUTABLE NAMES texturec REQUIRED)
find_program(ENGINE_BGFX_GEOMETRYC_EXECUTABLE NAMES geometryc REQUIRED)
