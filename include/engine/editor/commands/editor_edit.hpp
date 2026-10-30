#pragma once

#include <string>

namespace engine::editor
{

class EditorEdit
{
public:
    virtual ~EditorEdit() noexcept = default;
    // 全局设置等不影响场景持久数据的编辑自行返回 false。
    [[nodiscard]] virtual bool affects_scene() const noexcept
    {
        return true;
    }
    // 仅在编辑器主线程调用；失败不改变对象，成功后可用另一方法恢复。
    [[nodiscard]] virtual bool apply(std::string& error) = 0;
    [[nodiscard]] virtual bool revert(std::string& error) = 0;
};

} // namespace engine::editor
