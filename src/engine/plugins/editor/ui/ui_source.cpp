#include "plugins/editor/ui/ui_editor_state.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/import_context.hpp"
#include "editor/asset_pipeline/project_lock.hpp"
#include "plugins/editor/asset_import/ui_importer.hpp"
#include "runtime/resource/builtin_resources.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <toml++/toml.hpp>

#include <filesystem>
#include <map>
#include <sstream>
#include <system_error>

namespace engine::plugins::ui
{
namespace
{
namespace fs = std::filesystem;
namespace pipeline = editor::asset_pipeline;
constexpr std::size_t max_source = 1024U * 1024U;

bool safe_path(const fs::path& project, std::string_view logical, fs::path& output,
               std::string& error)
{
    output.clear();
    if (!GuidMap::valid_path(logical) || logical.find(":") != std::string_view::npos ||
        logical.find('\\') != std::string_view::npos)
    {
        error = "UI source requires a relative assets path";
        return false;
    }
    const auto relative = PathUtils::from_utf8(logical);
    if (relative.is_absolute() || relative.extension() != ".rml")
    {
        error = "UI source must have the .rml extension";
        return false;
    }
    std::error_code ec;
    const auto assets = fs::canonical(project / "assets", ec);
    if (ec)
    {
        error = "Project assets directory is unavailable";
        return false;
    }
    auto current = assets;
    for (const auto& segment : relative)
    {
        if (segment == ".." || segment == "." || segment.empty())
        {
            error = "UI source path cannot contain traversal";
            return false;
        }
        current /= segment;
        const auto status = fs::symlink_status(current, ec);
        if (ec && ec != std::errc::no_such_file_or_directory)
        {
            error = "Cannot inspect UI source path";
            return false;
        }
        ec.clear();
        if (fs::is_symlink(status))
        {
            error = "UI source path cannot contain symlinks";
            return false;
        }
    }
    auto meta = current;
    meta += ".meta";
    const auto status = fs::symlink_status(meta, ec);
    if (ec && ec != std::errc::no_such_file_or_directory)
    {
        error = "Cannot inspect UI metadata";
        return false;
    }
    if (fs::is_symlink(status))
    {
        error = "UI metadata cannot be a symlink";
        return false;
    }
    output = current;
    return true;
}
bool read_optional(const fs::path& path, std::string& output, bool& exists, std::string& error)
{
    output.clear();
    std::error_code ec;
    exists = fs::exists(path, ec);
    if (ec)
    {
        error = "Cannot inspect UI file";
        return false;
    }
    if (!exists)
    {
        return true;
    }
    if (!fs::is_regular_file(path, ec) || ec)
    {
        error = "UI source and metadata must be regular files";
        return false;
    }
    const auto bytes = fs::file_size(path, ec);
    if (ec || bytes > max_source)
    {
        error = "UI file exceeds 1 MiB or cannot be read";
        return false;
    }
    std::vector<std::byte> content;
    if (!pipeline::read_bytes(path, content, error))
    {
        return false;
    }
    output.assign(reinterpret_cast<const char*>(content.data()), content.size());
    return true;
}
bool read_pair(const fs::path& path, std::string& source, std::string& metadata, bool& exists,
               std::string& error)
{
    auto meta_path = path;
    meta_path += ".meta";
    bool has_meta = false;
    if (!read_optional(path, source, exists, error) ||
        !read_optional(meta_path, metadata, has_meta, error))
    {
        return false;
    }
    if (exists != has_meta)
    {
        error = "UI source and adjacent metadata must both exist or both be absent";
        return false;
    }
    return true;
}
Json bindings(const toml::table& meta)
{
    Json result{{"fonts", Json::array()}, {"textures", Json::array()}};
    if (const auto* fonts = meta["ui"]["fonts"].as_array())
    {
        for (const auto& font : *fonts)
        {
            if (const auto* item = font.as_table())
            {
                result["fonts"].push_back({{"family", (*item)["family"].value_or(std::string{})},
                                           {"asset", (*item)["asset"].value_or(std::string{})},
                                           {"shader", (*item)["shader"].value_or(
                                                          std::string("builtin://shaders/font"))}});
            }
        }
    }
    if (const auto* textures = meta["ui"]["textures"].as_array())
    {
        for (const auto& texture : *textures)
        {
            if (auto value = texture.value<std::string>())
            {
                result["textures"].push_back(*value);
            }
        }
    }
    return result;
}
bool read_meta(std::string_view text, toml::table& output, std::string& error)
{
    auto parsed = toml::parse(text);
    if (!parsed)
    {
        error = "UI metadata is not valid TOML";
        return false;
    }
    output = std::move(parsed).table();
    if (!GuidMap::valid_guid(output["guid"].value_or(std::string{})) ||
        output["importer"].value_or(std::string{}) != "ui")
    {
        error = "UI metadata requires its existing GUID and ui importer";
        return false;
    }
    return true;
}
bool save_bindings(const Json& args, toml::table& meta, std::string& error)
{
    if (!meta.contains("ui"))
    {
        meta.insert("ui", toml::table{});
    }
    auto* table = meta["ui"].as_table();
    if (!table)
    {
        error = "UI metadata requires a ui table";
        return false;
    }
    if (args.contains("fonts"))
    {
        const auto& fonts = args["fonts"];
        if (!fonts.is_array() || fonts.empty() || fonts.size() > 64)
        {
            error = "fonts requires 1..64 font bindings";
            return false;
        }
        toml::array items;
        for (const auto& font : fonts)
        {
            std::string family, asset, shader = "builtin://shaders/font";
            if (!fields(font, {"family", "asset", "shader"}, error) || !font.contains("family") ||
                !bounded_text(font["family"], family, 128) || family.empty() ||
                !font.contains("asset") || !bounded_text(font["asset"], asset, 4096) ||
                asset.empty() ||
                (font.contains("shader") &&
                 (!bounded_text(font["shader"], shader, 4096) || shader.empty())))
            {
                if (error.empty())
                {
                    error = "Invalid font binding";
                }
                return false;
            }
            items.push_back(toml::table{{"family", family}, {"asset", asset}, {"shader", shader}});
        }
        table->insert_or_assign("fonts", std::move(items));
    }
    if (args.contains("textures"))
    {
        const auto& textures = args["textures"];
        if (!textures.is_array() || textures.size() > 256)
        {
            error = "textures requires at most 256 resource references";
            return false;
        }
        toml::array items;
        for (const auto& item : textures)
        {
            std::string path;
            if (!bounded_text(item, path, 4096) || path.empty())
            {
                error = "Invalid texture reference";
                return false;
            }
            items.push_back(path);
        }
        table->insert_or_assign("textures", std::move(items));
    }
    return true;
}
bool publish_pair(const fs::path& staged, const fs::path& target, const fs::path& stage_dir,
                  bool exists, std::string& error)
{
    auto staged_meta = staged;
    staged_meta += ".meta";
    auto target_meta = target;
    target_meta += ".meta";
    const auto backup = stage_dir / "previous.rml";
    auto backup_meta = backup;
    backup_meta += ".meta";
    std::error_code ec;
    if (exists)
    {
        fs::rename(target, backup, ec);
        if (ec)
        {
            error = "Cannot stage previous UI source";
            return false;
        }
        fs::rename(target_meta, backup_meta, ec);
        if (ec)
        {
            std::error_code rollback;
            fs::rename(backup, target, rollback);
            error = rollback
                        ? "UI metadata staging failed; source backup retained in cache/ui-source"
                        : "Cannot stage previous UI metadata";
            return false;
        }
    }
    fs::rename(staged, target, ec);
    const bool source_published = !ec;
    if (!ec)
    {
        fs::rename(staged_meta, target_meta, ec);
    }
    if (!ec)
    {
        return true;
    }
    std::error_code remove_error, source_error, meta_error;
    if (source_published)
    {
        fs::remove(target, remove_error);
    }
    if (exists)
    {
        fs::rename(backup, target, source_error);
        fs::rename(backup_meta, target_meta, meta_error);
    }
    error = remove_error || source_error || meta_error
                ? "UI publish rollback failed; recovery files retained in cache/ui-source"
                : "UI publish failed; original files restored";
    return false;
}
bool perform_source(const Json& args, bool save, const fs::path& project,
                    const ResourceCatalog& catalog, SourceJob& job)
{
    std::string path = args["path"].get<std::string>();
    fs::path source_path;
    if (!safe_path(project, path, source_path, job.error))
    {
        return false;
    }
    pipeline::ProjectLock lock;
    bool acquired = false;
    if (!lock.try_acquire(project / "cache/.asset-build.lock", acquired, job.error) || !acquired)
    {
        if (job.error.empty())
        {
            job.error = "Asset pipeline is busy; retry this source operation";
        }
        return false;
    }
    std::string source, metadata;
    bool exists = false;
    if (!read_pair(source_path, source, metadata, exists, job.error))
    {
        return false;
    }
    toml::table meta;
    if (exists && !read_meta(metadata, meta, job.error))
    {
        return false;
    }
    const auto source_hash = exists ? pipeline::hash_text(source) : std::string{};
    const auto meta_hash = exists ? pipeline::hash_text(metadata) : std::string{};
    if (!save)
    {
        job.result = bindings(meta);
        job.result.update({{"path", path},
                           {"exists", exists},
                           {"source", source},
                           {"sourceHash", source_hash},
                           {"metaHash", meta_hash},
                           {"guid", meta["guid"].value_or(std::string{})},
                           {"stage", "read"}});
        return true;
    }
    if (source_hash != args["expectedSourceHash"].get<std::string>() ||
        meta_hash != args["expectedMetaHash"].get<std::string>())
    {
        job.error = "UI source or metadata changed; read current source before saving";
        return false;
    }
    if (!exists)
    {
        meta = toml::table{{"guid", pipeline::new_guid()}, {"importer", "ui"}, {"meta_version", 1}};
    }
    if (!save_bindings(args, meta, job.error))
    {
        return false;
    }
    std::ostringstream stream;
    stream << toml::toml_formatter(meta);
    metadata = stream.str();
    source = args["source"].get<std::string>();
    const auto stage_dir = project / "cache/ui-source" / PathUtils::from_utf8(pipeline::new_guid());
    std::error_code ec;
    fs::create_directories(stage_dir, ec);
    if (ec)
    {
        job.error = "Cannot create UI source staging directory";
        return false;
    }
    const auto staged = stage_dir / "document.rml";
    auto staged_meta = staged;
    staged_meta += ".meta";
    bool validated = pipeline::write_text(staged, source, job.error) &&
                     pipeline::write_text(staged_meta, metadata, job.error);
    std::map<Guid, schema::AssetKind> kinds;
    for (const auto& entry : catalog.data().entries)
    {
        if (entry)
        {
            kinds[entry->guid] = entry->kind;
        }
    }
    auto identities = catalog.identities();
    for (const auto& entry : builtin_resource_catalog().entries)
    {
        if (!entry)
        {
            job.error = "Builtin UI dependency catalog is invalid";
            validated = false;
            break;
        }
        Guid existing;
        if (identities.path_to_guid(entry->path, existing)
                ? existing != entry->guid
                : !identities.add(entry->path, entry->guid))
        {
            job.error = "Builtin UI dependency identity conflicts with project resources";
            validated = false;
            break;
        }
        kinds[entry->guid] = entry->kind;
    }
    pipeline::ImportContext context{identities, kinds, project, project / "assets",
                                    stage_dir,  {},    nullptr, {}};
    pipeline::ImportAsset asset;
    asset.guid = meta["guid"].value_or(std::string{});
    asset.path = path;
    asset.source = staged;
    asset.kind = schema::AssetKind::ui;
    asset.importer_id = "ui";
    asset_import::UiImporter importer;
    if (validated)
    {
        validated = importer.scan_dependencies(asset, context, job.error) &&
                    importer.convert(asset, context, job.error);
    }
    if (!validated || job.canceled.load())
    {
        fs::remove_all(stage_dir, ec);
        if (job.error.empty())
        {
            job.error = "UI source operation canceled before publish";
        }
        return false;
    }
    // Recheck identity under the same pipeline lock before publishing two source files.
    fs::path checked;
    if (!safe_path(project, path, checked, job.error) || checked != source_path)
    {
        fs::remove_all(stage_dir, ec);
        return false;
    }
    std::string latest_source, latest_meta;
    bool latest_exists = false;
    if (!read_pair(source_path, latest_source, latest_meta, latest_exists, job.error) ||
        exists != latest_exists ||
        source_hash != (exists ? pipeline::hash_text(latest_source) : "") ||
        meta_hash != (exists ? pipeline::hash_text(latest_meta) : ""))
    {
        fs::remove_all(stage_dir, ec);
        if (job.error.empty())
        {
            job.error = "UI files changed during validation";
        }
        return false;
    }
    fs::create_directories(source_path.parent_path(), ec);
    if (ec)
    {
        fs::remove_all(stage_dir, ec);
        job.error = "Cannot create UI source directory";
        return false;
    }
    if (!publish_pair(staged, source_path, stage_dir, exists, job.error))
    {
        return false;
    }
    fs::remove_all(stage_dir, ec);
    job.result = bindings(meta);
    job.result.update({{"path", path},
                       {"guid", asset.guid},
                       {"stage", "source_saved"},
                       {"import", "pending"},
                       {"view", "pending"},
                       {"sourceHash", pipeline::hash_text(source)},
                       {"metaHash", pipeline::hash_text(metadata)}});
    return true;
}
} // namespace
bool UiEditorCommands::State::source(std::string_view name, const Json& args, Json& result,
                                     std::string& error)
{
    if (name == "get_ui_source" && args.is_object() && args.contains("job"))
    {
        std::string id;
        if (!fields(args, {"job"}, error) || !bounded_text(args["job"], id, 64))
        {
            return false;
        }
        auto found = sources.find(id);
        if (found == sources.end())
        {
            error = "Unknown UI source job";
            return false;
        }
        const auto& job = *found->second;
        // Worker output is read only after its completion delegate publishes done on this thread.
        result = {{"job", id},
                  {"status", !job.done     ? "running"
                             : job.success ? "complete"
                                           : "failed"}};
        if (job.done)
        {
            result["result"] = job.result;
            result["error"] = job.error;
        }
        return true;
    }
    const bool save = name == "save_ui_source";
    if (!(save ? fields(args,
                        {"path", "source", "expectedSourceHash", "expectedMetaHash", "fonts",
                         "textures"},
                        error)
               : fields(args, {"path"}, error)))
    {
        return false;
    }
    std::string path, text;
    if (!args.contains("path") || !bounded_text(args["path"], path, 4096) || path.empty())
    {
        error = "path is required";
        return false;
    }
    if (save &&
        (!args.contains("source") || !bounded_text(args["source"], text, max_source) ||
         !args.contains("expectedSourceHash") ||
         !bounded_text(args["expectedSourceHash"], text, 128) ||
         !args.contains("expectedMetaHash") || !bounded_text(args["expectedMetaHash"], text, 128)))
    {
        error = "Save requires source and the exact source/meta hashes from get_ui_source";
        return false;
    }
    for (const auto& [id, job] : sources)
    {
        if (!job->done)
        {
            error = "A UI source operation is already running";
            return false;
        }
    }
    auto catalog = std::make_shared<ResourceCatalog>();
    if (!ResourceSystem::read_catalog(project / "cache", *catalog))
    {
        error = "UI source validation requires the published project resource catalog";
        return false;
    }
    auto job = std::make_shared<SourceJob>();
    job->id = std::to_string(++sequence);
    const auto root = project;
    if (!tasks.submit(
            "UI source", [args, save, root, catalog, job]
            { return perform_source(args, save, root, *catalog, *job); },
            [job](bool success)
            {
                job->success = success;
                job->done = true;
            },
            TaskPriority::normal))
    {
        error = "UI source task was not accepted";
        return false;
    }
    while (sources.size() >= 16)
    {
        sources.erase(sources.begin());
    }
    sources.emplace(job->id, job);
    result = {{"job", job->id}, {"accepted", true}, {"status", "running"}};
    return true;
}
} // namespace engine::plugins::ui
