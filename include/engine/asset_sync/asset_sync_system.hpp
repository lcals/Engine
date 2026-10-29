#pragma once

#include "system.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace engine
{

class AssetSyncSystem final : public System
{
public:
    AssetSyncSystem() noexcept;
    ~AssetSyncSystem() noexcept override;

    [[nodiscard]] bool sync(const std::filesystem::path& source_assets,
                            const std::filesystem::path& runtime_dir,
                            std::string_view entry = "scripts/game.js");
    [[nodiscard]] const std::string& revision() const noexcept;
    [[nodiscard]] const std::string& last_error() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    std::string revision_;
    std::string last_error_;
};

} // namespace engine
