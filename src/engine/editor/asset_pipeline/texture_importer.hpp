#pragma once

#include <string>
#include <string_view>

namespace engine::asset_pipeline
{

struct ImportAsset;
struct ImportContext;

class TextureImporter final
{
public:
    TextureImporter() = delete;

    [[nodiscard]] static std::string_view default_meta() noexcept;
    [[nodiscard]] static std::string_view version() noexcept;
    // 仅在批次有失效贴图时，由协调线程在下发转换任务前调用；允许重复初始化。
    [[nodiscard]] static bool initialize(std::string& error);
    [[nodiscard]] static bool convert(const ImportAsset& asset, const ImportContext& context,
                                      std::string& error);
};

} // namespace engine::asset_pipeline
