#include "plugins/runtime/resource_graph/resource_graph_system.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/task/task_system.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace engine::plugins::resource_graph
{
namespace
{
bool empty(const ResourceHandle& value) noexcept
{
    return value.owner == 0 && value.id == 0 && value.generation == 0;
}

using Key = std::pair<std::uint64_t, std::string>;

struct PlanWork
{
    ResourceCatalog catalog;
    std::map<std::string, schema::AssetEntryT, std::less<>> memory;
    std::string root;
    std::vector<std::string> order;
    std::atomic<bool> cancelled{false};
    ResourceError error = ResourceError::none;
    std::string message;

    const schema::AssetEntryT* find(std::string_view guid) const
    {
        const auto local = memory.find(guid);
        return local == memory.end() ? catalog.find(guid) : &local->second;
    }

    bool execute()
    {
        struct Cursor
        {
            const schema::AssetEntryT* entry;
            std::size_t next = 0;
        };
        const auto* entry = find(root);
        if (!entry)
        {
            error = ResourceError::not_found;
            message = "Unknown resource: " + root;
            return false;
        }
        std::map<std::string_view, unsigned> visited;
        std::vector<Cursor> stack{{entry}};
        visited.emplace(entry->guid, 1);
        while (!stack.empty())
        {
            if (cancelled.load(std::memory_order_relaxed))
            {
                error = ResourceError::cancelled;
                return false;
            }
            auto& cursor = stack.back();
            if (cursor.next == cursor.entry->dependencies.size())
            {
                if (order.size() == std::numeric_limits<std::uint32_t>::max())
                {
                    error = ResourceError::budget_exceeded;
                    message = "Resource closure is too large";
                    return false;
                }
                visited[cursor.entry->guid] = 2;
                order.push_back(cursor.entry->guid);
                stack.pop_back();
                continue;
            }
            const auto& guid = cursor.entry->dependencies[cursor.next++];
            const auto* child = find(guid);
            if (!child)
            {
                error = ResourceError::dependency_failed;
                message = "Missing declared dependency: " + guid;
                return false;
            }
            auto [mark, inserted] = visited.emplace(child->guid, 1);
            if (inserted)
            {
                stack.push_back({child});
            }
            else if (mark->second == 1)
            {
                error = ResourceError::dependency_failed;
                message = "Resource dependency cycle: " + guid;
                return false;
            }
        }
        return true;
    }
};
} // namespace

struct ResourceGraphSystem::State : std::enable_shared_from_this<State>
{
    enum class Phase
    {
        queued,
        reading,
        ready,
        failed
    };
    struct Record
    {
        Key key;
        schema::AssetEntryT info;
        Phase phase = Phase::queued;
        bool memory = false;
        bool admitted = false;
        std::unique_ptr<Resource> value;
        std::unique_ptr<Resource> candidate;
        std::unique_ptr<ResourceRead> read;
        std::vector<Record*> dependencies;
        std::uint64_t acquisitions = 0;
        std::uint64_t dependency_references = 0;
        std::uint64_t request_references = 0;
        std::uint64_t task_references = 0;
        ResourceError error = ResourceError::none;
        std::string message;
    };
    struct LoadState
    {
        std::weak_ptr<State> owner;
        std::uint64_t generation = 0;
        ResourceLoadStatus status = ResourceLoadStatus::loading;
        ResourceLoadProgress progress;
        ResourceError error = ResourceError::none;
        std::string message;
        ResourceHandle result;
        std::shared_ptr<PlanWork> plan;
        std::future<bool> planning;
        bool planned = false;
        std::vector<Record*> pins;
    };

    ResourceSystem& resources;
    TaskSystem& tasks;
    ResourceGraphConfig config;
    const std::uint64_t owner;
    bool active = false;
    std::uint64_t generation = 0;
    std::uint64_t last_acquisition = 0;
    std::map<Key, std::unique_ptr<Record>> records;
    std::map<Key, Record*> paths;
    std::map<std::uint64_t, Record*> acquisitions;
    std::vector<std::shared_ptr<LoadState>> operations;
    ResourceError error = ResourceError::none;
    std::string message;

    State(ResourceSystem& resources, TaskSystem& tasks, ResourceGraphConfig config,
          std::uint64_t owner)
        : resources(resources), tasks(tasks), config(config), owner(owner)
    {
    }

    bool fail(ResourceError value, std::string text)
    {
        error = value;
        message = std::move(text);
        return false;
    }
    bool success()
    {
        error = ResourceError::none;
        message.clear();
        return true;
    }

    bool can_acquire()
    {
        return last_acquisition < std::numeric_limits<std::uint64_t>::max() ||
               fail(ResourceError::budget_exceeded, "Resource acquisition identities exhausted");
    }
    bool add_count(std::uint64_t& count)
    {
        if (count == std::numeric_limits<std::uint64_t>::max())
        {
            return fail(ResourceError::budget_exceeded, "Resource reference count overflow");
        }
        ++count;
        return true;
    }
    Record* lookup(const ResourceHandle& handle) const noexcept
    {
        if (!active || handle.owner != owner || handle.generation != 1 || handle.id == 0)
        {
            return nullptr;
        }
        const auto found = acquisitions.find(handle.id);
        return found == acquisitions.end() ? nullptr : found->second;
    }

    void collect(Record* record) noexcept
    {
        // Iterative reverse release avoids stack growth for deep resource closures.
        std::vector<Record*> pending{record};
        while (!pending.empty())
        {
            auto* item = pending.back();
            pending.pop_back();
            if (item->acquisitions || item->dependency_references || item->request_references)
            {
                continue;
            }
            if (item->read)
            {
                (void)item->read->cancel();
                item->read.reset();
            }
            item->task_references = 0;
            auto dependencies = std::move(item->dependencies);
            paths.erase({item->key.first, item->info.path});
            // Destroy the item before withdrawing its dependency ownership.
            const auto key = item->key;
            records.erase(key);
            for (auto* dependency : dependencies)
            {
                --dependency->dependency_references;
                if (!dependency->dependency_references && !dependency->acquisitions &&
                    !dependency->request_references)
                {
                    pending.push_back(dependency);
                }
            }
        }
    }

    bool acquire(Record& record, ResourceHandle& output)
    {
        if (!empty(output))
        {
            return fail(ResourceError::busy, "Resource handle output must be empty");
        }
        if (!can_acquire() || !add_count(record.acquisitions))
        {
            return false;
        }
        const auto id = ++last_acquisition;
        acquisitions.emplace(id, &record);
        output = {owner, id, 1};
        return success();
    }

    bool release(ResourceHandle& handle) noexcept
    {
        if (empty(handle))
        {
            return success();
        }
        auto* record = lookup(handle);
        const auto id = handle.id;
        handle = {};
        if (!record)
        {
            error = ResourceError::invalid_handle;
            message.clear();
            return false;
        }
        acquisitions.erase(id);
        --record->acquisitions;
        collect(record);
        return success();
    }

    void unpin(LoadState& operation) noexcept
    {
        // Pins are independent counts, so each record remains alive until its own pin is removed.
        auto pins = std::move(operation.pins);
        for (auto* record : pins)
        {
            --record->request_references;
            collect(record);
        }
    }
    void finish(LoadState& operation, ResourceError value, std::string text)
    {
        operation.status = ResourceLoadStatus::failed;
        operation.error = value;
        operation.message = std::move(text);
        if (operation.plan)
        {
            operation.plan->cancelled.store(true, std::memory_order_relaxed);
        }
        unpin(operation);
    }
    bool cancel(LoadState& operation) noexcept
    {
        if (operation.status == ResourceLoadStatus::delivered ||
            operation.status == ResourceLoadStatus::failed)
        {
            return false;
        }
        if (operation.status == ResourceLoadStatus::cancelled)
        {
            return true;
        }
        if (operation.plan)
        {
            operation.plan->cancelled.store(true, std::memory_order_relaxed);
        }
        if (!empty(operation.result))
        {
            (void)release(operation.result);
        }
        unpin(operation);
        operation.status = ResourceLoadStatus::cancelled;
        operation.error = ResourceError::cancelled;
        return true;
    }

    void refresh_generation()
    {
        if (!active || generation == resources.generation())
        {
            return;
        }
        generation = resources.generation();
        for (const auto& operation : operations)
        {
            if (operation->status == ResourceLoadStatus::loading)
            {
                finish(*operation, ResourceError::generation_changed,
                       "Resource catalog changed during loading");
            }
        }
    }
    bool command()
    {
        if (!active || !resources.is_initialized() || !tasks.is_initialized())
        {
            return fail(ResourceError::unavailable, "Resource loader is unavailable");
        }
        refresh_generation();
        return success();
    }
    bool resolve(std::string_view path_or_guid, std::string& guid)
    {
        const auto found = paths.find({generation, std::string(path_or_guid)});
        if (found != paths.end())
        {
            guid = found->second->info.guid;
            return true;
        }
        if (GuidMap::valid_guid(path_or_guid))
        {
            guid = path_or_guid;
            if (records.contains({generation, guid}) || resources.catalog().find(guid))
            {
                return true;
            }
        }
        else if (resources.catalog().identities().path_to_guid(path_or_guid, guid))
        {
            return true;
        }
        return fail(ResourceError::not_found, "Unknown resource: " + std::string(path_or_guid));
    }
    std::shared_ptr<PlanWork> plan(std::string guid)
    {
        auto result = std::make_shared<PlanWork>();
        result->catalog = resources.catalog();
        result->root = std::move(guid);
        for (const auto& [key, record] : records)
        {
            if (key.first == generation && record->memory)
            {
                result->memory.emplace(record->info.guid, record->info);
            }
        }
        return result;
    }
    Record* make_record(const schema::AssetEntryT& info)
    {
        const Key key{generation, info.guid};
        const auto existing = records.find(key);
        if (existing != records.end())
        {
            return existing->second.get();
        }
        auto item = std::make_unique<Record>();
        item->key = key;
        item->info = info;
        auto* record = item.get();
        records.emplace(key, std::move(item));
        paths.emplace(Key{generation, info.path}, record);
        return record;
    }
    std::size_t pending_count() const
    {
        return static_cast<std::size_t>(
            std::count_if(records.begin(), records.end(), [](const auto& item)
                          { return item.second->admitted && item.second->phase != Phase::ready; }));
    }
    bool pin(LoadState& operation, Record& record)
    {
        if (std::find(operation.pins.begin(), operation.pins.end(), &record) !=
            operation.pins.end())
        {
            return true;
        }
        if (!add_count(record.request_references))
        {
            return false;
        }
        operation.pins.push_back(&record);
        return true;
    }
    bool publish(Record& record, std::unique_ptr<Resource>& candidate)
    {
        std::vector<Record*> dependencies;
        std::vector<const Resource*> values;
        for (const auto& guid : record.info.dependencies)
        {
            const auto found = records.find({record.key.first, guid});
            if (found == records.end() || found->second->phase != Phase::ready)
            {
                return fail(ResourceError::not_ready, "Resource dependency is not ready");
            }
            dependencies.push_back(found->second.get());
            values.push_back(found->second->value.get());
            if (found->second->dependency_references == std::numeric_limits<std::uint64_t>::max())
            {
                return fail(ResourceError::budget_exceeded, "Dependency reference count overflow");
            }
        }
        ResourceError status;
        std::string detail;
        if (!resources.validate_dependencies(*candidate, values, status, detail))
        {
            return fail(status, std::move(detail));
        }
        for (auto* dependency : dependencies)
        {
            ++dependency->dependency_references;
        }
        record.dependencies = std::move(dependencies);
        record.value = std::move(candidate);
        record.phase = Phase::ready;
        return true;
    }
    void fail_record(Record& record, ResourceError value, std::string text)
    {
        record.phase = Phase::failed;
        record.error = value;
        record.message = std::move(text);
        record.candidate.reset();
        record.read.reset();
        record.task_references = 0;
    }

    bool memory_inputs(const Resource& candidate, std::span<const ResourceHandle> handles,
                       std::vector<Record*>& dependencies)
    {
        const auto& info = candidate.info();
        if (!GuidMap::valid_guid(info.guid) || !GuidMap::valid_path(info.path) ||
            info.path.starts_with("editor://") || info.location || info.kind != candidate.kind())
        {
            return fail(ResourceError::invalid_data, "Invalid memory resource identity");
        }
        if (records.contains({generation, info.guid}) || paths.contains({generation, info.path}))
        {
            return fail(ResourceError::identity_conflict,
                        "Resource identity is already loaded or pending");
        }
        std::set<std::string_view> unique;
        for (const auto& handle : handles)
        {
            auto* dependency = lookup(handle);
            if (!dependency)
            {
                return fail(ResourceError::invalid_handle, "Invalid memory dependency handle");
            }
            if (dependency->key.first != generation)
            {
                return fail(ResourceError::generation_changed,
                            "Memory dependencies belong to another generation");
            }
            if (!unique.insert(dependency->info.guid).second ||
                std::find(info.dependencies.begin(), info.dependencies.end(),
                          dependency->info.guid) == info.dependencies.end())
            {
                return fail(ResourceError::dependency_failed,
                            "Duplicate or undeclared memory dependency");
            }
            dependencies.push_back(dependency);
        }
        if (handles.size() != info.dependencies.size())
        {
            return fail(ResourceError::dependency_failed,
                        "Memory candidate requires every direct dependency");
        }
        return true;
    }

    bool begin(std::string_view input, std::unique_ptr<ResourceLoad>& output);
    bool begin_memory(std::unique_ptr<Resource>& candidate, std::span<const ResourceHandle> handles,
                      std::unique_ptr<ResourceLoad>& output);
    void advance();
};

class GraphResourceLoad final : public ResourceLoad
{
    using State = ResourceGraphSystem::State;

public:
    explicit GraphResourceLoad(std::shared_ptr<State::LoadState> state) : state_(std::move(state))
    {
    }
    ~GraphResourceLoad() noexcept override
    {
        (void)cancel();
    }
    ResourceLoadStatus status(ResourceLoadProgress& output) const noexcept override
    {
        output = state_->progress;
        return state_->status;
    }
    bool take(ResourceHandle& output) override
    {
        if (!empty(output) || state_->status != ResourceLoadStatus::ready)
        {
            return false;
        }
        const auto owner = state_->owner.lock();
        if (!owner || !owner->lookup(state_->result))
        {
            return false;
        }
        output = std::exchange(state_->result, {});
        state_->status = ResourceLoadStatus::delivered;
        return true;
    }
    bool cancel() noexcept override
    {
        const auto owner = state_->owner.lock();
        if (owner)
        {
            return owner->cancel(*state_);
        }
        return state_->status == ResourceLoadStatus::cancelled;
    }
    ResourceError error() const noexcept override
    {
        return state_->error;
    }
    std::string_view last_error() const noexcept override
    {
        return state_->error == ResourceError::cancelled
                   ? std::string_view("Resource load cancelled")
                   : std::string_view(state_->message);
    }

private:
    std::shared_ptr<State::LoadState> state_;
};

bool ResourceGraphSystem::State::begin(std::string_view input,
                                       std::unique_ptr<ResourceLoad>& output)
{
    if (!command())
    {
        return false;
    }
    if (output)
    {
        return fail(ResourceError::busy, "Resource operation output must be empty");
    }
    const auto active_count = std::count_if(operations.begin(), operations.end(),
                                            [](const auto& op)
                                            {
                                                return op->status == ResourceLoadStatus::loading ||
                                                       op->status == ResourceLoadStatus::ready;
                                            });
    if (active_count >= config.max_requests)
    {
        return fail(ResourceError::busy, "Resource request budget is full");
    }
    std::string guid;
    if (!resolve(input, guid))
    {
        return false;
    }
    auto operation = std::make_shared<LoadState>();
    operation->owner = shared_from_this();
    operation->generation = generation;
    const auto existing = records.find({generation, guid});
    if (existing != records.end() && existing->second->phase == Phase::ready)
    {
        if (!acquire(*existing->second, operation->result))
        {
            return false;
        }
        std::set<const Record*> closure;
        std::vector<const Record*> pending{existing->second.get()};
        while (!pending.empty())
        {
            const auto* item = pending.back();
            pending.pop_back();
            if (closure.insert(item).second)
            {
                pending.insert(pending.end(), item->dependencies.begin(), item->dependencies.end());
            }
        }
        if (closure.size() > std::numeric_limits<std::uint32_t>::max())
        {
            (void)release(operation->result);
            return fail(ResourceError::budget_exceeded, "Resource closure is too large");
        }
        operation->progress.completed = operation->progress.total =
            static_cast<std::uint32_t>(closure.size());
        operation->status = ResourceLoadStatus::ready;
    }
    else
    {
        operation->plan = plan(guid);
        const auto work = operation->plan;
        operation->planning =
            tasks.submit("ResourceGraph.plan", [work]() { return work->execute(); });
        if (!operation->planning.valid())
        {
            return fail(ResourceError::busy, "Resource planning task domain is full");
        }
        auto* record =
            existing != records.end() ? existing->second.get() : make_record(*work->find(guid));
        if (!pin(*operation, *record))
        {
            work->cancelled.store(true, std::memory_order_relaxed);
            collect(record);
            return false;
        }
    }
    operations.push_back(operation);
    output = std::make_unique<GraphResourceLoad>(operation);
    return success();
}

bool ResourceGraphSystem::State::begin_memory(std::unique_ptr<Resource>& candidate,
                                              std::span<const ResourceHandle> handles,
                                              std::unique_ptr<ResourceLoad>& output)
{
    if (!command())
    {
        return false;
    }
    if (output)
    {
        return fail(ResourceError::busy, "Resource operation output must be empty");
    }
    if (!candidate)
    {
        return fail(ResourceError::invalid_data, "Memory resource is empty");
    }
    const auto count = std::count_if(operations.begin(), operations.end(),
                                     [](const auto& op)
                                     {
                                         return op->status == ResourceLoadStatus::loading ||
                                                op->status == ResourceLoadStatus::ready;
                                     });
    if (count >= config.max_requests)
    {
        return fail(ResourceError::busy, "Resource request budget is full");
    }
    std::vector<Record*> dependencies;
    if (!memory_inputs(*candidate, handles, dependencies))
    {
        return false;
    }
    auto operation = std::make_shared<LoadState>();
    operation->owner = shared_from_this();
    operation->generation = generation;
    operation->plan = plan(candidate->info().guid);
    operation->plan->memory.emplace(candidate->info().guid, candidate->info());
    const auto work = operation->plan;
    operation->planning =
        tasks.submit("ResourceGraph.memory-plan", [work]() { return work->execute(); });
    if (!operation->planning.valid())
    {
        return fail(ResourceError::busy, "Resource planning task domain is full");
    }
    for (auto* dependency : dependencies)
    {
        if (!pin(*operation, *dependency))
        {
            work->cancelled.store(true, std::memory_order_relaxed);
            unpin(*operation);
            return false;
        }
    }
    auto* record = make_record(candidate->info());
    if (!pin(*operation, *record))
    {
        work->cancelled.store(true, std::memory_order_relaxed);
        unpin(*operation);
        collect(record);
        return false;
    }
    record->memory = true;
    record->candidate = std::move(candidate);
    operations.push_back(operation);
    output = std::make_unique<GraphResourceLoad>(operation);
    return success();
}

void ResourceGraphSystem::State::advance()
{
    refresh_generation();
    std::uint32_t budget = config.completion_budget;
    for (const auto& operation : operations)
    {
        if (budget == 0)
        {
            break;
        }
        if (operation->status != ResourceLoadStatus::loading || operation->planned)
        {
            continue;
        }
        if (operation->planning.valid())
        {
            if (operation->planning.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                continue;
            }
            if (!operation->planning.get())
            {
                finish(*operation, operation->plan->error, operation->plan->message);
                --budget;
                continue;
            }
        }
        auto& plan = *operation->plan;
        std::size_t missing = 0;
        std::size_t needed = 0;
        for (const auto& guid : plan.order)
        {
            const auto found = records.find({generation, guid});
            if (found == records.end())
            {
                ++missing;
                ++needed;
            }
            else if (found->second->phase != Phase::ready)
            {
                ++needed;
                missing += !found->second->admitted;
            }
        }
        if (needed > config.max_pending_resources)
        {
            finish(*operation, ResourceError::budget_exceeded,
                   "Resource closure exceeds pending-resource budget");
            --budget;
            continue;
        }
        if (missing > config.max_pending_resources -
                          std::min<std::size_t>(pending_count(), config.max_pending_resources))
        {
            continue;
        }
        bool pinned = true;
        for (const auto& guid : plan.order)
        {
            auto* record = make_record(*plan.find(guid));
            if (!pin(*operation, *record))
            {
                collect(record);
                pinned = false;
                break;
            }
        }
        if (!pinned)
        {
            finish(*operation, error, message);
            --budget;
            continue;
        }
        for (auto* record : operation->pins)
        {
            record->admitted = true;
        }
        operation->planned = true;
        operation->progress.total = static_cast<std::uint32_t>(plan.order.size());
        --budget;
    }

    // Every record is shared across all requests; I/O and dependency publication happen once.
    budget = config.completion_budget;
    for (auto& [key, record_ptr] : records)
    {
        if (budget == 0)
        {
            break;
        }
        auto& record = *record_ptr;
        if (key.first != generation || record.phase == Phase::ready ||
            record.phase == Phase::failed)
        {
            continue;
        }
        const bool planned = std::any_of(
            operations.begin(), operations.end(),
            [&record](const auto& op)
            {
                return op->status == ResourceLoadStatus::loading && op->planned &&
                       std::find(op->pins.begin(), op->pins.end(), &record) != op->pins.end();
            });
        if (!planned)
        {
            continue;
        }
        bool ready = true;
        for (const auto& guid : record.info.dependencies)
        {
            const auto dependency = records.find({generation, guid});
            if (dependency == records.end() || dependency->second->phase != Phase::ready)
            {
                ready = false;
                if (dependency != records.end() && dependency->second->phase == Phase::failed)
                {
                    fail_record(record, ResourceError::dependency_failed,
                                dependency->second->message);
                    --budget;
                }
                break;
            }
        }
        if (!ready)
        {
            continue;
        }
        if (record.phase == Phase::queued)
        {
            const bool accepted = record.memory
                                      ? resources.begin_read_memory(record.candidate, record.read)
                                      : resources.begin_read(record.info.guid, record.read);
            if (!accepted)
            {
                if (resources.error() != ResourceError::busy)
                {
                    fail_record(record, resources.error(), std::string(resources.last_error()));
                    --budget;
                }
                continue;
            }
            record.phase = Phase::reading;
            record.task_references = 1;
            --budget;
        }
        if (record.phase != Phase::reading || record.read->status() == ResourceReadStatus::reading)
        {
            continue;
        }
        if (record.read->status() != ResourceReadStatus::ready ||
            !record.read->take(record.candidate))
        {
            fail_record(record, record.read->error(), std::string(record.read->last_error()));
        }
        else
        {
            record.read.reset();
            record.task_references = 0;
            if (!publish(record, record.candidate))
            {
                fail_record(record, error, message);
            }
        }
        if (budget)
        {
            --budget;
        }
    }

    budget = config.completion_budget;
    for (const auto& operation : operations)
    {
        if (budget == 0)
        {
            break;
        }
        if (operation->status != ResourceLoadStatus::loading || !operation->planned)
        {
            continue;
        }
        operation->progress.completed = 0;
        Record* failed = nullptr;
        for (auto* record : operation->pins)
        {
            operation->progress.completed += record->phase == Phase::ready;
            if (record->phase == Phase::failed)
            {
                failed = record;
                break;
            }
        }
        if (failed)
        {
            finish(*operation, failed->error, failed->message);
            --budget;
            continue;
        }
        if (operation->progress.completed == operation->progress.total)
        {
            auto& root = *records.at({generation, operation->plan->root});
            if (!acquire(root, operation->result))
            {
                finish(*operation, error, message);
            }
            else
            {
                operation->status = ResourceLoadStatus::ready;
                unpin(*operation);
            }
            --budget;
        }
    }
    std::erase_if(operations,
                  [](const auto& operation)
                  {
                      return operation->status == ResourceLoadStatus::delivered ||
                             operation->status == ResourceLoadStatus::failed ||
                             operation->status == ResourceLoadStatus::cancelled;
                  });
}

ResourceGraphSystem::ResourceGraphSystem(ResourceSystem& resources, TaskSystem& tasks,
                                         const ResourceGraphConfig& config)
    : state_(std::make_shared<State>(resources, tasks, config, owner_id()))
{
}
ResourceGraphSystem::~ResourceGraphSystem() noexcept
{
    shutdown();
}

bool ResourceGraphSystem::on_initialize(const EngineConfig&)
{
    auto& state = *state_;
    if (state.owner == 0 || !state.resources.is_initialized() || !state.tasks.is_initialized() ||
        state.config.max_requests == 0 || state.config.max_pending_resources == 0 ||
        state.config.completion_budget == 0)
    {
        return state.fail(ResourceError::unavailable, "Invalid resource graph services or budgets");
    }
    state.active = true;
    state.generation = state.resources.generation();
    return state.success();
}
bool ResourceGraphSystem::on_tick(double)
{
    if (!state_->command())
    {
        return false;
    }
    state_->advance();
    return true;
}
void ResourceGraphSystem::on_shutdown() noexcept
{
    auto& state = *state_;
    for (const auto& operation : state.operations)
    {
        (void)state.cancel(*operation);
    }
    state.operations.clear();
    while (!state.acquisitions.empty())
    {
        ResourceHandle handle{state.owner, state.acquisitions.begin()->first, 1};
        (void)state.release(handle);
    }
    state.active = false;
    (void)state.tasks.wait();
    state.paths.clear();
    state.records.clear();
}

bool ResourceGraphSystem::load(std::string_view input, ResourceHandle& output)
{
    auto& state = *state_;
    if (!state.command())
    {
        return false;
    }
    if (!empty(output))
    {
        return state.fail(ResourceError::busy, "Resource handle output must be empty");
    }
    if (!state.can_acquire())
    {
        return false;
    }
    std::string guid;
    if (!state.resolve(input, guid))
    {
        return false;
    }
    const auto current = state.records.find({state.generation, guid});
    if (current != state.records.end() && current->second->phase == State::Phase::ready)
    {
        return state.acquire(*current->second, output);
    }
    const auto plan = state.plan(guid);
    if (!plan->execute())
    {
        return state.fail(plan->error, plan->message);
    }
    for (const auto& identity : plan->order)
    {
        const auto existing = state.records.find({state.generation, identity});
        if (existing != state.records.end() && existing->second->phase != State::Phase::ready)
        {
            return state.fail(ResourceError::busy,
                              "Required resource has asynchronous work in flight");
        }
    }
    State::LoadState scope;
    for (const auto& identity : plan->order)
    {
        auto* record = state.make_record(*plan->find(identity));
        if (!state.pin(scope, *record))
        {
            state.collect(record);
            state.unpin(scope);
            return false;
        }
        if (record->phase == State::Phase::ready)
        {
            continue;
        }
        std::unique_ptr<Resource> value;
        if (!state.resources.read(identity, value))
        {
            state.fail(state.resources.error(), std::string(state.resources.last_error()));
            state.unpin(scope);
            return false;
        }
        if (!state.publish(*record, value))
        {
            state.unpin(scope);
            return false;
        }
    }
    const bool success = state.acquire(*state.records.at({state.generation, guid}), output);
    state.unpin(scope);
    return success;
}
bool ResourceGraphSystem::begin_load(std::string_view input, std::unique_ptr<ResourceLoad>& output)
{
    return state_->begin(input, output);
}

bool ResourceGraphSystem::load_memory(std::unique_ptr<Resource>& candidate,
                                      std::span<const ResourceHandle> handles,
                                      ResourceHandle& output)
{
    auto& state = *state_;
    if (!state.command())
    {
        return false;
    }
    if (!empty(output))
    {
        return state.fail(ResourceError::busy, "Resource handle output must be empty");
    }
    if (!candidate)
    {
        return state.fail(ResourceError::invalid_data, "Memory resource is empty");
    }
    if (!state.can_acquire())
    {
        return false;
    }
    std::vector<State::Record*> dependencies;
    if (!state.memory_inputs(*candidate, handles, dependencies))
    {
        return false;
    }
    if (!state.resources.validate_memory(*candidate))
    {
        return state.fail(state.resources.error(), std::string(state.resources.last_error()));
    }
    auto* record = state.make_record(candidate->info());
    record->memory = true;
    if (!state.publish(*record, candidate))
    {
        state.collect(record);
        return false;
    }
    const bool success = state.acquire(*record, output);
    if (!success)
    {
        candidate = std::move(record->value);
        state.collect(record);
    }
    return success;
}
bool ResourceGraphSystem::begin_load_memory(std::unique_ptr<Resource>& candidate,
                                            std::span<const ResourceHandle> handles,
                                            std::unique_ptr<ResourceLoad>& output)
{
    return state_->begin_memory(candidate, handles, output);
}

bool ResourceGraphSystem::acquire_loaded(std::string_view input, ResourceHandle& output)
{
    auto& state = *state_;
    if (!state.command())
    {
        return false;
    }
    if (!empty(output))
    {
        return state.fail(ResourceError::busy, "Resource handle output must be empty");
    }
    std::string guid;
    if (!state.resolve(input, guid))
    {
        return false;
    }
    const auto found = state.records.find({state.generation, guid});
    if (found == state.records.end())
    {
        return state.fail(ResourceError::not_loaded, "Resource has not been loaded: " + guid);
    }
    if (found->second->phase != State::Phase::ready)
    {
        return state.fail(ResourceError::not_ready, "Resource is not ready: " + guid);
    }
    return state.acquire(*found->second, output);
}
bool ResourceGraphSystem::retain(const ResourceHandle& handle, ResourceHandle& output)
{
    if (!state_->command())
    {
        return false;
    }
    auto* record = state_->lookup(handle);
    if (!record)
    {
        return state_->fail(ResourceError::invalid_handle, "Invalid resource acquisition");
    }
    return state_->acquire(*record, output);
}
bool ResourceGraphSystem::retain_dependency(const ResourceHandle& handle, std::string_view guid,
                                            ResourceHandle& output)
{
    if (!state_->command())
    {
        return false;
    }
    if (!empty(output))
    {
        return state_->fail(ResourceError::busy, "Resource handle output must be empty");
    }
    auto* root = state_->lookup(handle);
    if (!root)
    {
        return state_->fail(ResourceError::invalid_handle, "Invalid resource owner acquisition");
    }
    std::set<const State::Record*> visited;
    std::vector<State::Record*> pending(root->dependencies.begin(), root->dependencies.end());
    while (!pending.empty())
    {
        auto* record = pending.back();
        pending.pop_back();
        if (!visited.insert(record).second)
        {
            continue;
        }
        if (record->info.guid == guid)
        {
            return state_->acquire(*record, output);
        }
        pending.insert(pending.end(), record->dependencies.begin(), record->dependencies.end());
    }
    return state_->fail(ResourceError::not_found,
                        "Dependency is absent from the owner's loaded closure");
}
bool ResourceGraphSystem::release(ResourceHandle& handle) noexcept
{
    state_->refresh_generation();
    return state_->release(handle);
}
ResourceError ResourceGraphSystem::get(const ResourceHandle& handle,
                                       const Resource*& output) const noexcept
{
    output = nullptr;
    if (empty(handle))
    {
        return ResourceError::not_loaded;
    }
    const auto* record = state_->lookup(handle);
    if (!record)
    {
        return ResourceError::invalid_handle;
    }
    output = record->value.get();
    return ResourceError::none;
}
ResourceError ResourceGraphSystem::error() const noexcept
{
    return state_->error;
}
std::string_view ResourceGraphSystem::last_error() const noexcept
{
    if (state_->error == ResourceError::invalid_handle && state_->message.empty())
    {
        return "Invalid resource acquisition";
    }
    return state_->message;
}
} // namespace engine::plugins::resource_graph
