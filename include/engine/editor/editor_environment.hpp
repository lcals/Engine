#pragma once

#include <filesystem>
#include <string>

namespace engine
{

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

    [[nodiscard]] bool initialize(bool snapshot_builtin_assets = false);
    void shutdown() noexcept;
    [[nodiscard]] bool is_initialized() const noexcept;
    [[nodiscard]] bool prepare_builtin_assets();

private:
    friend class EditorSystem;

    ProjectSystem& project_;
    std::string project_identity_;
    std::filesystem::path persistent_directory_;
    std::filesystem::path builtin_path_;
    std::filesystem::path snapshot_directory_;
    bool initialized_ = false;
    bool snapshot_prepared_ = false;
};

} // namespace engine
