#include "editor/asset_pipeline/importer_utils.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/editor_tools.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <system_error>
#include <utility>

namespace engine::editor::asset_pipeline
{
bool read_table(const std::filesystem::path& path, toml::table& table, std::string& error)
{
    std::string text;
    if (!ResourceSystem::read_text(path, text))
    {
        error = "cannot read resource descriptor: " + PathUtils::to_utf8(path);
        return false;
    }
    auto result = toml::parse(text);
    if (!result)
    {
        error = "invalid resource TOML: " + PathUtils::to_utf8(path) + ": " +
                std::string(result.error().description());
        return false;
    }
    table = std::move(result).table();
    return true;
}

bool reference(const Guid& guid, schema::AssetKind kind, const ImportContext& context,
               std::vector<Guid>& dependencies, std::string& error)
{
    std::string path;
    const auto entry = context.kinds.find(guid);
    if (!GuidMap::valid_guid(guid) || !context.identities.guid_to_path(guid, path) ||
        entry == context.kinds.end() || entry->second != kind)
    {
        error = "missing resource GUID or incompatible resource type: " + guid;
        return false;
    }
    if (std::find(dependencies.begin(), dependencies.end(), guid) == dependencies.end())
    {
        dependencies.push_back(guid);
    }
    return true;
}

bool parameter_values(const schema::MaterialValueUnion& value, std::array<double, 4>& values,
                      std::size_t& size)
{
    values = {};
    size = 0;
    if (value.value == nullptr)
    {
        return false;
    }
    switch (value.type)
    {
    case schema::MaterialValue::FloatValue:
        values[0] = value.AsFloatValue()->value;
        size = 1;
        break;
    case schema::MaterialValue::Vec2Value:
    {
        const auto& data = value.AsVec2Value()->value;
        values = {data.x(), data.y(), 0, 0};
        size = 2;
        break;
    }
    case schema::MaterialValue::Vec3Value:
    {
        const auto& data = value.AsVec3Value()->value;
        values = {data.x(), data.y(), data.z(), 0};
        size = 3;
        break;
    }
    case schema::MaterialValue::Vec4Value:
    {
        const auto& data = value.AsVec4Value()->value;
        values = {data.x(), data.y(), data.z(), data.w()};
        size = 4;
        break;
    }
    default:
        return false;
    }
    return std::all_of(
        values.begin(), values.begin() + static_cast<std::ptrdiff_t>(size), [](double number)
        { return std::isfinite(number) && std::abs(number) <= std::numeric_limits<float>::max(); });
}

bool valid_parameter(const schema::ShaderParameterT& definition,
                     const schema::MaterialValueUnion& value)
{
    std::array<double, 4> values{};
    std::size_t size = 0;
    return value.type == definition.default_value.type && parameter_values(value, values, size) &&
           (!definition.bounded ||
            std::all_of(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(size),
                        [&](double number)
                        { return number >= definition.minimum && number <= definition.maximum; }));
}

bool file_hash(const std::filesystem::path& path, std::string& hash, std::string& error)
{
    std::vector<std::byte> bytes;
    if (!read_bytes(path, bytes, error))
    {
        return false;
    }
    hash = hash_bytes(bytes);
    return true;
}

bool hash_optional_file(const std::filesystem::path& path, std::string& hash, std::string& error)
{
    std::error_code code;
    const bool exists = std::filesystem::exists(path, code);
    if (code)
    {
        error = "cannot inspect configuration: " + PathUtils::to_utf8(path);
        return false;
    }
    if (!exists)
    {
        hash = "missing";
        return true;
    }
    return file_hash(path, hash, error);
}

bool schema_fingerprint(std::initializer_list<std::string_view> names, std::string& fingerprint,
                        std::string& error)
{
    fingerprint.clear();
    std::filesystem::path schemas;
    if (!editor_tool_data_path("include/schemas/runtime", ENGINE_SCHEMA_SOURCE_DIR, schemas, error))
    {
        return false;
    }
    for (const auto name : names)
    {
        std::string hash;
        if (!file_hash(schemas / name, hash, error))
        {
            fingerprint.clear();
            return false;
        }
        fingerprint += ":" + std::string(name) + ":" + hash;
    }
    return true;
}
std::string source_extension(const std::filesystem::path& path)
{
    auto extension = PathUtils::to_utf8(path.extension());
    std::transform(
        extension.begin(), extension.end(), extension.begin(), [](unsigned char value)
        { return static_cast<char>(value >= 'A' && value <= 'Z' ? value + 32 : value); });
    return extension;
}
std::string generic_path(const std::filesystem::path& path)
{
    const auto value = path.generic_u8string();
    return {value.begin(), value.end()};
}

} // namespace engine::editor::asset_pipeline
