#include "plugins/runtime/resource_graph/resource_graph_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/codecs/source_resource_codec.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/resource/types/json_resource.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

namespace
{
using namespace engine;
using engine::plugins::resource_graph::ResourceGraphSystem;

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "resource graph: %s\n", message);
    }
    return value;
}

std::string guid(unsigned index)
{
    std::array<char, 37> value{};
    std::snprintf(value.data(), value.size(), "20000000-0000-4000-8000-%012u", index);
    return value.data();
}

// The graph is format-independent. This fixture codec permits source DAGs and counts decoding;
// the production SourceResourceCodec separately rejects source dependencies.
class CountingCodec final : public ResourceCodec
{
public:
    using ResourceType = SourceResource;
    static inline std::atomic<unsigned> decodes{0};
    schema::AssetKind kind() const noexcept override
    {
        return schema::AssetKind::source;
    }
    bool decode(const schema::AssetEntryT& entry, ByteView bytes, std::unique_ptr<Resource>& output,
                std::string&) const override
    {
        ++decodes;
        output = std::make_unique<ResourceType>(entry, ByteBuffer(bytes.begin(), bytes.end()));
        return true;
    }
    bool validate_data(const Resource& resource, std::string&) const override
    {
        return resource_detail::cast<ResourceType>(&resource) != nullptr;
    }
    bool validate_dependencies(const Resource&, ResourceView values, std::string&) const override
    {
        for (const auto* value : values)
        {
            if (!resource_detail::cast<ResourceType>(value))
            {
                return false;
            }
        }
        return true;
    }
};

struct Fixture
{
    ThreadSystem threads{{2}};
    TaskSystem files{threads, {"GraphFiles", 1}};
    TaskSystem decoding{threads, {"GraphDecoding", 1}};
    TaskSystem planning{threads, {"GraphPlanning", 1}};
    ResourceSystem resources;
    ResourceGraphSystem graph{resources, planning, {}};
    std::filesystem::path root;
    schema::AssetCatalogDataT catalog;

    bool initialize()
    {
        std::error_code error;
        root = std::filesystem::temp_directory_path(error) /
               ("engine-resource-graph-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (error || !std::filesystem::create_directory(root, error) || error ||
            !threads.initialize({}) || !files.initialize({}) || !decoding.initialize({}) ||
            !planning.initialize({}) || !resources.add<CountingCodec>() ||
            !resources.initialize({}) || !resources.configure_async(files, decoding, {}) ||
            !graph.initialize({}))
        {
            return false;
        }
        catalog = resources.catalog().data();
        return true;
    }
    ~Fixture()
    {
        graph.shutdown();
        resources.shutdown();
        std::error_code error;
        if (!root.empty())
        {
            std::filesystem::remove_all(root, error);
        }
    }
    bool add(unsigned index, std::vector<std::string> dependencies = {})
    {
        auto entry = std::make_unique<schema::AssetEntryT>();
        entry->guid = guid(index);
        entry->path = "data/" + std::to_string(index);
        entry->kind = schema::AssetKind::source;
        entry->dependencies = std::move(dependencies);
        ByteBuffer entity;
        const std::array payload{static_cast<std::byte>(index)};
        if (!ResourceSystem::encode_resource(entry->guid, entry->kind, payload, entity,
                                             entry->header_size))
        {
            return false;
        }
        std::ofstream file(root / std::to_string(index), std::ios::binary);
        file.write(reinterpret_cast<const char*>(entity.data()),
                   static_cast<std::streamsize>(entity.size()));
        file.close();
        if (!file)
        {
            return false;
        }
        std::uint64_t hash = 0;
        if (!VirtualFileSystem::hash_file(root / std::to_string(index), hash, entry->content_size))
        {
            return false;
        }
        std::array<char, 17> digest{};
        std::snprintf(digest.data(), digest.size(), "%016llx",
                      static_cast<unsigned long long>(hash));
        entry->content_hash = digest.data();
        entry->location = std::make_unique<schema::FileLocationT>();
        entry->location->mount = "content";
        entry->location->path = std::to_string(index);
        entry->location->length = entry->content_size;
        catalog.entries.push_back(std::move(entry));
        return true;
    }
    bool publish()
    {
        catalog.revision += "-next";
        return resources.update_catalog(catalog, {{"content", root}});
    }
    bool pump(ResourceLoad& operation)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        ResourceLoadProgress progress;
        while (operation.status(progress) == ResourceLoadStatus::loading &&
               std::chrono::steady_clock::now() < deadline)
        {
            if (!resources.tick(0) || !graph.tick(0))
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (operation.status(progress) == ResourceLoadStatus::failed)
        {
            std::fprintf(stderr, "async error: %.*s\n",
                         static_cast<int>(operation.last_error().size()),
                         operation.last_error().data());
        }
        return operation.status(progress) == ResourceLoadStatus::ready;
    }
};

std::unique_ptr<Resource> memory(unsigned index, std::vector<std::string> dependencies = {})
{
    schema::AssetEntryT info;
    info.guid = guid(index);
    info.path = "memory/" + std::to_string(index);
    info.kind = schema::AssetKind::source;
    info.dependencies = std::move(dependencies);
    return std::make_unique<SourceResource>(std::move(info), ByteBuffer{std::byte{42}});
}

bool sync_lifetime()
{
    Fixture f;
    if (!f.initialize() || !f.add(1) || !f.add(2, {guid(1)}) || !f.add(3, {guid(1)}) ||
        !f.add(4, {guid(2), guid(3)}) || !f.publish())
    {
        return false;
    }
    CountingCodec::decodes = 0;
    ResourceHandle a, b, child, invalid;
    const Resource* value = nullptr;
    if (!expect(!f.graph.acquire_loaded("data/4", a) &&
                    f.graph.error() == ResourceError::not_loaded && CountingCodec::decodes == 0,
                "acquire_loaded does not load an existing catalog entry") ||
        !expect(!f.graph.acquire_loaded("unknown", a) &&
                    f.graph.error() == ResourceError::not_found,
                "unknown and unloaded resources have distinct errors") ||
        !f.graph.load("data/4", a) || !f.graph.load(guid(4), b) ||
        !expect(a.id != b.id && CountingCodec::decodes == 4,
                "one immutable record per closure resource") ||
        !f.graph.retain_dependency(a, "data/1", child) ||
        !expect(!f.graph.retain_dependency(a, "data/4", invalid) &&
                    f.graph.error() == ResourceError::not_found,
                "dependency lookup excludes the owner"))
    {
        return false;
    }
    const SourceResource* source = nullptr;
    const JsonResource* wrong = nullptr;
    const auto copied = a;
    if (!expect(f.graph.get(a, source) == ResourceError::none &&
                    source->data()[0] == std::byte{4} &&
                    f.graph.get(a, wrong) == ResourceError::type_mismatch && !wrong,
                "typed access validates the canonical payload") ||
        !f.graph.release(a) || !f.graph.release(a) ||
        !expect(f.graph.get(copied, value) == ResourceError::invalid_handle && !value &&
                    f.graph.get(b, value) == ResourceError::none,
                "copied handle is not an independent acquisition") ||
        !f.graph.release(b) ||
        !expect(
            !f.graph.acquire_loaded(guid(4), invalid) &&
                f.graph.error() == ResourceError::not_loaded &&
                f.graph.get(child, value) == ResourceError::none,
            "last root release unloads the root but preserves independent dependency ownership") ||
        !f.graph.release(child))
    {
        return false;
    }
    std::error_code error;
    std::filesystem::remove(f.root / "1", error);
    if (!expect(!error && !f.graph.load(guid(4), a) && f.graph.error() == ResourceError::io_failed,
                "last release evicts the complete closure") ||
        !expect(!f.graph.acquire_loaded(guid(2), a), "failed load rolls back unpublished records"))
    {
        return false;
    }
    auto duplicate = copied;
    return expect(!f.graph.release(duplicate) && duplicate.id == 0 && duplicate.owner == 0 &&
                      f.graph.get({}, value) == ResourceError::not_loaded,
                  "stale release is harmless and clears the caller's copy");
}

bool asynchronous_lifetime()
{
    Fixture f;
    if (!f.initialize() || !f.add(1) || !f.add(2, {guid(1)}) || !f.add(3, {guid(1)}) ||
        !f.publish())
    {
        return false;
    }
    CountingCodec::decodes = 0;
    std::unique_ptr<ResourceLoad> first, second, other;
    ResourceLoadProgress progress;
    ResourceHandle rejected, handle, retained;
    if (!f.graph.begin_load(guid(2), first) || !f.graph.begin_load(guid(2), second) ||
        !f.graph.begin_load(guid(3), other) ||
        !expect(
            first->status(progress) == ResourceLoadStatus::loading && !first->take(rejected) &&
                CountingCodec::decodes == 0 && !f.graph.load(guid(2), rejected) &&
                f.graph.error() == ResourceError::busy,
            "queries do not advance work and synchronous loads cannot wait on pending records") ||
        !expect(!f.graph.acquire_loaded(guid(2), rejected) &&
                    f.graph.error() == ResourceError::not_ready,
                "in-flight is distinct from unloaded") ||
        !first->cancel() || !first->cancel() || !f.pump(*second) || !f.pump(*other) ||
        !expect(
            CountingCodec::decodes == 3 && second->status(progress) == ResourceLoadStatus::ready &&
                progress.completed == 2 && progress.total == 2,
            "cancelling one waiter preserves shared work and progress counts unique resources") ||
        !second->take(handle) || !f.graph.retain(handle, retained) ||
        !expect(!second->take(rejected) && !second->cancel(), "take transfers exactly once") ||
        !f.graph.release(handle) || !other->cancel() || !f.graph.release(retained))
    {
        return false;
    }
    return expect(!f.graph.acquire_loaded(guid(1), rejected),
                  "cancelled and released results do not leak dependency pins");
}

bool generations_and_memory()
{
    Fixture f;
    if (!f.initialize())
    {
        return false;
    }
    auto leaf = memory(1);
    ResourceHandle old_leaf, old_root, current, borrowed;
    if (!f.graph.load_memory(leaf, {}, old_leaf) || leaf)
    {
        return false;
    }
    auto root = memory(2, {guid(1)});
    if (!f.graph.load_memory(root, std::span(&old_leaf, 1), old_root) || root ||
        !f.graph.release(old_leaf))
    {
        return false;
    }
    auto conflict = memory(2);
    if (!expect(!f.graph.load_memory(conflict, {}, current) && conflict &&
                    f.graph.error() == ResourceError::identity_conflict,
                "conflicting memory publication preserves the candidate"))
    {
        return false;
    }
    std::unique_ptr<ResourceLoad> pending, ready;
    auto candidate = memory(3);
    if (!f.graph.begin_load_memory(candidate, {}, pending) || candidate ||
        !f.graph.begin_load(guid(2), ready) || !f.publish() || !f.graph.tick(0))
    {
        return false;
    }
    ResourceLoadProgress progress;
    if (!expect(pending->status(progress) == ResourceLoadStatus::failed &&
                    pending->error() == ResourceError::generation_changed &&
                    ready->status(progress) == ResourceLoadStatus::ready,
                "generation changes fail pending requests and preserve completed ones") ||
        !f.graph.retain_dependency(old_root, "memory/1", borrowed))
    {
        return false;
    }
    const Resource* old_data = nullptr;
    if (f.graph.get(borrowed, old_data) != ResourceError::none)
    {
        return false;
    }
    const auto old_generation = old_data->generation();
    candidate = memory(1);
    if (!f.graph.load_memory(candidate, {}, current))
    {
        return false;
    }
    const Resource* new_data = nullptr;
    if (!expect(f.graph.get(current, new_data) == ResourceError::none && new_data != old_data &&
                    new_data->generation() != old_generation,
                "old closures never resolve against new-generation records"))
    {
        return false;
    }
    candidate = memory(4, {guid(1)});
    ResourceHandle rejected;
    if (!expect(!f.graph.load_memory(candidate, std::span(&borrowed, 1), rejected) && candidate &&
                    f.graph.error() == ResourceError::generation_changed,
                "new memory resources cannot combine old-generation dependencies"))
    {
        return false;
    }
    const auto copied = current;
    f.graph.shutdown();
    if (!expect(ready->status(progress) == ResourceLoadStatus::cancelled && ready->cancel() &&
                    !ready->take(rejected) &&
                    f.graph.get(copied, new_data) == ResourceError::invalid_handle,
                "shutdown cancels unclaimed results and invalidates all acquisitions") ||
        !f.graph.initialize({}))
    {
        return false;
    }
    candidate = memory(1);
    current = {};
    if (!f.graph.load_memory(candidate, {}, current))
    {
        return false;
    }
    return expect(f.graph.get(copied, new_data) == ResourceError::invalid_handle &&
                      f.graph.release(current),
                  "reinitialization never revives a stale acquisition");
}

bool ownership_and_budgets()
{
    Fixture f;
    if (!f.initialize() || !f.add(1) || !f.publish())
    {
        return false;
    }
    ResourceGraphSystem other(f.resources, f.planning, {});
    if (!other.initialize({}))
    {
        return false;
    }
    ResourceHandle handle, second;
    if (!f.graph.load(guid(1), handle))
    {
        return false;
    }
    const Resource* value = nullptr;
    auto foreign = handle;
    if (!expect(other.get(handle, value) == ResourceError::invalid_handle &&
                    !other.release(foreign) && f.graph.get(handle, value) == ResourceError::none,
                "provider identities prevent cross-loader releases") ||
        !expect(!f.graph.retain(handle, handle) &&
                    f.graph.get(handle, value) == ResourceError::none,
                "nonempty outputs never overwrite existing ownership") ||
        !f.graph.release(handle))
    {
        return false;
    }
    ResourceGraphSystem small(f.resources, f.planning, {2, 16, 0.002});
    std::vector<std::string> dependencies{guid(1)};
    for (unsigned index = 2; index <= 1101; ++index)
    {
        if (!f.add(index))
        {
            return false;
        }
        dependencies.push_back(guid(index));
    }
    if (!small.initialize({}) || !f.add(1102, std::move(dependencies)) || !f.publish())
    {
        return false;
    }
    std::unique_ptr<ResourceLoad> operation;
    if (!small.begin_load(guid(1102), operation))
    {
        return false;
    }
    ResourceLoadProgress progress;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (operation->status(progress) == ResourceLoadStatus::loading &&
           std::chrono::steady_clock::now() < deadline)
    {
        if (!f.resources.tick(0) || !small.tick(0))
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ResourceHandle result, dependency;
    return expect(operation->status(progress) == ResourceLoadStatus::ready &&
                      progress.completed == 1102 && progress.total == 1102 &&
                      operation->take(result) &&
                      small.retain_dependency(result, "data/1101", dependency) &&
                      small.release(result) && small.release(dependency),
                  "closures above 1024 resources advance through the bounded read window");
}
} // namespace

int main()
{
    return expect(sync_lifetime(), "sync_lifetime") &&
                   expect(asynchronous_lifetime(), "asynchronous_lifetime") &&
                   expect(generations_and_memory(), "generations_and_memory") &&
                   expect(ownership_and_budgets(), "ownership_and_budgets")
               ? 0
               : 1;
}
