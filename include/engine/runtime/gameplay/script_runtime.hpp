#pragma once

#include "runtime/gameplay/script_status.hpp"
#include "runtime/system/system.hpp"

#include <string_view>
#include <vector>

namespace engine
{

class ResourceCatalog;
struct ResourceMount;
using MountList = std::vector<ResourceMount>;

class ScriptRuntime : public System
{
public:
    ~ScriptRuntime() noexcept override = default;

    // 预检候选目录，不执行玩法、不替换当前资源，失败保留当前运行。
    [[nodiscard]] virtual bool prepare_gameplay(const ResourceCatalog& catalog,
                                                const MountList& mounts,
                                                std::string_view entry = {},
                                                std::string_view revision = {}) = 0;
    [[nodiscard]] virtual bool start_prepared_gameplay() = 0;
    virtual void stop_gameplay() noexcept = 0;
    virtual void set_hot_reload(bool enabled) = 0;
    // 借用实现拥有的状态，后续操作可能更新内容。
    [[nodiscard]] virtual const ScriptStatus& status() const noexcept = 0;
};

} // namespace engine
