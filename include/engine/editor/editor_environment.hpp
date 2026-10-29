#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace engine
{
class GameplaySession;
}

namespace engine::schema
{
struct AssetCatalogDataT;
}

namespace engine::editor
{

namespace asset_pipeline
{
class ProjectLock;
}

class ProjectSystem;

class EditorSystem;

class EditorEnvironment final
{
public:
    // 借用已由宿主管理的项目；项目须先初始化并存活到环境关闭。
    explicit EditorEnvironment(ProjectSystem& project,
                               std::filesystem::path persistent_directory = {});
    ~EditorEnvironment() noexcept;
    EditorEnvironment(const EditorEnvironment&) = delete;
    EditorEnvironment& operator=(const EditorEnvironment&) = delete;
    EditorEnvironment(EditorEnvironment&&) = delete;
    EditorEnvironment& operator=(EditorEnvironment&&) = delete;

    [[nodiscard]] bool initialize();
    void shutdown() noexcept;
    [[nodiscard]] bool refresh_resources(GameplaySession& gameplay);
    [[nodiscard]] bool is_initialized() const noexcept;
    [[nodiscard]] bool apply_resources(const schema::AssetCatalogDataT& data,
                                       GameplaySession& gameplay);

private:
    friend class EditorSystem;

    ProjectSystem& project_;
    std::string project_identity_;
    std::string resource_revision_;
    std::filesystem::path persistent_directory_;
    std::filesystem::path builtin_path_;
    std::filesystem::path session_marker_;
    std::unique_ptr<asset_pipeline::ProjectLock> session_lease_;
    bool initialized_ = false;
};

} // namespace engine::editor
