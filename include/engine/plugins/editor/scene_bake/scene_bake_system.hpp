#pragma once

#include "runtime/system/system.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace engine
{
class RenderSystem;
class ResourceSystem;
} // namespace engine
namespace engine::editor
{
class EditorCommands;
}
namespace engine::plugins::scene_bake
{

class SceneBakeSystem final : public System
{
public:
    using ValidateCallback = std::function<bool(std::string_view, std::uint64_t&, std::string&)>;
    using PublishCallback = std::function<bool(std::string&)>;

    // 主线程借用；宿主检查当前已保存预览，发布后执行通用导入/刷新。
    SceneBakeSystem(ResourceSystem& resources, RenderSystem& renderer,
                    std::filesystem::path project_root, ValidateCallback validate,
                    PublishCallback publish);
    // 宿主先 shutdown 本系统，再关闭渲染器，最后销毁本对象，保留在途读回内存。
    ~SceneBakeSystem() noexcept override;
    [[nodiscard]] bool register_commands(editor::EditorCommands& commands);
    [[nodiscard]] bool busy() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace engine::plugins::scene_bake
