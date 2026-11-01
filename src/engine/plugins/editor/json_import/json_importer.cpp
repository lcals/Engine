#include "plugins/editor/json_import/json_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace engine::plugins::json_import
{

schema::AssetKind JsonImporter::kind() const noexcept
{
    return schema::AssetKind::json;
}

bool JsonImporter::matches(const std::filesystem::path& source) const
{
    return editor::asset_pipeline::source_extension(source) == ".json";
}

bool JsonImporter::inspect_batch(const editor::asset_pipeline::ImportContext&,
                                 std::string& fingerprint, std::string& error)
{
    error.clear();
    fingerprint = "json-importer-1:utf8-no-bom:max-bytes-4194304:nlohmann-" +
                  std::to_string(NLOHMANN_JSON_VERSION_MAJOR) + "." +
                  std::to_string(NLOHMANN_JSON_VERSION_MINOR) + "." +
                  std::to_string(NLOHMANN_JSON_VERSION_PATCH);
    return true;
}

bool JsonImporter::convert(const editor::asset_pipeline::ImportAsset& asset,
                           const editor::asset_pipeline::ImportContext& context,
                           std::string& error) const
{
    error.clear();
    std::vector<std::byte> bytes;
    if (!editor::asset_pipeline::read_bytes(asset.source, bytes, error))
    {
        return false;
    }
    constexpr std::size_t maximum_json_size = 4U * 1024U * 1024U;
    if (bytes.empty() || bytes.size() > maximum_json_size)
    {
        error = "JSON resource must contain 1 to 4194304 UTF-8 bytes: " + asset.path;
        return false;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (text.starts_with("\xef\xbb\xbf") || text.find('\0') != std::string_view::npos ||
        !nlohmann::json::accept(text.begin(), text.end()))
    {
        error = "Invalid UTF-8 JSON resource (BOM and comments are not supported): " + asset.path;
        return false;
    }
    return editor::asset_pipeline::write_bytes(context.output / asset.guid, bytes, error);
}

} // namespace engine::plugins::json_import
