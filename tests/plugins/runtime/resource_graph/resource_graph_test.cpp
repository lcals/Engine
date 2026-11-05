#include "resource_fixture.hpp"

#include "plugins/runtime/resource_graph/resource_graph_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/builtin_resources.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

using namespace engine;

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "resource graph: %s\n", message);
    }
    return value;
}

std::string guid(std::size_t index)
{
    std::array<char, 37> output{};
    std::snprintf(output.data(), output.size(), "20000000-0000-4000-8000-%012zu", index);
    return output.data();
}

bool write(const std::filesystem::path& path, ByteView bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.close();
    return static_cast<bool>(file);
}

bool put_source(test_support::ResourceFixture& fixture, std::size_t index, std::byte value,
                const std::vector<std::string>& dependencies = {}, bool store = true)
{
    auto entry = std::make_unique<schema::AssetEntryT>();
    entry->guid = guid(index);
    entry->path = "data/" + std::to_string(index);
    entry->kind = schema::AssetKind::source;
    entry->dependencies = dependencies;
    ByteBuffer bytes;
    const std::array payload{value};
    if (!ResourceSystem::encode_resource(entry->guid, entry->kind, payload, bytes,
                                         entry->header_size))
    {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : bytes)
    {
        hash = (hash ^ std::to_integer<std::uint8_t>(byte)) * 1099511628211ULL;
    }
    std::array<char, 17> digest{};
    std::snprintf(digest.data(), digest.size(), "%016llx", static_cast<unsigned long long>(hash));
    entry->content_hash = digest.data();
    entry->content_size = bytes.size();
    entry->location = std::make_unique<schema::FileLocationT>();
    entry->location->mount = "content";
    entry->location->path = "gen/" + entry->guid;
    entry->location->length = bytes.size();
    if (store && !write(fixture.root / entry->location->path, bytes))
    {
        return false;
    }
    for (auto& existing : fixture.catalog.entries)
    {
        if (existing->guid == entry->guid)
        {
            existing = std::move(entry);
            return true;
        }
    }
    fixture.catalog.entries.push_back(std::move(entry));
    return true;
}

bool test_shared_graph_and_generations()
{
    ResourceSystem resources;
    engine::plugins::resource_graph::ResourceGraphSystem graph(resources);
    EngineConfig config;
    ResourcePtr output;
    if (!expect(&resources.loader() == &resources && !graph.initialize(config) &&
                    !graph.load(guid(4), output) && !output,
                "uninitialized graph fails without replacing the default single-item loader") ||
        !resources.initialize(config) || !graph.initialize(config))
    {
        return false;
    }
    resources.set_loader(&graph);
    test_support::ResourceFixture fixture;
    if (!put_source(fixture, 1, std::byte{42}, {}, false) ||
        !put_source(fixture, 2, std::byte{2}, {guid(1)}, false) ||
        !put_source(fixture, 3, std::byte{3}, {guid(1)}, false) ||
        !put_source(fixture, 4, std::byte{4}, {guid(2), guid(3)}) || !fixture.publish(resources))
    {
        return false;
    }
    if (!expect(resources.load(guid(4), output) && output->bytes()[0] == std::byte{4} &&
                    !resources.find(guid(4)),
                "core reads only the requested entity even when dependency files are absent") ||
        !expect(!resources.loader().load(guid(4), output) && !output && !graph.find(guid(4)) &&
                    !graph.find(guid(1)),
                "graph load fails and publishes no partial closure") ||
        !put_source(fixture, 1, std::byte{42}) ||
        !put_source(fixture, 2, std::byte{2}, {guid(1)}) ||
        !put_source(fixture, 3, std::byte{3}, {guid(1)}))
    {
        return false;
    }
    ResourcePtr shared;
    ResourcePtr leaf;
    if (!expect(graph.load(guid(4), output) && graph.load(guid(4), shared) && shared == output &&
                    graph.load(guid(1), leaf) && leaf == graph.find(guid(1)),
                "plugin shares live roots and a diamond's common dependency"))
    {
        return false;
    }
    std::weak_ptr<const Resource> weak_root = output;
    std::weak_ptr<const Resource> weak_parent = graph.find(guid(2));
    std::weak_ptr<const Resource> weak_leaf = leaf;
    output.reset();
    shared.reset();
    if (!expect(weak_root.expired() && weak_parent.expired() && !weak_leaf.expired(),
                "releasing root retires users while separately held dependency survives"))
    {
        return false;
    }
    leaf.reset();
    if (!expect(weak_leaf.expired() && !graph.find(guid(1)),
                "weak graph index retains no resource after the final owner releases") ||
        !graph.load(guid(4), output))
    {
        return false;
    }
    auto old_leaf = graph.find(guid(1));
    const auto old_generation = output->generation();
    if (!put_source(fixture, 1, std::byte{43}) || !fixture.publish(resources) ||
        !expect(resources.generation() != old_generation && !graph.find(guid(4)) &&
                    !graph.find(guid(1)) && output->generation() == old_generation &&
                    old_leaf->bytes()[0] == std::byte{42},
                "directory replacement isolates old objects and invalidates current lookups") ||
        !expect(graph.load(guid(4), shared) && shared != output &&
                    shared->generation() == resources.generation() &&
                    graph.find(guid(1)) != old_leaf &&
                    graph.find(guid(1))->bytes()[0] == std::byte{43},
                "same GUID in a new generation never reuses old entity data"))
    {
        return false;
    }
    const auto current_generation = resources.generation();
    auto invalid = fixture.catalog;
    invalid.schema_version = 0;
    if (!expect(!resources.update_catalog(invalid, {{"content", fixture.root}}) &&
                    resources.generation() == current_generation && graph.find(guid(4)) == shared,
                "invalid directory replacement preserves current catalog and graph") ||
        !expect(!resources.validate_dependencies(
                    *output, std::array{graph.find(guid(2)), graph.find(guid(3))}),
                "cross-generation dependency validation rejects mixed entities"))
    {
        return false;
    }
    std::weak_ptr<const Resource> old_dependency = old_leaf;
    old_leaf.reset();
    if (!expect(!old_dependency.expired(), "old root retains its full old dependency closure"))
    {
        return false;
    }
    output.reset();
    if (!expect(old_dependency.expired() && graph.find(guid(1)),
                "old generation releases independently from new-generation objects"))
    {
        return false;
    }
    graph.shutdown();
    return expect(&resources.loader() == &resources && !graph.find(guid(4)) &&
                      shared->bytes()[0] == std::byte{4},
                  "plugin shutdown restores core loader and leaves external resource data valid");
}

bool test_cross_entity_validation()
{
    ResourceSystem resources;
    engine::plugins::resource_graph::ResourceGraphSystem graph(resources);
    EngineConfig config;
    if (!resources.initialize(config) || !graph.initialize(config))
    {
        return false;
    }
    resources.set_loader(&graph);
    test_support::ResourceFixture fixture;
    auto shader = *ResourceSystem::find_shader("builtin://shaders/pbr");
    shader.path = "invalid-pair.shader";
    schema::MaterialDataT material;
    material.type_path = "invalid-pair.material";
    material.shader_path = shader.path;
    auto parameter = std::make_unique<schema::MaterialParameterT>();
    parameter->name = "notDeclaredByShader";
    schema::FloatValueT value;
    value.value = 0.5F;
    parameter->value.Set(std::move(value));
    material.parameters.push_back(std::move(parameter));
    if (!fixture.add(shader) || !fixture.add(material) || !fixture.publish(resources))
    {
        return false;
    }
    ResourcePtr single;
    ResourcePtr loaded;
    const auto& shader_guid = fixture.catalog.entries[0]->guid;
    const auto& material_guid = fixture.catalog.entries[1]->guid;
    return expect(resources.load(material_guid, single) && single->material(),
                  "single-item parse does not recursively fetch another entity") &&
           expect(!graph.load(material_guid, loaded) && !loaded && !graph.find(shader_guid) &&
                      !graph.find(material_guid),
                  "graph retains material/shader semantic checks and rolls back failed closure");
}

bool test_step_progress_and_ownership()
{
    ResourceSystem resources;
    engine::plugins::resource_graph::ResourceGraphSystem graph(resources);
    if (!resources.initialize({}) || !graph.initialize({}))
    {
        return false;
    }
    resources.set_loader(&graph);
    test_support::ResourceFixture fixture;
    if (!put_source(fixture, 1, std::byte{1}, {}, false) ||
        !put_source(fixture, 2, std::byte{2}, {guid(1)}, false) ||
        !put_source(fixture, 3, std::byte{3}, {guid(1)}, false) ||
        !put_source(fixture, 4, std::byte{4}, {guid(2), guid(3)}, false) ||
        !fixture.publish(resources))
    {
        return false;
    }
    std::unique_ptr<ResourceLoad> operation;
    ResourceLoadProgress progress;
    if (!expect(graph.begin_load(guid(4), operation) && operation &&
                    operation->step(0, progress) == ResourceLoadStatus::loading &&
                    progress.completed == 0 && progress.total == 4 && !operation->resource(),
                "begin and zero budget count a unique diamond without reading missing entities") ||
        !expect(operation->step(1, progress) == ResourceLoadStatus::failed &&
                    progress.completed == 0 && progress.total == 4 && !operation->resource() &&
                    !graph.find(guid(1)) && !graph.find(guid(4)),
                "entity failure retains exact counters and publishes no partial closure") ||
        !put_source(fixture, 1, std::byte{1}) || !put_source(fixture, 2, std::byte{2}, {guid(1)}) ||
        !put_source(fixture, 3, std::byte{3}, {guid(1)}) ||
        !put_source(fixture, 4, std::byte{4}, {guid(2), guid(3)}))
    {
        return false;
    }
    ResourcePtr leaf;
    if (!graph.load(guid(1), leaf) || !graph.begin_load(guid(4), operation) ||
        !expect(operation->step(2, progress) == ResourceLoadStatus::loading &&
                    progress.completed == 2 && progress.total == 4 && graph.find(guid(1)) == leaf &&
                    !graph.find(guid(2)) && !graph.find(guid(4)) && !operation->resource(),
                "budget counts a cached leaf and private parent while withholding candidates"))
    {
        return false;
    }
    operation.reset();
    if (!expect(graph.find(guid(1)) == leaf && !graph.find(guid(2)),
                "cancelling releases only task candidates and preserves another owner's leaf") ||
        !graph.begin_load(guid(4), operation) ||
        !expect(operation->step(2, progress) == ResourceLoadStatus::loading &&
                    progress.completed == 2,
                "the next operation starts with independent progress"))
    {
        return false;
    }
    ResourcePtr published_parent;
    if (!graph.load(guid(2), published_parent) ||
        !expect(operation->step(1, progress) == ResourceLoadStatus::loading &&
                    progress.completed == 3 && !graph.find(guid(3)),
                "one complete resource consumes one step budget") ||
        !expect(operation->step(1, progress) == ResourceLoadStatus::ready &&
                    progress.completed == 4 && progress.total == 4 && operation->resource() &&
                    graph.find(guid(2)) == published_parent && graph.find(guid(1)) == leaf &&
                    operation->last_error().empty(),
                "ready publishes the closure and reuses a parent published between steps"))
    {
        return false;
    }
    auto root = operation->resource();
    std::weak_ptr<const Resource> weak_parent = published_parent;
    std::weak_ptr<const Resource> weak_leaf = leaf;
    operation.reset();
    published_parent.reset();
    leaf.reset();
    if (!expect(!weak_parent.expired() && !weak_leaf.expired() && graph.find(guid(4)) == root,
                "copied ready root owns the canonical parent and its full dependency closure"))
    {
        return false;
    }
    root.reset();
    return expect(weak_parent.expired() && weak_leaf.expired() && !graph.find(guid(4)),
                  "the final ready owner releases the entire closure without a strong cache");
}

bool test_step_terminal_and_generation_rules()
{
    ResourceSystem resources;
    engine::plugins::resource_graph::ResourceGraphSystem graph(resources);
    if (!resources.initialize({}) || !graph.initialize({}))
    {
        return false;
    }
    resources.set_loader(&graph);
    test_support::ResourceFixture fixture;
    if (!put_source(fixture, 1, std::byte{1}) ||
        !put_source(fixture, 2, std::byte{2}, {guid(1)}, false) || !fixture.publish(resources))
    {
        return false;
    }
    std::unique_ptr<ResourceLoad> operation;
    ResourceLoadProgress progress;
    if (!graph.begin_load(guid(2), operation) ||
        !expect(operation->step(1, progress) == ResourceLoadStatus::loading &&
                    progress.completed == 1 && progress.total == 2,
                "a successful child is counted before a later parent failure") ||
        !expect(operation->step(1, progress) == ResourceLoadStatus::failed &&
                    progress.completed == 1 && !operation->resource() &&
                    !operation->last_error().empty() && !graph.find(guid(1)),
                "failed parent releases unpublished dependencies without losing completed count"))
    {
        return false;
    }
    const std::string error(operation->last_error());
    ResourcePtr unused;
    if (!expect(!graph.load(guid(999), unused) &&
                    operation->step(20, progress) == ResourceLoadStatus::failed &&
                    progress.completed == 1 && progress.total == 2 &&
                    operation->last_error() == error,
                "failed terminal snapshot owns its error independently from later loader calls") ||
        !put_source(fixture, 2, std::byte{2}, {guid(1)}) || !graph.begin_load(guid(2), operation) ||
        operation->step(1, progress) != ResourceLoadStatus::loading ||
        !put_source(fixture, 1, std::byte{3}) || !fixture.publish(resources) ||
        !expect(operation->step(0, progress) == ResourceLoadStatus::failed &&
                    progress.completed == 1 && progress.total == 2 && !operation->resource() &&
                    !graph.find(guid(1)),
                "zero budget detects a loading generation change and releases candidates") ||
        !graph.begin_load(guid(2), operation) ||
        operation->step(2, progress) != ResourceLoadStatus::ready)
    {
        return false;
    }
    const auto old_root = operation->resource();
    const auto old_generation = old_root->generation();
    if (!put_source(fixture, 1, std::byte{4}) || !fixture.publish(resources) ||
        !expect(operation->step(0, progress) == ResourceLoadStatus::ready &&
                    progress.completed == 2 && progress.total == 2 &&
                    operation->resource() == old_root &&
                    operation->resource()->generation() == old_generation &&
                    operation->last_error().empty() && !graph.find(guid(2)),
                "ready remains an old-generation snapshot and never republishes stale objects"))
    {
        return false;
    }
    ResourcePtr replacement;
    if (!graph.load(guid(2), replacement) ||
        !expect(replacement != old_root && replacement->generation() == resources.generation() &&
                    graph.find(guid(1))->bytes()[0] == std::byte{4} &&
                    operation->step(10, progress) == ResourceLoadStatus::ready &&
                    graph.find(guid(2)) == replacement,
                "repeating an old ready operation cannot overwrite new-generation cache entries") ||
        !graph.begin_load(guid(2), operation))
    {
        return false;
    }
    resources.set_loader(nullptr);
    if (!expect(operation->step(0, progress) == ResourceLoadStatus::failed &&
                    progress.completed == 0 && progress.total == 2,
                "changing the selected loader fails an unadvanced operation without entity IO"))
    {
        return false;
    }
    resources.set_loader(&graph);
    if (!graph.begin_load(guid(2), operation))
    {
        return false;
    }
    return expect(!resources.begin_load(guid(2), operation) && !operation,
                  "default single-item loader reports unsupported steps and clears old output");
}

bool test_deep_graph_cleanup()
{
    ResourceSystem resources;
    engine::plugins::resource_graph::ResourceGraphSystem graph(resources);
    EngineConfig config;
    if (!resources.initialize(config) || !graph.initialize(config))
    {
        return false;
    }
    test_support::ResourceFixture fixture;
    constexpr std::size_t count = 4096;
    for (std::size_t index = 1; index <= count; ++index)
    {
        if (!put_source(fixture, index, std::byte{7},
                        index == 1 ? std::vector<std::string>{}
                                   : std::vector<std::string>{guid(index - 1)}))
        {
            return false;
        }
    }
    ResourcePtr root;
    if (!fixture.publish(resources) || !graph.load(guid(count), root))
    {
        return false;
    }
    std::weak_ptr<const Resource> leaf = graph.find(guid(1));
    root.reset();
    if (!expect(leaf.expired() && !graph.find(guid(count)),
                "4096-resource dependency chain releases without recursive C++ destruction"))
    {
        return false;
    }
    const std::array corrupt{std::byte{0}};
    if (!write(fixture.root / "gen" / guid(count), corrupt))
    {
        return false;
    }
    return expect(!graph.load(guid(count), root) && !root && !graph.find(guid(1)) &&
                      !graph.find(guid(count - 1)),
                  "deep terminal failure releases the whole newly loaded closure");
}

bool test_external_builtin_lookup()
{
    ResourceSystem resources;
    engine::plugins::resource_graph::ResourceGraphSystem graph(resources);
    if (!resources.initialize({}) || !graph.initialize({}))
    {
        return false;
    }
    resources.set_loader(&graph);
    constexpr auto identity = "00000000-0000-4000-8000-000000000005";
    constexpr auto path = "builtin://meshes/cube";
    const auto* original = ResourceSystem::find_mesh(path);
    const auto* info = builtin_resource_info(identity);
    if (!original || !info || original->vertices.empty())
    {
        return false;
    }
    auto mesh = *original;
    const auto vertex = mesh.vertices.front();
    mesh.vertices.front() = {vertex.x() + 3.0F, vertex.y(),  vertex.z(),
                             vertex.nx(),       vertex.ny(), vertex.nz()};
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishMeshDataBuffer(builder, schema::MeshData::Pack(builder, &mesh));
    auto entry = std::make_unique<schema::AssetEntryT>(*info);
    ByteBuffer entity;
    const ByteView payload{reinterpret_cast<const std::byte*>(builder.GetBufferPointer()),
                           builder.GetSize()};
    if (!ResourceSystem::encode_resource(identity, entry->kind, payload, entity,
                                         entry->header_size))
    {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : entity)
    {
        hash = (hash ^ std::to_integer<std::uint8_t>(byte)) * 1099511628211ULL;
    }
    std::array<char, 17> digest{};
    std::snprintf(digest.data(), digest.size(), "%016llx", static_cast<unsigned long long>(hash));
    entry->content_hash = digest.data();
    entry->content_size = entity.size();
    entry->location = std::make_unique<schema::FileLocationT>();
    entry->location->mount = "content";
    entry->location->path = "gen/" + entry->guid;
    entry->location->length = entity.size();
    test_support::ResourceFixture fixture;
    fixture.catalog.entries.push_back(std::move(entry));
    ResourcePtr loaded;
    if (!write(fixture.root / "gen" / identity, entity) || !fixture.publish(resources) ||
        !expect(!ResourceSystem::find_mesh(path) && graph.load(identity, loaded) && loaded &&
                    loaded->mesh() && loaded->mesh()->vertices.front().x() == vertex.x() + 3.0F &&
                    ResourceSystem::find_mesh(path) == loaded->mesh(),
                "builtin lookups use the current loader's external resource instead of bootstrap"))
    {
        return false;
    }
    entity.back() ^= std::byte{1};
    ResourcePtr replacement;
    return expect(write(fixture.root / "gen" / identity, entity) && fixture.publish(resources) &&
                      !ResourceSystem::find_mesh(path) && !graph.load(identity, replacement) &&
                      !replacement && !ResourceSystem::find_mesh(path) &&
                      loaded->mesh()->vertices.front().x() == vertex.x() + 3.0F,
                  "corrupt new builtin generations cannot expose stale or compiled definitions");
}

bool test_async_graph_loading()
{
    ThreadSystem threads({2});
    TaskSystem tasks(threads, {"ResourceTest", 2});
    ResourceSystem resources;
    engine::plugins::resource_graph::ResourceGraphSystem graph(resources);
    if (!threads.initialize({}) || !tasks.initialize({}) || !resources.initialize({}) ||
        !graph.initialize({}))
    {
        return false;
    }
    resources.set_loader(&graph);
    test_support::ResourceFixture fixture;
    if (!put_source(fixture, 1, std::byte{1}) || !put_source(fixture, 2, std::byte{2}, {guid(1)}) ||
        !put_source(fixture, 3, std::byte{3}, {guid(1)}) ||
        !put_source(fixture, 4, std::byte{4}, {guid(2), guid(3)}) || !fixture.publish(resources) ||
        !resources.configure_async(tasks, {2, 1024 * 1024, 1}))
    {
        return false;
    }
    auto pump = [&](ResourceLoad& operation)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        ResourceLoadProgress progress;
        while (std::chrono::steady_clock::now() < deadline)
        {
            (void)tasks.dispatch_completions(8, 0.01);
            const auto status = operation.step(8, progress);
            if (status != ResourceLoadStatus::loading)
            {
                return status;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return ResourceLoadStatus::loading;
    };
    std::unique_ptr<ResourceLoad> operation;
    ResourceLoadProgress progress;
    if (!resources.begin_load(guid(4), operation))
    {
        return false;
    }
    resources.set_loader(nullptr);
    const bool loader_changed =
        expect(operation->step(0, progress) == ResourceLoadStatus::failed && !operation->resource(),
               "core single-item operations reject a changed selected loader before I/O");
    operation.reset();
    resources.set_loader(&graph);
    if (!loader_changed)
    {
        return false;
    }
    if (!graph.begin_load(guid(4), operation) ||
        !expect(operation->step(8, progress) == ResourceLoadStatus::loading &&
                    !operation->resource() && !graph.find(guid(1)),
                "async graph submits reads without publishing incomplete dependencies") ||
        !expect(pump(*operation) == ResourceLoadStatus::ready &&
                    operation->resource()->bytes()[0] == std::byte{4} && graph.find(guid(1)),
                "async graph finishes under a two-request budget with a one-callback budget"))
    {
        return false;
    }
    ResourcePtr synchronous;
    if (!expect(graph.load(guid(4), synchronous) && synchronous == operation->resource(),
                "explicit synchronous load shares completed asynchronous roots"))
    {
        return false;
    }
    operation.reset();
    synchronous.reset();
    if (!graph.begin_load(guid(4), operation) ||
        operation->step(8, progress) != ResourceLoadStatus::loading)
    {
        return false;
    }
    operation.reset(); // 已提交读取取消；无等待，结果由文件队列回收。
    if (!graph.begin_load(guid(4), operation) ||
        !expect(pump(*operation) == ResourceLoadStatus::ready,
                "cancellation returns in-flight capacity without publishing abandoned roots"))
    {
        return false;
    }
    operation.reset();
    if (!graph.begin_load(guid(4), operation) ||
        operation->step(8, progress) != ResourceLoadStatus::loading ||
        !put_source(fixture, 1, std::byte{9}) || !fixture.publish(resources) ||
        !expect(operation->step(0, progress) == ResourceLoadStatus::failed &&
                    !operation->resource() && !graph.find(guid(4)),
                "catalog replacement rejects old reads before publication") ||
        !graph.begin_load(guid(4), operation) ||
        !expect(pump(*operation) == ResourceLoadStatus::ready &&
                    graph.find(guid(1))->bytes()[0] == std::byte{9},
                "new reads use the replacement catalog and mount snapshot"))
    {
        return false;
    }
    operation.reset();
    resources.shutdown_async();
    if (!resources.configure_async(tasks, {1, 1, 1}) || !resources.begin_load(guid(4), operation) ||
        !expect(operation->step(1, progress) == ResourceLoadStatus::failed,
                "an entity exceeding the byte budget fails instead of stalling forever"))
    {
        return false;
    }
    operation.reset();
    resources.shutdown_async();
    if (!resources.configure_async(tasks) || !resources.begin_load(guid(4), operation) ||
        operation->step(1, progress) != ResourceLoadStatus::loading)
    {
        return false;
    }
    resources.shutdown_async();
    return expect(operation->step(0, progress) == ResourceLoadStatus::failed &&
                      !resources.async_enabled() && graph.load(guid(4), synchronous),
                  "shutdown invalidates pending operations and leaves synchronous APIs usable");
}

} // namespace

int main()
{
    return test_shared_graph_and_generations() && test_cross_entity_validation() &&
                   test_step_progress_and_ownership() &&
                   test_step_terminal_and_generation_rules() && test_deep_graph_cleanup() &&
                   test_external_builtin_lookup() && test_async_graph_loading()
               ? 0
               : 1;
}
