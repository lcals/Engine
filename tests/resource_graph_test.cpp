#include "resource_fixture.hpp"

#include "plugins/runtime/resource_graph/resource_graph_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
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
    entry->path = "project/data/" + std::to_string(index);
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
    auto shader = *ResourceSystem::find_shader("builtin/shaders/pbr");
    shader.path = "project/invalid-pair.shader";
    schema::MaterialDataT material;
    material.type_path = "project/invalid-pair.material";
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

} // namespace

int main()
{
    return test_shared_graph_and_generations() && test_cross_entity_validation() &&
                   test_deep_graph_cleanup()
               ? 0
               : 1;
}
