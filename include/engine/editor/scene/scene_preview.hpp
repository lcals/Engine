#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace engine
{
class Camera;
class GameplaySession;
class RenderSystem;
class WindowSystem;
class WorldSystem;
struct EngineConfig;
} // namespace engine

namespace engine::editor
{
class EditorCommands;
class EditorEnvironment;
struct SceneSavedResource;

class ScenePreview final
{
public:
    using ImportCallback = std::function<bool(std::string&)>;

    // 所有对象及回调依赖持续借用；已注册的命令服务须先于本对象销毁。
    ScenePreview(EditorEnvironment& environment, GameplaySession& gameplay, WorldSystem& world,
                 RenderSystem& renderer, WindowSystem& window, std::filesystem::path project_root,
                 ImportCallback import);
    ~ScenePreview() noexcept;
    ScenePreview(const ScenePreview&) = delete;
    ScenePreview& operator=(const ScenePreview&) = delete;
    ScenePreview(ScenePreview&&) = delete;
    ScenePreview& operator=(ScenePreview&&) = delete;

    [[nodiscard]] bool initialize(const EngineConfig& config);
    [[nodiscard]] bool register_commands(EditorCommands& commands);
    [[nodiscard]] bool tick(double delta_time);
    void close() noexcept;
    [[nodiscard]] bool active() const noexcept;
    // 镜头只属于编辑器独立世界，返回值借用至关闭预览或对象销毁。
    [[nodiscard]] const Camera* camera() const noexcept;
    // 保存当前路径后记录已发布资源；自身增量导入保留当前场景和撤销历史。
    void mark_saved(std::string_view path, std::span<const SceneSavedResource> resources);
    [[nodiscard]] const std::string& last_error() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace engine::editor
