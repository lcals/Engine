#include "plugins/editor/destruction/destruction_files.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/project_lock.hpp"
#include "editor/commands/editor_commands.hpp"
#include "plugins/editor/destruction/destruction_authoring.hpp"
#include "plugins/editor/destruction/destruction_source.hpp"

#include <nlohmann/json.hpp>

#include <memory>

namespace engine::plugins::destruction
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

bool create_asset(const fs::path& project, const Json& arguments, Json& result, std::string& error)
{
    if (!arguments.is_object() || arguments.size() != 2 || !arguments.contains("path") ||
        !arguments["path"].is_string() || !arguments.contains("source") ||
        !arguments["source"].is_string())
    {
        error = "Destruction creation requires path and source TOML strings";
        return false;
    }
    const auto& relative = arguments["path"].get_ref<const std::string&>();
    const auto& source = arguments["source"].get_ref<const std::string&>();
    schema::DestructionGeneratorDataT generator;
    schema::DestructionAssetDataT volume;
    fs::path destination;
    if (!destruction_file_path(project, "assets", relative, ".destruction", destination, error) ||
        !read_destruction_source(source, generator, error) ||
        !DestructionAuthoring::generate(generator, volume, error))
    {
        return false;
    }
    editor::asset_pipeline::ProjectLock lock;
    if (!lock.acquire(project / "cache/.asset-build.lock", error))
    {
        return false;
    }
    std::string guid;
    if (!editor::asset_pipeline::create_source_with_meta(destination, source, "destruction", guid,
                                                         error))
    {
        return false;
    }
    result = {{"path", relative},
              {"guid", guid},
              {"chunks", volume.chunks.size()},
              {"bonds", volume.bonds.size()},
              {"importRequired", true}};
    return true;
}

} // namespace

bool destruction_file_path(const std::filesystem::path& project, std::string_view directory,
                           std::string_view relative, std::string_view extension,
                           std::filesystem::path& output, std::string& error)
{
    return editor::asset_pipeline::project_file_path(project, directory, relative, extension,
                                                     output, error);
}

bool register_destruction_asset_commands(editor::EditorCommands& commands,
                                         const std::filesystem::path& project)
{
    return commands.register_command(
        "create_destruction_asset", false,
        [project](const Json& arguments, Json& result, std::unique_ptr<editor::EditorEdit>&,
                  std::string& error) { return create_asset(project, arguments, result, error); });
}

} // namespace engine::plugins::destruction
