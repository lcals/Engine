#include "plugins/runtime/resource_graph/resource_graph_system.hpp"

#include "runtime/resource/resource_system.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace engine::plugins::resource_graph
{
namespace
{

struct ResourceBundle
{
    std::vector<ResourcePtr> dependencies;
    ResourcePtr entity;

    ~ResourceBundle() noexcept
    {
        // 当前实体先释放，直接依赖按反序释放。排队避免深链 shared_ptr 析构递归。
        entity.reset();
        if (dependencies.empty())
        {
            return;
        }
        static thread_local bool draining = false;
        static thread_local std::vector<ResourcePtr> pending;
        for (auto& dependency : dependencies)
        {
            pending.push_back(std::move(dependency));
        }
        dependencies.clear();
        if (draining)
        {
            return;
        }
        draining = true;
        while (!pending.empty())
        {
            auto dependency = std::move(pending.back());
            pending.pop_back();
            dependency.reset();
        }
        draining = false;
    }
};

} // namespace

ResourceGraphSystem::ResourceGraphSystem(ResourceSystem& resources) noexcept : resources_(resources)
{
}

ResourceGraphSystem::~ResourceGraphSystem() noexcept
{
    shutdown();
}

bool ResourceGraphSystem::on_initialize(const EngineConfig&)
{
    if (!resources_.is_initialized())
    {
        last_error_ = "Resource graph requires an initialized resource core";
        return false;
    }
    generation_ = resources_.generation();
    return true;
}

void ResourceGraphSystem::on_shutdown() noexcept
{
    if (&resources_.loader() == this)
    {
        resources_.set_loader(nullptr);
    }
    loaded_.clear();
    generation_ = 0;
    last_error_.clear();
}

const std::string& ResourceGraphSystem::last_error() const noexcept
{
    return last_error_;
}

ResourcePtr ResourceGraphSystem::find(std::string_view guid) const noexcept
{
    if (!is_initialized() || !resources_.is_initialized() || generation_ != resources_.generation())
    {
        return {};
    }
    const auto found = loaded_.find(guid);
    return found != loaded_.end() ? found->second.lock() : ResourcePtr{};
}

bool ResourceGraphSystem::load(std::string_view guid, ResourcePtr& output)
{
    // 入参可借用 output 元数据，先复制标识再释放旧引用。
    const std::string identity(guid);
    output.reset();
    last_error_.clear();
    if (!is_initialized() || !resources_.is_initialized() || !GuidMap::valid_guid(identity) ||
        resources_.catalog().find(identity) == nullptr)
    {
        last_error_ = "Unknown resource or resource graph not initialized: " + identity;
        return false;
    }
    if (generation_ != resources_.generation())
    {
        // 旧 Bundle 由调用方继续持有；弱索引只呈现当前目录代际。
        loaded_.clear();
        generation_ = resources_.generation();
    }
    std::erase_if(loaded_, [](const auto& item) { return item.second.expired(); });

    struct Pending
    {
        std::string guid;
        std::size_t next_dependency = 0;
    };
    std::vector<Pending> pending{{identity, 0}};
    std::map<std::string, ResourcePtr, std::less<>> retained;
    std::map<std::string, bool, std::less<>> visiting;
    visiting.emplace(identity, true);
    while (!pending.empty())
    {
        if (generation_ != resources_.generation())
        {
            last_error_ = "Resource catalog changed during graph load";
            return false;
        }
        auto& current = pending.back();
        if (auto cached = find(current.guid))
        {
            retained.emplace(current.guid, std::move(cached));
            visiting.erase(current.guid);
            pending.pop_back();
            continue;
        }
        const auto* entry = resources_.catalog().find(current.guid);
        if (entry == nullptr)
        {
            last_error_ = "Missing necessary resource: " + current.guid;
            return false;
        }
        if (current.next_dependency < entry->dependencies.size())
        {
            const auto& dependency = entry->dependencies[current.next_dependency++];
            if (retained.contains(dependency))
            {
                continue;
            }
            if (!visiting.emplace(dependency, true).second)
            {
                last_error_ = "Cyclic resource dependency: " + dependency;
                return false;
            }
            pending.push_back({dependency, 0});
            continue;
        }

        auto bundle = std::make_shared<ResourceBundle>();
        for (const auto& dependency : entry->dependencies)
        {
            const auto found = retained.find(dependency);
            if (found == retained.end())
            {
                last_error_ = "Necessary resource was not retained: " + dependency;
                return false;
            }
            bundle->dependencies.push_back(found->second);
        }
        if (!resources_.load(current.guid, bundle->entity))
        {
            last_error_ = resources_.last_error();
            return false;
        }
        if (!resources_.validate_dependencies(*bundle->entity, bundle->dependencies))
        {
            last_error_ = "Resource references do not match their definitions: " + entry->path;
            return false;
        }
        ResourcePtr resource(bundle, bundle->entity.get());
        retained.emplace(current.guid, std::move(resource));
        visiting.erase(current.guid);
        pending.pop_back();
    }
    // 整个闭包成功后才发布弱索引，失败不留下部分驻留记录。
    for (const auto& [current, resource] : retained)
    {
        loaded_[current] = resource;
    }
    output = retained.find(identity)->second;
    return true;
}

} // namespace engine::plugins::resource_graph
