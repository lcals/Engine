#pragma once

#include "resource_services.hpp"
#include "runtime/gameplay/script_runtime.hpp"

namespace test_support
{

// 与 GameplaySession 使用同一候选入口；不保留测试专用的生产启动分支。
inline bool prepare_script(engine::ScriptRuntime& script, ResourceServices& services,
                           const engine::MountList& mounts, std::string_view entry = {})
{
    const engine::ResourceCatalog candidate(services.core.catalog());
    if (!services.publish_and_load(candidate.data(), mounts))
    {
        return false;
    }
    engine::ResourceReference root;
    return services.load(entry.empty() ? "scripts/game.js" : entry, root) &&
           script.prepare_gameplay(root.handle(), candidate.revision());
}

inline bool start_script(engine::ScriptRuntime& script, ResourceServices& services,
                         const engine::MountList& mounts, std::string_view entry = {})
{
    if (!prepare_script(script, services, mounts, entry))
    {
        return false;
    }
    script.stop_gameplay();
    return script.start_prepared_gameplay();
}

inline bool execute_script(ResourceServices& services, engine::ScriptRuntime& script,
                           const engine::ProjectScriptRequest& request, std::string& result,
                           std::string& error)
{
    static std::uint64_t sequence = 0;
    char guid[37]{};
    std::snprintf(guid, sizeof(guid), "99100000-0000-4000-8000-%012llu",
                  static_cast<unsigned long long>(++sequence));
    engine::schema::AssetEntryT info;
    info.guid = guid;
    info.path = std::string("requests/") + guid + ".js";
    info.module_path = request.module_path;
    info.kind = engine::schema::AssetKind::script;
    const auto* begin = reinterpret_cast<const std::byte*>(request.javascript.data());
    std::unique_ptr<engine::Resource> candidate = std::make_unique<engine::ScriptResource>(
        std::move(info), engine::ByteBuffer(begin, begin + request.javascript.size()));
    engine::ResourceHandle handle;
    result.clear();
    error.clear();
    if (!services.loader.load_memory(candidate, {}, handle))
    {
        error = services.loader.last_error();
        return false;
    }
    engine::ResourceReference root;
    if (!root.adopt(&services.loader, handle))
    {
        return false;
    }
    return script.execute_project_script(root.handle(), request, result, error);
}

} // namespace test_support
