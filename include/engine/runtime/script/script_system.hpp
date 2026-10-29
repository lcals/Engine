#pragma once

#include "runtime/system/system.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine
{

class WorldSystem;
class ResourceCatalog;
struct ResourceMount;
using MountList = std::vector<ResourceMount>;

struct ScriptStatus
{
    bool running = false;
    bool hot_reload = false;
    std::string active_revision;
    std::string available_revision;
    std::string last_error;
};

class ScriptSystem final : public System
{
public:
    explicit ScriptSystem(WorldSystem* world = nullptr) noexcept;
    ~ScriptSystem() noexcept override;

    // 预检只读取候选目录，不执行用户代码、不改变活动资源或旧玩法。
    [[nodiscard]] bool prepare_gameplay(const ResourceCatalog& catalog, const MountList& mounts,
                                        std::string_view entry = {},
                                        std::string_view revision = {});
    [[nodiscard]] bool start_prepared_gameplay();
    // 从宿主已提交的活动目录启动；目录缺失时返回失败，不回退物理脚本目录。
    [[nodiscard]] bool start_gameplay(std::string_view entry = {});
    void stop_gameplay() noexcept;
    void set_hot_reload(bool enabled);
    [[nodiscard]] const ScriptStatus& status() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    struct State;
    // 可空借用，非空时必须活到脚本系统关闭。
    WorldSystem* world_ = nullptr;
    ScriptStatus status_;
    std::unique_ptr<State> state_;
};

} // namespace engine
