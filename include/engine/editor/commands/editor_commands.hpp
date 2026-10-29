#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace engine::editor
{

class EditorEdit;

class EditorCommands final
{
public:
    using SceneIdProvider = std::function<std::uint64_t()>;
    using Handler = std::function<bool(const nlohmann::json&, nlohmann::json&,
                                       std::unique_ptr<EditorEdit>&, std::string&)>;

    // 提供者及处理函数借用的对象须存活到服务关闭；仅由编辑器主线程访问。
    explicit EditorCommands(SceneIdProvider scene_id);
    ~EditorCommands() noexcept;
    EditorCommands(const EditorCommands&) = delete;
    EditorCommands& operator=(const EditorCommands&) = delete;

    // 修改处理函数创建尚未应用的记录；查询处理函数返回空记录。
    [[nodiscard]] bool register_command(std::string_view name, bool undoable, Handler handler);
    [[nodiscard]] bool execute(std::string_view name, const nlohmann::json& arguments,
                               nlohmann::json& result, std::string& error);
    void clear_history() noexcept;
    [[nodiscard]] std::size_t undo_count() const noexcept;
    [[nodiscard]] std::size_t redo_count() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace engine::editor
