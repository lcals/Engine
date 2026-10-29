#pragma once

#include <string>

namespace engine
{

class EditorEdit
{
public:
    virtual ~EditorEdit() noexcept = default;
    // 仅在编辑器主线程调用；失败不改变对象，成功后可用另一方法恢复。
    [[nodiscard]] virtual bool apply(std::string& error) = 0;
    [[nodiscard]] virtual bool revert(std::string& error) = 0;
};

} // namespace engine
