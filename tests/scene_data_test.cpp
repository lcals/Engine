#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

namespace
{

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "scene data test: %s\n", message);
    }
    return condition;
}

const engine::schema::NodeDataT* find_node(const engine::schema::SceneDataT& scene,
                                           std::string_view name)
{
    const auto iterator =
        std::find_if(scene.nodes.begin(), scene.nodes.end(), [name](const auto& node)
                     { return node->info != nullptr && node->info->name == name; });
    return iterator == scene.nodes.end() ? nullptr : iterator->get();
}

const engine::schema::ComponentDataT* find_component(const engine::schema::NodeDataT& node,
                                                     engine::ComponentType type)
{
    const auto iterator =
        std::find_if(node.components.begin(), node.components.end(),
                     [type](const auto& item) { return item->payload.type == type; });
    return iterator == node.components.end() ? nullptr : iterator->get();
}

bool round_trip(std::vector<std::uint8_t>& bytes)
{
    const engine::EngineConfig config;
    engine::ResourceSystem resources;
    engine::WorldSystem world;
    if (!expect(resources.initialize(config), "initialize shared resource definitions") ||
        !expect(world.initialize(config), "initialize world"))
    {
        return false;
    }
    engine::Scene* scene = world.create_scene("Shared scene model");
    if (!expect(scene != nullptr, "create source scene"))
    {
        return false;
    }
    engine::Node* root = scene->create_node("Root");
    engine::Node* camera_node = scene->create_node("Camera");
    engine::Node* light_node = scene->create_node("Light");
    engine::Node* cube_node = scene->create_node("Cube");
    if (!expect(root != nullptr && camera_node != nullptr && light_node != nullptr &&
                    cube_node != nullptr,
                "create model nodes") ||
        !expect(camera_node->set_parent(root) && light_node->set_parent(root) &&
                    cube_node->set_parent(root),
                "set model hierarchy"))
    {
        return false;
    }
    engine::Camera* camera = camera_node->add_component<engine::Camera>();
    engine::Light* light = light_node->add_component<engine::Light>();
    engine::MeshRenderer* mesh = cube_node->add_component<engine::MeshRenderer>();
    if (!expect(camera != nullptr && light != nullptr && mesh != nullptr,
                "create model components") ||
        !expect(scene->set_active_camera(camera), "select camera"))
    {
        return false;
    }
    engine::TransformSettings transform;
    transform.position = {1.5, 2.5, -3.5};
    transform.rotation = {10.0, 20.0, 30.0};
    transform.scale = {2.0, 3.0, 4.0};
    engine::CameraSettings camera_settings;
    camera_settings.fov_degrees = 75.0F;
    camera_settings.near_clip = 0.2F;
    camera_settings.far_clip = 200.0F;
    engine::LightSettings light_settings;
    light_settings.color = {0.25F, 0.5F, 0.75F, 1.0F};
    light_settings.intensity = 4.5F;
    auto& material = mesh->material();
    if (!expect(material.type_path() == "builtin/materials/default-pbr" &&
                    material.shader_path() == "builtin/shaders/pbr",
                "default material uses resource paths") ||
        !expect(cube_node->get_component<engine::Transform>()->set_settings(transform) &&
                    camera->set_settings(camera_settings) && light->set_settings(light_settings) &&
                    material.set_v4("baseColor", {0.75, 0.25, 0.125, 1.0}) &&
                    material.set_float("metallic", 0.625F) &&
                    material.set_float("roughness", 0.25F),
                "set generated component values and typed material parameters"))
    {
        return false;
    }
    mesh->set_enabled(false);
    engine::schema::SceneDataT snapshot;
    if (!expect(scene->build_data(snapshot) && snapshot.nodes.size() == 4 &&
                    snapshot.info != nullptr,
                "build independent generated snapshot"))
    {
        return false;
    }
    // 测试中的交换快照使用超过 JS Number 精确整数范围的标识，运行时节点不变。
    constexpr std::uint64_t identity_offset = std::uint64_t{1} << 54;
    for (auto& node : snapshot.nodes)
    {
        node->info->id += identity_offset;
        if (node->info->parent_id != 0)
        {
            node->info->parent_id += identity_offset;
        }
    }
    snapshot.info->active_camera_node += identity_offset;
    if (!expect(material.set_float("metallic", 0.0F), "modify runtime after taking snapshot"))
    {
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    engine::schema::FinishSceneDataBuffer(builder,
                                          engine::schema::SceneData::Pack(builder, &snapshot));
    bytes.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    if (!expect(engine::schema::VerifySceneDataBuffer(verifier), "verify packed scene bytes") ||
        !expect(engine::schema::SceneDataBufferHasIdentifier(bytes.data()),
                "buffer has shared ESCN identifier"))
    {
        return false;
    }
    std::unique_ptr<engine::schema::SceneDataT> restored(
        engine::schema::GetSceneData(bytes.data())->UnPack());
    if (!expect(restored != nullptr && restored->info != nullptr &&
                    restored->info->name == "Shared scene model" && restored->nodes.size() == 4 &&
                    restored->info->active_camera_node == camera_node->info().id + identity_offset,
                "scene metadata and active camera survive round trip"))
    {
        return false;
    }
    const auto* decoded_cube = find_node(*restored, "Cube");
    const auto* decoded_camera = find_node(*restored, "Camera");
    const auto* decoded_light = find_node(*restored, "Light");
    if (!expect(decoded_cube != nullptr && decoded_camera != nullptr && decoded_light != nullptr &&
                    decoded_cube->info->id == cube_node->info().id + identity_offset &&
                    decoded_cube->info->parent_id == root->info().id + identity_offset &&
                    decoded_camera->info->parent_id == root->info().id + identity_offset &&
                    decoded_light->info->parent_id == root->info().id + identity_offset,
                "node identities and parent links survive round trip"))
    {
        return false;
    }
    const auto* transform_data = find_component(*decoded_cube, engine::Transform::component_type);
    const auto* mesh_data = find_component(*decoded_cube, engine::MeshRenderer::component_type);
    const auto* camera_data = find_component(*decoded_camera, engine::Camera::component_type);
    const auto* light_data = find_component(*decoded_light, engine::Light::component_type);
    if (!expect(transform_data != nullptr && mesh_data != nullptr && camera_data != nullptr &&
                    light_data != nullptr,
                "union discriminators preserve component types"))
    {
        return false;
    }
    const auto* decoded_transform = transform_data->payload.AsTransformData();
    const auto* decoded_mesh = mesh_data->payload.AsMeshRendererData();
    const auto* decoded_camera_settings = camera_data->payload.AsCameraData();
    const auto* decoded_light_settings = light_data->payload.AsLightData();
    if (!expect(decoded_transform->position.x() == 1.5 && decoded_transform->position.y() == 2.5 &&
                    decoded_transform->position.z() == -3.5 &&
                    decoded_transform->rotation.z() == 30.0 &&
                    decoded_transform->scale.x() == 2.0 && decoded_transform->scale.y() == 3.0 &&
                    decoded_transform->scale.z() == 4.0,
                "generated vector values survive round trip") ||
        !expect(!mesh_data->enabled &&
                    decoded_mesh->mesh_guid == "00000000-0000-4000-8000-000000000005" &&
                    decoded_mesh->material_guid == "00000000-0000-4000-8000-000000000003",
                "material and enabled state remain independent of later runtime edits") ||
        !expect(decoded_camera_settings->fov_degrees == 75.0F &&
                    decoded_camera_settings->near_clip == 0.2F &&
                    decoded_camera_settings->far_clip == 200.0F &&
                    decoded_light_settings->intensity == 4.5F &&
                    decoded_light_settings->color.g() == 0.5F,
                "camera and light generated data survive round trip"))
    {
        return false;
    }
    std::vector<std::uint8_t> corrupted = bytes;
    corrupted[4] = 0;
    flatbuffers::Verifier wrong_identifier(corrupted.data(), corrupted.size());
    flatbuffers::Verifier truncated(bytes.data(), bytes.size() / 2);
    return expect(!engine::schema::VerifySceneDataBuffer(wrong_identifier),
                  "reject incorrect format identifier") &&
           expect(!engine::schema::VerifySceneDataBuffer(truncated), "reject truncated scene data");
}

bool load_buffer(const char* path, std::vector<std::uint8_t>& bytes)
{
    bytes.clear();
    std::ifstream input(engine::PathUtils::from_utf8(path), std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    if (!expect(input && size > 0 && size <= 1024 * 1024, "read bounded test buffer"))
    {
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    return expect(static_cast<bool>(input) && engine::schema::VerifySceneDataBuffer(verifier),
                  "verify TypeScript exchange buffer before accessing it");
}

bool check_typescript_result(const char* changed_path, const char* original_path)
{
    std::vector<std::uint8_t> changed_bytes;
    std::vector<std::uint8_t> original_bytes;
    if (!load_buffer(changed_path, changed_bytes) || !load_buffer(original_path, original_bytes))
    {
        return false;
    }
    std::unique_ptr<engine::schema::SceneDataT> changed(
        engine::schema::GetSceneData(changed_bytes.data())->UnPack());
    std::unique_ptr<engine::schema::SceneDataT> original(
        engine::schema::GetSceneData(original_bytes.data())->UnPack());
    if (!expect(changed != nullptr && original != nullptr && changed->info != nullptr &&
                    original->info != nullptr && changed->nodes.size() == 4 &&
                    changed->info->name == "TypeScript 场景" &&
                    changed->info->active_camera_node == original->info->active_camera_node,
                "TypeScript updates UTF8 metadata while retaining 64-bit active camera"))
    {
        return false;
    }
    for (const auto& source : original->nodes)
    {
        if (!expect(source != nullptr && source->info != nullptr,
                    "source node metadata is present"))
        {
            return false;
        }
        const auto* target = find_node(*changed, source->info->name);
        if (!expect(target != nullptr && target->info->id == source->info->id &&
                        target->info->parent_id == source->info->parent_id &&
                        target->info->id > (std::uint64_t{1} << 53),
                    "TypeScript bigint preserves every 64-bit identity and parent link"))
        {
            return false;
        }
    }
    const auto* cube = find_node(*changed, "Cube");
    const auto* camera = find_node(*changed, "Camera");
    const auto* light = find_node(*changed, "Light");
    if (!expect(cube != nullptr && camera != nullptr && light != nullptr,
                "TypeScript retains scene nodes"))
    {
        return false;
    }
    const auto* transform = find_component(*cube, engine::Transform::component_type);
    const auto* mesh = find_component(*cube, engine::MeshRenderer::component_type);
    const auto* camera_component = find_component(*camera, engine::Camera::component_type);
    const auto* light_component = find_component(*light, engine::Light::component_type);
    if (!expect(transform != nullptr && mesh != nullptr && camera_component != nullptr &&
                    light_component != nullptr,
                "TypeScript preserves union payload types"))
    {
        return false;
    }
    const auto* transform_data = transform->payload.AsTransformData();
    const auto* mesh_data = mesh->payload.AsMeshRendererData();
    const auto* camera_data = camera_component->payload.AsCameraData();
    const auto* light_data = light_component->payload.AsLightData();
    return expect(transform_data != nullptr && transform_data->position.x() == 42.5 &&
                      transform_data->position.y() == 2.5 && transform_data->scale.x() == 2.0 &&
                      transform_data->scale.y() == 3.0 && transform_data->scale.z() == 4.0,
                  "TypeScript edits generated transform data") &&
           expect(mesh_data != nullptr && mesh->enabled &&
                      mesh_data->mesh_guid == "00000000-0000-4000-8000-000000000005" &&
                      mesh_data->material_guid == "00000000-0000-4000-8000-000000000004",
                  "TypeScript edits nested material and component state") &&
           expect(camera_data != nullptr && camera_data->fov_degrees == 55.0F &&
                      camera_data->near_clip == 0.2F && camera_data->far_clip == 200.0F,
                  "TypeScript edits camera while retaining untouched settings") &&
           expect(light_data != nullptr && light_data->intensity == 7.5F &&
                      light_data->color.g() == 0.5F,
                  "TypeScript edits light union payload");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 4 && std::strcmp(argv[1], "--read-buffer") == 0)
    {
        if (!check_typescript_result(argv[2], argv[3]))
        {
            return 1;
        }
        std::puts("Generated C++/TypeScript scene data round trip and bigint identities passed.");
        return 0;
    }
    if (argc != 1 && !(argc == 3 && std::strcmp(argv[1], "--write-buffer") == 0))
    {
        std::fputs(
            "Usage: scene_data_test [--write-buffer PATH | --read-buffer TS_PATH CPP_PATH]\n",
            stderr);
        return 2;
    }
    std::vector<std::uint8_t> bytes;
    if (!round_trip(bytes))
    {
        return 1;
    }
    if (argc == 3)
    {
        std::ofstream output(engine::PathUtils::from_utf8(argv[2]), std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!expect(static_cast<bool>(output), "write requested cross-language test fixture"))
        {
            return 1;
        }
    }
    std::puts("FlatBuffers scene Pack, Verifier, UnPack and snapshot isolation passed.");
    return 0;
}
