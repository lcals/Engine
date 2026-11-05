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

bool read_source(const std::filesystem::path& path, std::string_view platform, Json& output,
                 std::string& error)
{
    std::string text;
    if (!ResourceSystem::read_text(path, text) || text.size() > 65536)
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
    for (const auto& [name, value] : output.items())
    {
        if ((name != "common" && name != "quality" && name != "platform") || !value.is_object())
        {
            error = "Unknown project runtime settings field: " + name;
            return false;
        }
    }
    if (output.contains("quality") && !output["quality"].empty())
    {
        error = "Quality settings require business selection and are not enabled yet";
        return false;
    }
    auto selected = output.value("common", Json::object());
    if (const auto platforms = output.find("platform"); platforms != output.end())
    {
        for (const auto& [name, value] : platforms->items())
        {
            if ((name != "windows" && name != "macos" && name != "linux" && name != "android" &&
                 name != "ios") ||
                !value.is_object())
            {
                error = "Invalid runtime settings platform table: " + name;
                return false;
            }
            if (name == platform)
            {
                selected.merge_patch(value);
            }
        }
    }
    output = std::move(selected);
    return true;
}

} // namespace

bool load_project_runtime_settings(const std::filesystem::path& project_root,
                                   std::string_view platform, RuntimeSettings& output,
                                   std::string& error)
{
    error.clear();
    if (platform != "windows" && platform != "macos" && platform != "linux" &&
        platform != "android" && platform != "ios")
    {
        error = "Unknown runtime settings platform";
        return false;
    }
    Json document = Json::object();
    std::error_code status;
    auto entries = std::filesystem::directory_iterator(project_root / "settings", status);
    if (status == std::errc::no_such_file_or_directory)
    {
        error = "Project runtime settings directory is missing";
        return false;
    }
    for (; !status && entries != std::filesystem::directory_iterator{}; entries.increment(status))
    {
        const auto& path = entries->path();
        const auto name = path.stem().string();
        if (path.extension() != ".toml" || !name.starts_with("runtime-"))
        {
            continue;
        }
        Json module;
        if (name.size() == 8 || !read_source(path, platform, module, error))
        {
            error = name + ": " + error;
            return false;
        }
        if (!module.empty())
        {
            document[name.substr(8)] = std::move(module);
        }
    }
    if (status)
    {
        error = "Cannot enumerate project runtime settings: " + status.message();
        return false;
    }
    return decode_runtime_settings(document.dump(), output, error);
}

} // namespace engine::editor
