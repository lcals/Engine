#pragma once

#include "plugins/runtime/resource_graph/resource_graph_system.hpp"
#include "resource_codecs.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/builtin_resource_candidate.hpp"
#include "runtime/resource/builtin_resources.hpp"
#include "runtime/resource/resource_reference.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

#include <cstdio>
#include <map>

namespace test_support
{
inline const engine::Resource* get_resource(engine::ResourceLoader& loader,
                                            const engine::ResourceHandle& handle)
{
    const engine::Resource* value = nullptr;
    return loader.get(handle, value) == engine::ResourceError::none ? value : nullptr;
}

// 测试宿主显式装配真实的读取核心、三个任务域和可替换加载插件。
struct ResourceServices
{
    engine::ThreadSystem owned_threads{{2}};
    engine::ThreadSystem& threads;
    engine::TaskSystem files{threads, {"TestFiles", 2}};
    engine::TaskSystem reads{threads, {"TestReads", 2}};
    engine::TaskSystem plans{threads, {"TestPlans", 2}};
    engine::ResourceSystem core;
    engine::plugins::resource_graph::ResourceGraphSystem loader{core, plans, {}};
    std::vector<engine::ResourceReference> pins;

    explicit ResourceServices(engine::ThreadSystem* external = nullptr)
        : threads(external ? *external : owned_threads)
    {
    }

    bool initialize(const engine::EngineConfig& config = {})
    {
        return (&threads == &owned_threads ? threads.initialize(config)
                                           : threads.is_initialized()) &&
               files.initialize(config) && reads.initialize(config) && plans.initialize(config) &&
               register_resource_codecs(core) && core.initialize(config) &&
               core.configure_async(files, reads, {}) && loader.initialize(config);
    }

    ~ResourceServices() noexcept
    {
        shutdown();
    }
    void shutdown() noexcept
    {
        pins.clear();
        loader.shutdown();
        core.shutdown();
        plans.shutdown();
        reads.shutdown();
        files.shutdown();
        if (&threads == &owned_threads)
        {
            threads.shutdown();
        }
    }
    bool tick()
    {
        return core.tick(0) && loader.tick(0);
    }

    bool load(std::string_view path, engine::ResourceReference& output)
    {
        engine::ResourceHandle handle;
        if (!loader.load(path, handle))
        {
            std::fprintf(stderr, "Resource fixture load %.*s: %.*s\n", int(path.size()),
                         path.data(), int(loader.last_error().size()), loader.last_error().data());
            return false;
        }
        engine::ResourceReference value;
        if (!value.adopt(&loader, handle))
        {
            (void)loader.release(handle);
            return false;
        }
        output = std::move(value);
        return true;
    }
    bool load(std::string_view path)
    {
        engine::ResourceReference value;
        if (!load(path, value))
        {
            std::fprintf(stderr, "Resource fixture load %.*s: %.*s\n",
                         static_cast<int>(path.size()), path.data(),
                         static_cast<int>(loader.last_error().size()), loader.last_error().data());
            return false;
        }
        pins.push_back(std::move(value));
        return true;
    }

    bool prepare_builtins()
    {
        const auto& entries = engine::builtin_resource_catalog().entries;
        std::map<std::string, engine::ResourceHandle, std::less<>> ready;
        while (ready.size() != entries.size())
        {
            bool progressed = false;
            for (const auto& entry : entries)
            {
                if (ready.contains(entry->guid))
                {
                    continue;
                }
                std::vector<engine::ResourceHandle> dependencies;
                bool complete = true;
                for (const auto& guid : entry->dependencies)
                {
                    const auto found = ready.find(guid);
                    if (found == ready.end())
                    {
                        complete = false;
                        break;
                    }
                    dependencies.push_back(found->second);
                }
                if (!complete)
                {
                    continue;
                }
                engine::ResourceHandle handle;
                if (!loader.acquire_loaded(entry->guid, handle))
                {
                    const auto* physical = core.catalog().find(entry->guid);
                    if (physical && physical->location)
                    {
                        if (!loader.load(entry->guid, handle))
                        {
                            return false;
                        }
                    }
                    else
                    {
                        std::unique_ptr<engine::Resource> candidate;
                        if (!engine::resource_detail::builtin_candidate(entry->guid, candidate) ||
                            !loader.load_memory(candidate, dependencies, handle))
                        {
                            std::fprintf(stderr, "Builtin fixture: %.*s\n",
                                         static_cast<int>(loader.last_error().size()),
                                         loader.last_error().data());
                            return false;
                        }
                    }
                }
                engine::ResourceReference value;
                if (!value.adopt(&loader, handle))
                {
                    (void)loader.release(handle);
                    return false;
                }
                ready.emplace(entry->guid, value.handle());
                pins.push_back(std::move(value));
                progressed = true;
            }
            if (!progressed)
            {
                return false;
            }
        }
        return true;
    }

    template <typename T> const typename T::Data* loaded(std::string_view path)
    {
        engine::ResourceReference value;
        if (!value.acquire(&loader, path))
        {
            return nullptr;
        }
        const auto* data = value.data<T>();
        if (data)
        {
            pins.push_back(std::move(value));
        }
        return data;
    }

    bool publish_and_load(const engine::schema::AssetCatalogDataT& catalog,
                          const engine::MountList& mounts)
    {
        if (!core.update_catalog(catalog, mounts) || !prepare_builtins())
        {
            return false;
        }
        for (const auto& entry : catalog.entries)
        {
            if (!entry->path.starts_with("builtin://") && !load(entry->guid))
            {
                return false;
            }
        }
        return true;
    }
};
} // namespace test_support
