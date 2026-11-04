#include "plugins/editor/animation_tools/animation_authoring_commands.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "editor/asset_pipeline/project_lock.hpp"
#include "editor/commands/editor_commands.hpp"
#include "editor/commands/editor_edit.hpp"
#include "plugins/editor/asset_import/fbx_authoring.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_filesystem.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <system_error>

namespace engine::plugins::animation_tools
{
namespace
{
namespace fs = std::filesystem;
namespace io = editor::asset_pipeline;
using Json = nlohmann::json;
constexpr std::array sections{"skeleton_sockets", "skeleton_aim_ik", "skeleton_two_bone_ik",
                              "animation_events"};

bool paths(const fs::path& project, const std::string& path, fs::path& source, fs::path& meta,
           std::string& error)
{
    const auto extension = PathUtils::to_utf8(PathUtils::from_utf8(path).extension());
    if (io::source_extension(PathUtils::from_utf8(path)) != ".fbx")
    {
        error = "Animation authoring requires an FBX source asset path";
        return false;
    }
    return io::project_file_path(project, "assets", path, extension, source, error) &&
           io::project_file_path(project, "assets", path + ".meta", ".meta", meta, error);
}

bool snapshot(const fs::path& project, const std::string& path, std::string& source_hash,
              std::string& metadata, toml::table& table, std::string& error)
{
    fs::path source, meta;
    ByteBuffer bytes;
    if (!paths(project, path, source, meta, error) || !io::read_bytes(source, bytes, error) ||
        !ResourceSystem::read_text(meta, metadata))
    {
        if (error.empty())
        {
            error = "Cannot read FBX source and adjacent metadata";
        }
        return false;
    }
    auto parsed = toml::parse(metadata);
    if (!parsed || parsed["meta_version"].value_or(std::int64_t{}) != 1 ||
        parsed["importer"].value_or(std::string{}) != "fbx" ||
        !GuidMap::valid_guid(parsed["guid"].value_or(std::string{})))
    {
        error = "Animation authoring requires valid FBX metadata and an existing GUID";
        return false;
    }
    source_hash = io::hash_bytes(bytes);
    table = std::move(parsed.table());
    return true;
}

Json value_json(const toml::node& node)
{
    if (const auto* values = node.as_array())
    {
        Json output = Json::array();
        for (const auto& item : *values)
        {
            output.push_back(value_json(item));
        }
        return output;
    }
    if (const auto* table = node.as_table())
    {
        Json output = Json::object();
        for (const auto& [key, item] : *table)
        {
            output[std::string(key.str())] = value_json(item);
        }
        return output;
    }
    if (const auto value = node.value<bool>())
    {
        return *value;
    }
    if (const auto value = node.value<std::int64_t>())
    {
        return *value;
    }
    if (const auto value = node.value<double>())
    {
        return *value;
    }
    if (const auto value = node.value<std::string>())
    {
        return *value;
    }
    return nullptr;
}

bool table_json(const Json& input, toml::table& output, std::string& error)
{
    if (!input.is_object())
    {
        error = "Animation authoring entries must be objects";
        return false;
    }
    for (const auto& [key, value] : input.items())
    {
        if (value.is_boolean())
        {
            output.insert(key, value.get<bool>());
        }
        else if (value.is_number_integer())
        {
            if (value.is_number_unsigned() &&
                value.get<std::uint64_t>() > std::numeric_limits<std::int64_t>::max())
            {
                error = "Animation authoring integer is out of range";
                return false;
            }
            output.insert(key, value.get<std::int64_t>());
        }
        else if (value.is_number_float() && std::isfinite(value.get<double>()))
        {
            output.insert(key, value.get<double>());
        }
        else if (value.is_string())
        {
            output.insert(key, value.get<std::string>());
        }
        else if (value.is_array() && value.size() <= 4)
        {
            toml::array values;
            for (const auto& item : value)
            {
                if (!item.is_number() || !std::isfinite(item.get<double>()))
                {
                    error = "Animation authoring vector arrays require finite numbers";
                    return false;
                }
                values.push_back(item.get<double>());
            }
            output.insert(key, std::move(values));
        }
        else
        {
            error = "Unsupported animation authoring value: " + key;
            return false;
        }
    }
    return true;
}

bool arguments(const Json& input, bool writing, std::string& error)
{
    if (!input.is_object() || !input.contains("path") || !input["path"].is_string())
    {
        error = "Animation authoring requires a source asset path";
        return false;
    }
    for (const auto& [key, unused] : input.items())
    {
        (void)unused;
        bool known = key == "path";
        if (writing)
        {
            known = known || key == "sourceHash" || key == "metaHash" ||
                    std::find(sections.begin(), sections.end(), key) != sections.end();
        }
        if (!known)
        {
            error = "Unknown animation authoring argument: " + key;
            return false;
        }
    }
    if (writing && (!input.contains("sourceHash") || !input["sourceHash"].is_string() ||
                    !input.contains("metaHash") || !input["metaHash"].is_string()))
    {
        error = "Read animation authoring first and provide its exact sourceHash and metaHash";
        return false;
    }
    return true;
}

class AnimationMetaEdit final : public editor::EditorEdit
{
public:
    AnimationMetaEdit(fs::path project, std::string path, std::string source_hash,
                      std::string before, std::string after)
        : project_(std::move(project)), path_(std::move(path)),
          source_hash_(std::move(source_hash)), before_(std::move(before)), after_(std::move(after))
    {
    }
    bool affects_scene() const noexcept override
    {
        return false;
    }
    bool apply(std::string& error) override
    {
        return replace(before_, after_, error);
    }
    bool revert(std::string& error) override
    {
        return replace(after_, before_, error);
    }

private:
    bool replace(const std::string& expected, const std::string& replacement, std::string& error)
    {
        io::ProjectLock lock;
        if (!lock.acquire(project_ / "cache/.asset-build.lock", error))
        {
            return false;
        }
        std::string source_hash, current;
        toml::table table;
        if (!snapshot(project_, path_, source_hash, current, table, error))
        {
            return false;
        }
        if (source_hash != source_hash_ || current != expected)
        {
            error = "FBX source or metadata changed outside this animation edit";
            return false;
        }
        if (expected == replacement)
        {
            return true;
        }
        fs::path source, meta;
        if (!paths(project_, path_, source, meta, error))
        {
            return false;
        }
        const auto identity = io::new_guid();
        if (identity.empty())
        {
            error = "Cannot allocate animation metadata staging identity";
            return false;
        }
        auto staging = meta;
        staging += ".animation-write-" + identity + ".meta";
        if (!io::write_text(staging, replacement, error) ||
            !SDL_RenamePath(PathUtils::to_utf8(staging).c_str(), PathUtils::to_utf8(meta).c_str()))
        {
            std::error_code cleanup;
            fs::remove(staging, cleanup);
            if (error.empty())
            {
                error = "Cannot atomically publish animation source metadata";
            }
            return false;
        }
        return true;
    }
    fs::path project_;
    std::string path_, source_hash_, before_, after_;
};

bool authoring(const fs::path& project, const Json& input, bool writing, Json& result,
               std::unique_ptr<editor::EditorEdit>& edit, std::string& error)
{
    if (!arguments(input, writing, error))
    {
        return false;
    }
    const auto path = input["path"].get<std::string>();
    std::string source_hash, before;
    toml::table meta;
    if (!snapshot(project, path, source_hash, before, meta, error))
    {
        return false;
    }
    if (writing &&
        (input["sourceHash"] != source_hash || input["metaHash"] != io::hash_text(before)))
    {
        error = "FBX source or metadata changed; read current animation authoring before saving";
        return false;
    }
    for (const auto* section : sections)
    {
        if (!writing || !input.contains(section))
        {
            continue;
        }
        const auto& entries = input[section];
        const std::size_t limit = std::string_view(section) == "animation_events" ? 4096 : 1024;
        if (!entries.is_array() || entries.size() > limit)
        {
            error = std::string(section) + " requires a bounded object array";
            return false;
        }
        toml::array values;
        for (const auto& entry : entries)
        {
            toml::table value;
            if (!table_json(entry, value, error))
            {
                return false;
            }
            values.push_back(std::move(value));
        }
        meta.insert_or_assign(section, std::move(values));
    }
    fs::path source, meta_path;
    asset_import::FbxDocument document;
    if (!paths(project, path, source, meta_path, error) ||
        !asset_import::load_document(source, document, error) ||
        !asset_import::read_animation_authoring(meta, document, error) ||
        !asset_import::validate_animation_authoring(document, error))
    {
        return false;
    }
    // The loaded source must be the same snapshot for which the edit was prepared.
    std::string current_hash, current_meta;
    toml::table unused;
    if (!snapshot(project, path, current_hash, current_meta, unused, error))
    {
        return false;
    }
    if (current_hash != source_hash || current_meta != before)
    {
        error = "FBX source or metadata changed while preparing animation authoring";
        return false;
    }
    std::string after = before;
    if (writing)
    {
        std::ostringstream formatted;
        formatted << toml::toml_formatter(meta);
        after = formatted.str();
        edit = std::make_unique<AnimationMetaEdit>(project, path, source_hash, before, after);
    }
    result = {{"path", path},
              {"guid", meta["guid"].value_or(std::string{})},
              {"sourceHash", source_hash},
              {"metaHash", io::hash_text(after)},
              {"saved", writing},
              {"reimportPending", writing && after != before},
              {"joints", Json::array()},
              {"animations", Json::array()}};
    for (const auto* section : sections)
    {
        result[section] = meta.contains(section) ? value_json(*meta.get(section)) : Json::array();
    }
    for (const auto* joint : document.joints)
    {
        result["joints"].push_back(document.node_paths.at(joint));
    }
    for (const auto& [key, animation] : document.animations)
    {
        result["animations"].push_back(
            {{"key", key}, {"duration", animation->time_end - animation->time_begin}});
    }
    return true;
}
} // namespace

bool register_animation_authoring_commands(editor::EditorCommands& commands,
                                           const fs::path& project)
{
    return commands.register_command(
               "get_animation_authoring", false,
               [project](const Json& input, Json& output, std::unique_ptr<editor::EditorEdit>& edit,
                         std::string& error)
               { return authoring(project, input, false, output, edit, error); }) &&
           commands.register_command(
               "set_animation_authoring", true,
               [project](const Json& input, Json& output, std::unique_ptr<editor::EditorEdit>& edit,
                         std::string& error)
               { return authoring(project, input, true, output, edit, error); });
}
} // namespace engine::plugins::animation_tools
