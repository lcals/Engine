#include "runtime/engine_config.hpp"
#include "runtime/resource/builtin_resources.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/world/world_system.hpp"

#include <flatbuffers/flatbuffers.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <system_error>

namespace
{

using namespace engine;
constexpr const char* shader_guid = "10000000-0000-4000-8000-000000000001";
constexpr const char* material_guid = "10000000-0000-4000-8000-000000000002";
constexpr const char* mesh_guid = "10000000-0000-4000-8000-000000000003";
constexpr const char* scene_guid = "10000000-0000-4000-8000-000000000004";
constexpr const char* source_guid = "10000000-0000-4000-8000-000000000005";

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "resource catalog: %s\n", message);
    }
    return value;
}

std::string digest(ByteView bytes)
{
    std::uint64_t value = 14695981039346656037ULL;
    for (const std::byte byte : bytes)
    {
        value ^= std::to_integer<std::uint8_t>(byte);
        value *= 1099511628211ULL;
    }
    std::array<char, 17> text{};
    std::snprintf(text.data(), text.size(), "%016llx", static_cast<unsigned long long>(value));
    return text.data();
}

template <typename T> ByteBuffer pack(const T& data, const char* identifier)
{
    flatbuffers::FlatBufferBuilder builder;
    builder.Finish(T::TableType::Pack(builder, &data), identifier);
    const auto* bytes = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
    return {bytes, bytes + builder.GetSize()};
}

bool write(const std::filesystem::path& path, ByteView bytes)
{
    std::ofstream stream(path, std::ios::binary);
    if (!bytes.empty())
    {
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    stream.close();
    return static_cast<bool>(stream);
}

struct Fixture
{
    schema::AssetCatalogDataT data;
    std::map<Guid, ByteBuffer, std::less<>> files;

    bool add(const char* guid, const std::string& path, schema::AssetKind kind,
             const ByteBuffer& payload, std::vector<std::string> dependencies = {})
    {
        auto entry = std::make_unique<schema::AssetEntryT>();
        entry->guid = guid;
        entry->path = path;
        entry->kind = kind;
        entry->dependencies = std::move(dependencies);
        ByteBuffer entity;
        if (!ResourceSystem::encode_resource(guid, kind, payload, entity, entry->header_size))
        {
            return false;
        }
        entry->content_size = entity.size();
        entry->content_hash = digest(entity);
        entry->location = std::make_unique<schema::FileLocationT>();
        entry->location->mount = "content";
        entry->location->path = "gen/" + entry->guid;
        entry->location->length = entity.size();
        files.emplace(guid, std::move(entity));
        data.entries.push_back(std::move(entry));
        return true;
    }
};

std::string platform()
{
#if defined(__ANDROID__)
    return "android";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    return "ios";
#elif defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

bool range_reads(const std::filesystem::path& root, const Fixture& fixture)
{
    const auto& entity = fixture.files.find(source_guid)->second;
    const auto path = root / "resources.pack";
    // 包总大小超过整文件上限，成员读取不能退化为整包读取。
    constexpr std::uint64_t prefix_size = 40U * 1024U * 1024U;
    {
        std::ofstream stream(path, std::ios::binary);
        stream.seekp(static_cast<std::streamoff>(prefix_size));
        stream.write(reinterpret_cast<const char*>(entity.data()),
                     static_cast<std::streamsize>(entity.size()));
        stream.seekp(80U * 1024U * 1024U);
        stream.put('\0');
        stream.close();
        if (!stream)
        {
            return false;
        }
    }
    auto data = fixture.data;
    auto& location = *data.entries.back()->location;
    location.path = "resources.pack";
    location.offset = prefix_size;
    ResourceCatalog catalog;
    VirtualFileSystem files;
    const MountList mounts{{"content", root}};
    ByteBuffer payload;
    const auto zero_bytes = [](std::uint64_t count)
    {
        std::uint64_t factor = 1;
        std::uint64_t base = 1099511628211ULL;
        while (count != 0)
        {
            if ((count & 1U) != 0)
            {
                factor *= base;
            }
            base *= base;
            count >>= 1;
        }
        return factor;
    };
    constexpr std::uint64_t package_size = 80U * 1024U * 1024U + 1U;
    std::uint64_t expected_hash = 14695981039346656037ULL * zero_bytes(prefix_size);
    for (const std::byte byte : entity)
    {
        expected_hash ^= std::to_integer<std::uint8_t>(byte);
        expected_hash *= 1099511628211ULL;
    }
    expected_hash *= zero_bytes(package_size - prefix_size - entity.size());
    std::uint64_t hash = 0;
    std::uint64_t size = 0;
    if (!expect(VirtualFileSystem::hash_file(path, hash, size) && hash == expected_hash &&
                    size == package_size,
                "stream hash covers an 80 MiB package without the whole-file size limit") ||
        !expect(!VirtualFileSystem::read_file(path, payload) && payload.empty(),
                "whole-file reads retain their independent 64 MiB limit") ||
        !expect(!VirtualFileSystem::hash_file(root / "missing", hash, size) && hash == 0 &&
                    size == 0,
                "failed stream hash clears both outputs"))
    {
        return false;
    }
    if (!write(root / "empty", {}) ||
        !expect(VirtualFileSystem::hash_file(root / "empty", hash, size) &&
                    hash == 14695981039346656037ULL && size == 0,
                "empty stream hash uses the FNV-1a offset basis") ||
        !expect(!VirtualFileSystem::hash_file("apk:/assets/../escape", hash, size) && hash == 0 &&
                    size == 0,
                "stream hash preserves APK path validation"))
    {
        return false;
    }
    if (!expect(catalog.assign(data) && files.configure(mounts) &&
                    ResourceSystem::read_resource(catalog, mounts, source_guid, payload) &&
                    payload == ByteBuffer{std::byte{1}, std::byte{2}, std::byte{0}},
                "read one entity range from a package larger than the whole-file limit"))
    {
        return false;
    }
    payload.assign(3, std::byte{1});
    if (!expect(!files.read(location, location.length, 1, payload) && payload.empty(),
                "member range cannot escape into the next packed entity"))
    {
        return false;
    }
    location.offset = std::numeric_limits<std::uint64_t>::max();
    if (!expect(!files.read(location, 0, 1, payload) && payload.empty(),
                "overflowing package range rejected"))
    {
        return false;
    }
    location.offset = 0;
    location.path = "../resources.pack";
    if (!expect(!files.read(location, 0, 1, payload), "relative traversal rejected"))
    {
        return false;
    }
    location.path = "link";
    std::error_code error;
    std::filesystem::create_symlink(path, root / "link", error);
    if (!error && !expect(!files.read(location, 0, 1, payload), "symlink member rejected"))
    {
        return false;
    }
    return true;
}

bool run(const std::filesystem::path& root)
{
    ResourceSystem resources;
    WorldSystem world;
    EngineConfig config;
    if (!resources.initialize(config) || !world.initialize(config))
    {
        return false;
    }
    auto* scene = world.create_scene("Catalog scene");
    auto* camera_node = scene ? scene->create_node("Camera") : nullptr;
    auto* mesh_node = scene ? scene->create_node("Triangle") : nullptr;
    auto* camera = camera_node ? camera_node->add_component<Camera>() : nullptr;
    auto* renderer = mesh_node ? mesh_node->add_component<MeshRenderer>() : nullptr;
    if (!camera || !renderer || !scene->set_active_camera(camera) ||
        !mesh_node->set_parent(camera_node))
    {
        return false;
    }
    schema::SceneDataT scene_data;
    if (!scene->build_data(scene_data))
    {
        return false;
    }
    world.destroy_scene();
    auto shader = *ResourceSystem::find_shader("builtin/shaders/pbr");
    shader.path = "project/shaders/test.shader";
    auto material = *ResourceSystem::find_material("builtin/materials/default-pbr");
    material.type_path = "project/materials/test.material";
    material.shader_guid = shader_guid;
    material.shader_path = shader.path;
    auto mesh = *ResourceSystem::find_mesh("builtin/meshes/cube");
    mesh.path = "project/meshes/test.mesh";
    for (auto& component : scene_data.nodes[1]->components)
    {
        if (auto* data = component->payload.AsMeshRendererData())
        {
            data->mesh_guid = mesh_guid;
            data->material_guid = material_guid;
        }
    }
    Fixture fixture;
    fixture.data.schema_version = 2;
    fixture.data.revision = "test-revision";
    fixture.data.target_platform = platform();
    if (!fixture.add(shader_guid, shader.path, schema::AssetKind::shader, pack(shader, "ESHD")) ||
        !fixture.add(material_guid, material.type_path, schema::AssetKind::material,
                     pack(material, "EMAT"), {shader_guid}) ||
        !fixture.add(mesh_guid, mesh.path, schema::AssetKind::mesh, pack(mesh, "EMSH")) ||
        !fixture.add(scene_guid, "project/scenes/main.scene", schema::AssetKind::scene,
                     pack(scene_data, "ESCN"), {mesh_guid, material_guid}) ||
        !fixture.add(source_guid, "project/data/raw.bin", schema::AssetKind::source,
                     {std::byte{1}, std::byte{2}, std::byte{0}}))
    {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(root / "gen", error);
    const MountList mounts{{"content", root}};
    ResourceCatalog catalog;
    if (error || !write(root / "catalog.bin", pack(fixture.data, "EASC")) ||
        !expect(ResourceSystem::read_catalog(root, catalog) &&
                    resources.update_catalog(catalog.data(), mounts),
                "metadata read and update do not require entity files"))
    {
        return false;
    }
    ResourcePtr loaded;
    if (!expect(!resources.load(scene_guid, loaded) && !loaded,
                "missing entity fails only at explicit load"))
    {
        return false;
    }
    for (const auto& [guid, bytes] : fixture.files)
    {
        if (!write(root / "gen" / guid, bytes))
        {
            return false;
        }
    }
    schema::ResourceHeaderDataT header;
    if (!expect(resources.read_header(scene_guid, header) && header.guid == scene_guid &&
                    header.payload_offset == fixture.data.entries[3]->header_size,
                "read and validate only entity header") ||
        !expect(ResourceSystem::find_mesh(mesh.path) == nullptr,
                "metadata and headers do not create resource objects") ||
        !expect(resources.load(scene_guid, loaded) && loaded->scene() &&
                    ResourceSystem::find_mesh(mesh.path) &&
                    ResourceSystem::find_shader(shader.path),
                "top-level load creates all necessary resources"))
    {
        return false;
    }
    ResourcePtr shared;
    if (!expect(resources.load(scene_guid, shared) && shared == loaded,
                "live resources are shared by identity") ||
        !expect(resources.update_catalog(fixture.data, mounts),
                "compatible metadata refresh keeps active resources"))
    {
        return false;
    }
    auto changed = fixture.data;
    changed.revision = "changed";
    changed.entries.front()->content_hash = "0000000000000000";
    if (!expect(!resources.update_catalog(changed, mounts) &&
                    resources.catalog().revision() == "test-revision",
                "active transitive dependency cannot change") ||
        !expect(!resources.update_catalog(fixture.data, {{"content", root / "other"}}),
                "active storage mount cannot change"))
    {
        return false;
    }
    changed = fixture.data;
    changed.entries.back()->content_hash = "0000000000000000";
    if (!expect(resources.update_catalog(changed, mounts) &&
                    resources.update_catalog(fixture.data, mounts),
                "unused entries may change while unrelated resources remain held"))
    {
        return false;
    }
    auto cycle = fixture.data;
    cycle.entries.front()->dependencies = {material_guid};
    if (!expect(!catalog.assign(cycle) && catalog.revision() == "test-revision",
                "dependency cycle rejected transactionally"))
    {
        return false;
    }
    auto missing = fixture.data;
    missing.entries.front()->dependencies = {"10000000-0000-4000-8000-000000000099"};
    if (!expect(!catalog.assign(missing), "missing necessary dependency rejected"))
    {
        return false;
    }
    auto foreign = fixture.data;
    foreign.target_platform = platform() == "android" ? "macos" : "android";
    if (!expect(catalog.assign(foreign) && !resources.update_catalog(foreign, mounts),
                "metadata tools accept foreign platforms but runtime activation rejects them"))
    {
        return false;
    }
    auto* restored = world.load_scene("project/scenes/main.scene");
    if (!expect(restored && restored->nodes().size() == 2 && restored->active_camera() &&
                    restored->nodes()[1]->parent() == restored->nodes()[0].get(),
                "world reconstructs scene while holding resource root"))
    {
        return false;
    }
    schema::SceneDataT rebuilt;
    if (!expect(restored->build_data(rebuilt) && rebuilt == scene_data,
                "runtime reconstruction preserves schema identity and references"))
    {
        return false;
    }
    std::weak_ptr<const Resource> weak = loaded;
    loaded.reset();
    shared.reset();
    if (!expect(!weak.expired(), "scene lifetime keeps its resource root alive"))
    {
        return false;
    }
    world.destroy_scene();
    if (!expect(weak.expired() && ResourceSystem::find_mesh(mesh.path) == nullptr &&
                    ResourceSystem::find_shader(shader.path) == nullptr,
                "weak index does not keep scene or dependencies resident"))
    {
        return false;
    }
    auto corrupt = fixture.files.find(scene_guid)->second;
    corrupt.back() ^= std::byte{1};
    if (!write(root / "gen" / scene_guid, corrupt) ||
        !expect(ResourceSystem::read_catalog(root, catalog) &&
                    resources.read_header(scene_guid, header),
                "metadata and header reads do not eagerly read or hash payload") ||
        !expect(!resources.load(scene_guid, loaded) && !loaded &&
                    ResourceSystem::find_shader(shader.path) == nullptr,
                "corrupted entity rejected and newly loaded dependencies rolled back"))
    {
        return false;
    }
    ByteBuffer payload;
    if (!expect(!ResourceSystem::decode_resource(*fixture.data.entries[3], corrupt, payload) &&
                    payload.empty(),
                "offline entity decoder enforces full-file checksum") ||
        !range_reads(root, fixture))
    {
        return false;
    }
    schema::AssetCatalogDataT empty;
    empty.schema_version = 2;
    empty.revision = "empty";
    empty.target_platform = platform();
    ResourcePtr builtin;
    return expect(resources.update_catalog(empty, {}) &&
                      resources.load("00000000-0000-4000-8000-000000000003", builtin) &&
                      builtin->material() && ResourceSystem::find_shader("builtin/shaders/pbr"),
                  "empty project catalog still loads compiled builtin dependencies");
}

} // namespace

int main()
{
    std::error_code error;
    const auto temporary =
        std::filesystem::temp_directory_path(error) /
        ("engine-catalog-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (error)
    {
        return 1;
    }
    const bool result = run(temporary);
    std::filesystem::remove_all(temporary, error);
    return result && !error ? 0 : 1;
}
