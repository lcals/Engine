#pragma once

#if defined(ENGINE_ENABLE_TEST_SUPPORT)
#include "runtime/gameplay/project_script_request.hpp"
#endif
#include "runtime/gameplay/script_status.hpp"
#include "runtime/resource/resource_handle.hpp"
#include "runtime/system/system.hpp"

#include <string_view>
#include <vector>

namespace engine
{

class GameplaySession;

class ScriptRuntime : public System
{
public:
    ~ScriptRuntime() noexcept override = default;

    [[nodiscard]] virtual bool bind_gameplay_session(GameplaySession* session) = 0;

    // 预检候选目录，不执行玩法、不替换当前资源，失败保留当前运行。
    [[nodiscard]] virtual bool prepare_gameplay(const ResourceHandle& entry,
                                                std::string_view revision = {}) = 0;
    [[nodiscard]] virtual bool start_prepared_gameplay() = 0;
    virtual void stop_gameplay() noexcept = 0;
#if defined(ENGINE_ENABLE_TEST_SUPPORT)
    // 仅活动 Run 的主线程安全点；失败不停止玩法，也不回滚脚本已产生的副作用。
    [[nodiscard]] virtual bool execute_project_script(const ResourceHandle& script,
                                                      const ProjectScriptRequest& request,
                                                      std::string& result_json,
                                                      std::string& error) = 0;
#endif

    // 借用实现拥有的状态，后续操作可能更新内容。
    [[nodiscard]] virtual const ScriptStatus& status() const noexcept = 0;
};

} // namespace engine
