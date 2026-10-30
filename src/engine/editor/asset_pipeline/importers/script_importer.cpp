#include "editor/asset_pipeline/importers/script_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/editor_tools.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "runtime/utils/path_utils.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace engine::editor::asset_pipeline
{
schema::AssetKind ScriptImporter::kind() const noexcept
{
    return schema::AssetKind::script;
}

bool ScriptImporter::matches(const std::filesystem::path& source) const
{
    return source_extension(source) == ".ts" &&
           !PathUtils::to_utf8(source.filename()).ends_with(".d.ts");
}

std::string ScriptImporter::module_path(const std::filesystem::path& relative) const
{
    auto module = relative;
    module.replace_extension(".js");
    return generic_path(module);
}

bool ScriptImporter::inspect_batch(const ImportContext& context, std::string& fingerprint,
                                   std::string& error)
{
    fingerprint.clear();
    std::string key;
    std::filesystem::path node;
    std::filesystem::path builder;
    if (!editor_tool_path("node", ENGINE_NODE_EXECUTABLE, node, error) ||
        !editor_tool_data_path("scripts/build_scripts.mjs", ENGINE_SCRIPT_BUILDER, builder,
                               error) ||
        !run_process({PathUtils::to_utf8(node), "--version"}, error, &key))
    {
        if (error.empty())
        {
            error = "TypeScript import requires Node.js";
        }
        return false;
    }
    for (const auto& path :
         {context.project / "settings" / "package.json",
          context.project / "settings" / "package-lock.json",
          context.project / "settings" / "tsconfig.json",
          context.project / "settings" / "node_modules" / "typescript" / "package.json", builder,
          builder.parent_path().parent_path() / "include" / "engine.d.ts"})
    {
        std::string hash;
        if (!hash_optional_file(path, hash, error))
        {
            return false;
        }
        key += ":" + hash;
    }
    std::string input_snapshot;
    if (!run_process({PathUtils::to_utf8(node), PathUtils::to_utf8(builder), "--project",
                      PathUtils::to_utf8(context.project), "--inputs"},
                     error, &input_snapshot))
    {
        return false;
    }
    key += ":" + hash_text(input_snapshot);
    // TS全工程输出共享同一输入快照；声明文件和meta变化也使批次失效。
    for (const auto* asset : context.sources)
    {
        if (asset == nullptr)
        {
            error = "null source in import batch";
            return false;
        }
        if (asset->source.extension() == ".ts")
        {
            std::string source_hash;
            std::string meta_hash;
            auto meta = asset->source;
            meta += ".meta";
            if (!file_hash(asset->source, source_hash, error) || !file_hash(meta, meta_hash, error))
            {
                return false;
            }
            key += ":" + asset->path + ":" + source_hash + ":" + meta_hash;
        }
    }
    const auto data = nlohmann::json::parse(input_snapshot, nullptr, false);
    if (!data.is_object() || !data.contains("modules") || !data["modules"].is_array())
    {
        error = "script compiler returned invalid runtime module dependencies";
        return false;
    }
    std::map<std::string, std::vector<Guid>> modules;
    for (const auto& module : data["modules"])
    {
        if (!module.is_object() || !module.contains("source") || !module["source"].is_string() ||
            !module.contains("dependencies") || !module["dependencies"].is_array())
        {
            error = "script compiler returned an invalid module";
            return false;
        }
        const auto path = "project/" + module["source"].get<std::string>();
        std::vector<Guid> dependencies;
        for (const auto& dependency : module["dependencies"])
        {
            Guid guid;
            if (!dependency.is_string() ||
                !context.identities.path_to_guid("project/" + dependency.get<std::string>(), guid))
            {
                error = "script runtime import has no project resource identity: " + path;
                return false;
            }
            dependencies.push_back(std::move(guid));
        }
        std::sort(dependencies.begin(), dependencies.end());
        dependencies.erase(std::unique(dependencies.begin(), dependencies.end()),
                           dependencies.end());
        if (!modules.emplace(path, std::move(dependencies)).second)
        {
            error = "script compiler returned duplicate modules";
            return false;
        }
    }
    modules_ = std::move(modules);
    fingerprint = std::move(key);
    return true;
}

bool ScriptImporter::scan_dependencies(ImportAsset& asset, const ImportContext&,
                                       std::string& error) const
{
    asset.build_dependencies.clear();
    asset.runtime_dependencies.clear();
    const auto found = modules_.find(asset.path);
    if (found == modules_.end())
    {
        error = "script source is excluded from TypeScript emission: " + asset.path;
        return false;
    }
    asset.runtime_dependencies = found->second;
    return true;
}

bool ScriptImporter::prepare_batch(const ImportContext& context, TaskDescription& task,
                                   std::string& error) const
{
    std::filesystem::path node;
    std::filesystem::path builder;
    if (!editor_tool_path("node", ENGINE_NODE_EXECUTABLE, node, error) ||
        !editor_tool_data_path("scripts/build_scripts.mjs", ENGINE_SCRIPT_BUILDER, builder, error))
    {
        return false;
    }
    const auto project = PathUtils::to_utf8(context.project);
    const auto output = PathUtils::to_utf8(context.output.parent_path() / "scripts");
    task = {
        "Compile project TypeScript",
        [project, output, node = PathUtils::to_utf8(node), builder = PathUtils::to_utf8(builder),
         &error]()
        { return run_process({node, builder, "--project", project, "--output", output}, error); },
        {}};
    return true;
}

bool ScriptImporter::convert(const ImportAsset& asset, const ImportContext& context,
                             std::string& error) const
{
    const auto relative = module_path(asset.source.lexically_relative(context.assets));
    std::vector<std::byte> bytes;
    return read_bytes(context.output.parent_path() / "scripts" / PathUtils::from_utf8(relative),
                      bytes, error) &&
           write_bytes(context.output / asset.guid, bytes, error);
}
} // namespace engine::editor::asset_pipeline
