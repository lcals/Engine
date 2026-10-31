#pragma once

#include <filesystem>
#include <string>

namespace engine::editor
{

// 一次保存成功发布的单项源资源；摘要用于区分自身保存与后续外部修改。
struct SceneSavedResource
{
    std::string guid;
    std::filesystem::path source;
    std::string source_hash;
    std::string meta_hash;
};

} // namespace engine::editor
