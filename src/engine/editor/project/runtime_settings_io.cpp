#include "editor/project/runtime_settings_io.hpp"

#include "runtime/resource/resource_system.hpp"

#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include <cmath>
#include <system_error>

namespace engine::editor
{
namespace
{

using Json = nlohmann::json;

bool convert(const toml::node& source, Json& output)
{
    if (const auto* table = source.as_table())
    {
        output = Json::object();
        for (const auto& [key, value] : *table)
        {
            if (!convert(value, output[std::string(key.str())]))
            {
                return false;
            }
        }
        return true;
    }
    if (const auto* array = source.as_array())
    {
        output = Json::array();
        for (const auto& value : *array)
        {
            Json item;
            if (!convert(value, item))
            {
                return false;
            }
            output.push_back(std::move(item));
        }
        return true;
    }
    if (source.is_string())
    {
        output = *source.value<std::string>();
        return true;
    }
    if (source.is_integer())
    {
        output = *source.value<std::int64_t>();
        return true;
    }
    if (source.is_floating_point())
    {
        const auto value = *source.value<double>();
        if (!std::isfinite(value))
        {
            return false;
        }
        output = value;
        return true;
    }
    if (source.is_boolean())
    {
        output = *source.value<bool>();
        return true;
    }
    return false;
}

bool read_source(const std::filesystem::path& path, Json& output, std::string& error)
{
    std::error_code status;
    if (!std::filesystem::exists(path, status) && !status)
    {
        output = {{"version", 1}};
        return true;
    }
    std::string text;
    if (status || !ResourceSystem::read_text(path, text) || text.size() > 65536)
    {
        error = "Cannot read project runtime settings (maximum 64 KiB per file)";
        return false;
    }
    const auto parsed = toml::parse(text);
    if (!parsed || !convert(parsed.table(), output))
    {
        error = "Invalid project runtime settings TOML or unsupported value type";
        return false;
    }
    if (!output.contains("version") || !output["version"].is_number_integer() ||
        output["version"].get<std::int64_t>() != 1)
    {
        error = "Project runtime settings require version 1";
        return false;
    }
    for (const auto& [name, value] : output.items())
    {
        if (name != "version" && name != "defaults" && name != "profiles" && name != "overrides" &&
            (name != "game_language" || path.filename() != "runtime.toml"))
        {
            error = "Unknown project runtime settings field: " + name;
            return false;
        }
    }
    return true;
}

bool attach_parallel(Json& layer, const Json& parallel, std::string& error)
{
    if (!layer.is_object() || !parallel.is_object() || layer.contains("parallel"))
    {
        error = "Invalid or duplicate parallel settings layer";
        return false;
    }
    layer["parallel"] = parallel;
    return true;
}

} // namespace

bool load_project_runtime_settings(const std::filesystem::path& project_root,
                                   std::string_view profile, RuntimeSettings& output,
                                   std::string& error)
{
    error.clear();
    Json document;
    Json parallel;
    if (!read_source(project_root / "settings/runtime.toml", document, error) ||
        !read_source(project_root / "settings/parallel.toml", parallel, error))
    {
        return false;
    }
    for (const auto* name : {"defaults", "overrides"})
    {
        if (parallel.contains(name))
        {
            if (!document.contains(name))
            {
                document[name] = Json::object();
            }
            if (!attach_parallel(document[name], parallel[name], error))
            {
                return false;
            }
        }
    }
    if (const auto profiles = parallel.find("profiles"); profiles != parallel.end())
    {
        if (!profiles->is_object())
        {
            error = "Project parallel profiles must be a table";
            return false;
        }
        if (!document.contains("profiles"))
        {
            document["profiles"] = Json::object();
        }
        if (!document["profiles"].is_object())
        {
            error = "Project runtime profiles must be a table";
            return false;
        }
        for (const auto& [name, layer] : profiles->items())
        {
            auto& target = document["profiles"];
            if (!target.contains(name))
            {
                target[name] = Json::object();
            }
            if (!attach_parallel(target[name], layer, error))
            {
                return false;
            }
        }
    }
    return decode_runtime_settings(document.dump(), profile, output, error);
}

} // namespace engine::editor
