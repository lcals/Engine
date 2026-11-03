#pragma once

#include "runtime/gameplay/script_runtime.hpp"
#include "runtime/resource/resource_system.hpp"

namespace test_support
{

// 与 GameplaySession 使用同一候选入口；不保留测试专用的生产启动分支。
inline bool start_script(engine::ScriptRuntime& script, engine::ResourceSystem& resources,
                         const engine::MountList& mounts, std::string_view entry = {})
{
    const engine::ResourceCatalog candidate(resources.catalog());
    if (!script.prepare_gameplay(candidate, mounts, entry))
    {
        return false;
    }
    script.stop_gameplay();
    return resources.update_catalog(candidate.data(), mounts) && script.start_prepared_gameplay();
}

} // namespace test_support
