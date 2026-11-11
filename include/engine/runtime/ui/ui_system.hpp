#pragma once

#include "runtime/scene/scene_loading.hpp"
#include "runtime/system/system.hpp"
#include "runtime/ui/ui_element_info.hpp"
#include "runtime/ui/ui_event.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

union SDL_Event;
namespace engine
{
class WindowSystem;
class WorldSystem;
class FontSystem;
class UiSystem final : public System
{
public:
    // 借用对象须存活到关闭；绘制必须在渲染设备就绪到关闭之间调用。
    UiSystem(WindowSystem& window, WorldSystem& world, FontSystem& fonts);
    ~UiSystem() noexcept override;
    [[nodiscard]] SceneWorkState prepare_scope(SceneScopeId scope, const SceneWorkBudget& budget);
    [[nodiscard]] bool scope_status(SceneScopeId scope, ScenePreparationSnapshot& output) const;
    void release_scope(SceneScopeId scope) noexcept;
    [[nodiscard]] bool render(std::uint32_t width, std::uint32_t height);
    // Prepare documents without drawing. View operations synchronize before resolving handles.
    [[nodiscard]] bool synchronize();
    [[nodiscard]] bool find_element(std::uint64_t node, std::string_view id,
                                    UiElementHandle& output);
    [[nodiscard]] bool document_root(std::uint64_t node, UiElementHandle& output);
    [[nodiscard]] bool inspect_element(UiElementHandle handle, UiElementInfo& output);
    [[nodiscard]] bool child_element(UiElementHandle parent, std::uint32_t index,
                                     UiElementHandle& output);
    [[nodiscard]] bool element_style(UiElementHandle handle, std::string_view name,
                                     std::string& output);
    // Logical window coordinates. Successful misses return an empty handle.
    [[nodiscard]] bool hit_test(float x, float y, UiElementHandle& output);
    [[nodiscard]] bool pointer_active() const noexcept;
    [[nodiscard]] bool focus_element(UiElementHandle handle);
    [[nodiscard]] bool is_valid(UiElementHandle handle);
    [[nodiscard]] std::uint64_t document_id(std::uint64_t node);
    [[nodiscard]] bool document_visible(std::uint64_t node);
    [[nodiscard]] bool set_document_visible(std::uint64_t node, bool visible);
    // Failed reads clear output; failed writes do not change the target.
    [[nodiscard]] bool element_id(UiElementHandle handle, std::string& output);
    [[nodiscard]] bool text(UiElementHandle handle, std::string& output);
    [[nodiscard]] bool value(UiElementHandle handle, std::string& output);
    [[nodiscard]] bool set_text(UiElementHandle handle, std::string_view value);
    [[nodiscard]] bool set_value(UiElementHandle handle, std::string_view value);
    [[nodiscard]] bool set_visible(UiElementHandle handle, bool value);
    [[nodiscard]] bool set_enabled(UiElementHandle handle, bool value);
    [[nodiscard]] bool set_class(UiElementHandle handle, std::string_view name, bool active);
    [[nodiscard]] bool set_style(UiElementHandle handle, std::string_view name,
                                 std::string_view value);
    [[nodiscard]] bool set_image(UiElementHandle handle, std::string_view path);
    // 设备仍有效时释放绘制缓存；保留文档、控件身份及运行时修改。
    void release_gpu() noexcept;
    void release_scene() noexcept;
    [[nodiscard]] bool process_event(const SDL_Event& event);
    // Explicit SDL modifier bits for caller-generated input; does not change OS input state.
    [[nodiscard]] bool process_event(const SDL_Event& event, std::uint16_t modifiers);
    // 文档同步后查询持续占用；普通指针 capture 不表示全局独占。
    [[nodiscard]] bool keyboard_claimed() const noexcept;
    [[nodiscard]] bool pointer_claimed() const noexcept;
    [[nodiscard]] bool owns_pointer(const SDL_Event& event) const noexcept;
    // 安全取消不产生普通 click；文档、控件和已排队业务事件保持原契约。
    void cancel_input() noexcept;
    [[nodiscard]] std::size_t document_count() const noexcept;
    [[nodiscard]] bool poll_event(UiEvent& output);

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    struct State;
    std::unique_ptr<State> state_;
    WindowSystem& window_;
    WorldSystem& world_;
};
} // namespace engine
