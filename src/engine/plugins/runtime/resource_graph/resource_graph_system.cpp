#include "plugins/runtime/resource_graph/resource_graph_system.hpp"

#include "runtime/resource/resource_system.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
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

// 只保存既有 DFS 的计划与所有权状态，由调用方在同一线程显式推进。
class GraphResourceLoad final : public ResourceLoad
{
public:
    GraphResourceLoad(ResourceGraphSystem& graph, std::string guid)
        : graph_(graph), loader_(&graph.resources_.loader()), generation_(graph.generation_),
          guid_(std::move(guid))
    {
    }

    [[nodiscard]] bool initialize()
    {
        struct Pending
        {
            std::string guid;
            std::size_t next_dependency = 0;
        };
        std::vector<Pending> pending{{guid_, 0}};
        // false 表示仍在 DFS 路径中，true 表示已进入唯一后序计划。
        std::map<std::string, bool, std::less<>> visited;
        visited.emplace(guid_, false);
        while (!pending.empty())
        {
            auto& current = pending.back();
            const auto* entry = graph_.resources_.catalog().find(current.guid);
            if (entry == nullptr)
            {
                fail("Missing necessary resource: " + current.guid);
                return false;
            }
            if (current.next_dependency < entry->dependencies.size())
            {
                const auto& dependency = entry->dependencies[current.next_dependency++];
                const auto [found, inserted] = visited.emplace(dependency, false);
                if (!inserted)
                {
                    if (!found->second)
                    {
                        fail("Cyclic resource dependency: " + dependency);
                        return false;
                    }
                    continue;
                }
                pending.push_back({dependency, 0});
                continue;
            }
            if (plan_.size() == std::numeric_limits<std::uint32_t>::max())
            {
                fail("Resource dependency closure exceeds progress counter capacity");
                return false;
            }
            plan_.push_back(current.guid);
            visited.find(current.guid)->second = true;
            pending.pop_back();
        }
        progress_.total = static_cast<std::uint32_t>(plan_.size());
        return true;
    }

    ResourceLoadStatus step(std::uint32_t resource_budget, ResourceLoadProgress& output) override
    {
        if (status_ == ResourceLoadStatus::loading && !current_generation())
        {
            fail("Resource catalog or loader changed during graph load");
        }
        while (status_ == ResourceLoadStatus::loading && resource_budget != 0)
        {
            const auto& identity = plan_[progress_.completed];
            Retained retained;
            retained.resource = graph_.find(identity);
            if (!retained.resource)
            {
                const auto* entry = graph_.resources_.catalog().find(identity);
                if (entry == nullptr)
                {
                    fail("Missing necessary resource: " + identity);
                    break;
                }
                retained.candidate = std::make_shared<ResourceBundle>();
                for (const auto& dependency : entry->dependencies)
                {
                    const auto found = retained_.find(dependency);
                    if (found == retained_.end())
                    {
                        fail("Necessary resource was not retained: " + dependency);
                        break;
                    }
                    retained.candidate->dependencies.push_back(found->second.resource);
                }
                if (status_ != ResourceLoadStatus::loading)
                {
                    break;
                }
                if (!graph_.resources_.load(identity, retained.candidate->entity))
                {
                    fail(graph_.resources_.last_error());
                    break;
                }
                if (!graph_.resources_.validate_dependencies(*retained.candidate->entity,
                                                             retained.candidate->dependencies))
                {
                    fail("Resource references do not match their definitions: " + entry->path);
                    break;
                }
                retained.resource =
                    ResourcePtr(retained.candidate, retained.candidate->entity.get());
            }
            retained_.emplace(identity, std::move(retained));
            ++progress_.completed;
            --resource_budget;
            if (!current_generation())
            {
                fail("Resource catalog or loader changed during graph load");
                break;
            }
            if (progress_.completed == progress_.total)
            {
                publish();
            }
        }
        output = progress_;
        return status_;
    }

    ResourcePtr resource() const noexcept override
    {
        return root_;
    }

    std::string_view last_error() const noexcept override
    {
        return last_error_;
    }

private:
    struct Retained
    {
        ResourcePtr resource;
        std::shared_ptr<ResourceBundle> candidate;
    };

    [[nodiscard]] bool current_generation() const noexcept
    {
        return graph_.is_initialized() && graph_.resources_.is_initialized() &&
               &graph_.resources_.loader() == loader_ && graph_.generation_ == generation_ &&
               graph_.resources_.generation() == generation_;
    }

    void fail(std::string_view error)
    {
        // 错误可能借用实体、目录或核心缓冲，释放候选前先保存。
        last_error_ = error;
        status_ = ResourceLoadStatus::failed;
        root_.reset();
        retained_.clear();
        plan_.clear();
    }

    void publish()
    {
        // 其他同步调用可能已在两次 step 间发布同代对象；优先复用它们，
        // 再按后序把尚未发布的私有父项接到最终共享依赖上。
        for (const auto& identity : plan_)
        {
            auto& retained = retained_.find(identity)->second;
            if (auto cached = graph_.find(identity))
            {
                retained.resource = std::move(cached);
                retained.candidate.reset();
            }
            else if (retained.candidate)
            {
                const auto* entry = graph_.resources_.catalog().find(identity);
                assert(entry != nullptr);
                for (std::size_t index = 0; index < entry->dependencies.size(); ++index)
                {
                    const auto dependency = retained_.find(entry->dependencies[index]);
                    assert(dependency != retained_.end());
                    retained.candidate->dependencies[index] = dependency->second.resource;
                }
            }
            graph_.loaded_[identity] = retained.resource;
        }
        root_ = retained_.find(guid_)->second.resource;
        status_ = ResourceLoadStatus::ready;
        retained_.clear();
        plan_.clear();
    }

    ResourceGraphSystem& graph_;
    ResourceLoader* loader_ = nullptr;
    std::uint64_t generation_ = 0;
    std::string guid_;
    std::vector<std::string> plan_;
    std::map<std::string, Retained, std::less<>> retained_;
    ResourcePtr root_;
    ResourceLoadProgress progress_;
    ResourceLoadStatus status_ = ResourceLoadStatus::loading;
    std::string last_error_;
};

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
    if (auto cached = find(identity))
    {
        output = std::move(cached);
        return true;
    }
    std::unique_ptr<ResourceLoad> operation;
    if (!begin_load(identity, operation))
    {
        return false;
    }
    ResourceLoadProgress progress;
    const auto status = operation->step(std::numeric_limits<std::uint32_t>::max(), progress);
    if (status != ResourceLoadStatus::ready)
    {
        last_error_ = operation->last_error();
        return false;
    }
    output = operation->resource();
    return true;
}

bool ResourceGraphSystem::begin_load(std::string_view guid, std::unique_ptr<ResourceLoad>& output)
{
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
    auto operation = std::make_unique<GraphResourceLoad>(*this, identity);
    if (!operation->initialize())
    {
        last_error_ = operation->last_error();
        return false;
    }
    output = std::move(operation);
    return true;
}

} // namespace engine::plugins::resource_graph
