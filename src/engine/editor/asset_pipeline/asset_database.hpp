#pragma once

#include "editor/asset_pipeline/import_asset.hpp"

#include <cstdint>
#include <map>

struct sqlite3;
#include <string>
#include <vector>

namespace engine::editor::asset_pipeline
{

struct AssetRecord
{
    ImportAsset asset;
    std::string source_hash;
    std::string meta_hash;
    std::string build_key;
    std::string output_hash;
    std::uint64_t output_size = 0;
    std::uint32_t header_size = 0;
    std::string module_path;
};

class AssetDatabase final
{
public:
    AssetDatabase() noexcept = default;
    ~AssetDatabase() noexcept;
    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;

    [[nodiscard]] bool open(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] bool load(std::map<Guid, AssetRecord>& records, std::string& error);
    [[nodiscard]] bool revision(std::string& output, std::string& error);
    [[nodiscard]] bool begin_update(const std::vector<AssetRecord>& records,
                                    const std::string& revision, std::string& error);
    [[nodiscard]] bool commit(std::string& error);
    void rollback() noexcept;

private:
    [[nodiscard]] bool execute(const char* sql, std::string& error);
    sqlite3* database_ = nullptr;
    bool transaction_ = false;
};

} // namespace engine::editor::asset_pipeline
