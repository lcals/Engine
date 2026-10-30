#pragma once

#include <filesystem>
#include <functional>
#include <memory>
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
    // 成功保存当前逻辑路径后建立修改检查点，恢复外部资源自动刷新。
    void mark_saved(std::string_view path) noexcept;
    [[nodiscard]] const std::string& last_error() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace engine::editor
