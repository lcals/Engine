#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"
#include "runtime/scene/environment_component.hpp"
#include "runtime/scene/light.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/post_process_component.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <string_view>
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
    auto* environment = root->add_component<engine::EnvironmentComponent>();
    auto* post_process = camera_node->add_component<engine::PostProcessComponent>();
    if (!expect(camera != nullptr && light != nullptr && mesh != nullptr &&
                    environment != nullptr && post_process != nullptr,
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
    light_settings.cast_shadows = true;
    light_settings.shadow_resolution = 1024;
    light_settings.shadow_bias = 0.002F;
    light_settings.shadow_cascades = 2;
    light_settings.shadow_distance = 120.0F;
    light_settings.shadow_split_lambda = 0.7F;
    light_settings.shadow_cascade_blend = 0.2F;
    engine::EnvironmentSettings environment_settings;
    environment_settings.ambient_color = {0.25F, 0.5F, 0.75F, 1.0F};
    environment_settings.ambient_intensity = 0.75F;
    environment_settings.ambient_tricolor = true;
    environment_settings.ambient_sky_color = {0.5F, 0.75F, 1.0F, 1.0F};
    environment_settings.ambient_equator_color = {0.4F, 0.3F, 0.2F, 1.0F};
    environment_settings.ambient_ground_color = {0.1F, 0.2F, 0.3F, 1.0F};
    environment_settings.fog_mode = engine::schema::FogMode::exponential_squared;
    environment_settings.fog_color = {0.7F, 0.8F, 0.9F, 1.0F};
    environment_settings.fog_start = 5.0F;
    environment_settings.fog_end = 250.0F;
    environment_settings.fog_density = 0.025F;
    environment_settings.clear_color = 0x123456ff;
    engine::PostProcessSettings post_process_settings;
    post_process_settings.exposure = 1.5F;
    post_process_settings.filmic_tonemap = true;
    post_process_settings.ao_enabled = true;
    post_process_settings.ao_radius = 1.4F;
    post_process_settings.ao_strength = .8F;
    post_process_settings.ao_power = 2.0F;
    post_process_settings.ao_fade_start = 40.0F;
    post_process_settings.ao_fade_end = 140.0F;
    post_process_settings.hdr_enabled = true;
    post_process_settings.bloom_enabled = true;
    post_process_settings.bloom_threshold = 1.2F;
    post_process_settings.bloom_intensity = .3F;
    post_process_settings.bloom_radius = 2.0F;
    auto& material = mesh->material();
    if (!expect(material.type_path() == "builtin/materials/default-pbr" &&
                    material.shader_path() == "builtin/shaders/pbr",
                "default material uses resource paths") ||
        !expect(cube_node->get_component<engine::Transform>()->set_settings(transform) &&
                    camera->set_settings(camera_settings) && light->set_settings(light_settings) &&
                    environment->set_settings(environment_settings) &&
                    post_process->set_settings(post_process_settings) &&
                    material.set_v4("baseColor", {0.75, 0.25, 0.125, 1.0}) &&
                    material.set_float("metallic", 0.625F) &&
                    material.set_float("roughness", 0.25F),
                "set generated component values and typed material parameters"))
    {
        return false;
    }
    auto invalid_environment = environment_settings;
    invalid_environment.ambient_color.mutate_r(std::numeric_limits<float>::quiet_NaN());
    auto invalid_post_process = post_process_settings;
    invalid_post_process.exposure = 0.0F;
    auto invalid_light = light_settings;
    invalid_light.kind = engine::schema::LightKind::point;
    if (!expect(!environment->set_settings(invalid_environment) &&
                    environment->settings() == environment_settings &&
                    !post_process->set_settings(invalid_post_process) &&
                    post_process->settings() == post_process_settings &&
                    !light->set_settings(invalid_light) && light->settings() == light_settings,
                "invalid scene effects preserve the previous component settings"))
    {
        return false;
    }
    invalid_environment = environment_settings;
    invalid_environment.fog_end = invalid_environment.fog_start;
    invalid_post_process.exposure = 101.0F;
    invalid_light = light_settings;
    invalid_light.shadow_resolution = 1000;
    if (!expect(!environment->set_settings(invalid_environment) &&
                    !post_process->set_settings(invalid_post_process) &&
                    !light->set_settings(invalid_light),
                "fog range, exposure limit and non-power-of-two shadow size are rejected"))
    {
        return false;
    }
    invalid_post_process = post_process_settings;
    invalid_post_process.ao_radius = std::numeric_limits<float>::quiet_NaN();
    if (!expect(!post_process->set_settings(invalid_post_process) &&
                    post_process->settings() == post_process_settings,
                "nonfinite AO radius preserves the complete post-process component"))
    {
        return false;
    }
    invalid_post_process = post_process_settings;
    invalid_post_process.ao_fade_end = invalid_post_process.ao_fade_start;
    if (!expect(!post_process->set_settings(invalid_post_process) &&
                    post_process->settings() == post_process_settings,
                "invalid AO fade interval preserves the complete post-process component"))
    {
        return false;
    }
    invalid_post_process = post_process_settings;
    invalid_post_process.hdr_enabled = false;
    if (!expect(!post_process->set_settings(invalid_post_process) &&
                    post_process->settings() == post_process_settings,
                "Bloom without HDR preserves the complete post-process component"))
    {
        return false;
    }
    invalid_post_process = post_process_settings;
    invalid_post_process.bloom_intensity = std::numeric_limits<float>::quiet_NaN();
    if (!expect(!post_process->set_settings(invalid_post_process) &&
                    post_process->settings() == post_process_settings,
                "nonfinite Bloom intensity preserves the complete post-process component"))
    {
        return false;
    }
    invalid_light = light_settings;
    invalid_light.shadow_cascades = 3;
    if (!expect(!light->set_settings(invalid_light) && light->settings() == light_settings,
                "unsupported cascade count leaves the light unchanged"))
    {
        return false;
    }
    invalid_light = light_settings;
    invalid_light.shadow_distance = std::numeric_limits<float>::quiet_NaN();
    if (!expect(!light->set_settings(invalid_light) && light->settings() == light_settings,
                "nonfinite cascade distance leaves the light unchanged"))
    {
        return false;
    }
    invalid_light = light_settings;
    invalid_light.shadow_split_lambda = 1.01F;
    if (!expect(!light->set_settings(invalid_light) && light->settings() == light_settings,
                "out-of-range cascade split leaves the light unchanged"))
    {
        return false;
    }
    invalid_light = light_settings;
    invalid_light.shadow_cascade_blend = 0.31F;
    if (!expect(!light->set_settings(invalid_light) && light->settings() == light_settings,
                "out-of-range cascade blend leaves the light unchanged"))
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
    const auto* decoded_root = find_node(*restored, "Root");
    if (!expect(decoded_cube != nullptr && decoded_camera != nullptr && decoded_light != nullptr &&
                    decoded_root != nullptr &&
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
    const auto* environment_data =
        find_component(*decoded_root, engine::EnvironmentComponent::component_type);
    const auto* post_process_data =
        find_component(*decoded_camera, engine::PostProcessComponent::component_type);
    if (!expect(transform_data != nullptr && mesh_data != nullptr && camera_data != nullptr &&
                    light_data != nullptr && environment_data != nullptr &&
                    post_process_data != nullptr,
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
                    decoded_light_settings->color.g() == 0.5F &&
                    *decoded_light_settings == light_settings,
                "camera and light generated data survive round trip") ||
        !expect(*environment_data->payload.AsEnvironmentData() == environment_settings &&
                    *post_process_data->payload.AsPostProcessData() == post_process_settings,
                "all environment and camera post-process fields survive FlatBuffers round trip"))
    {
        return false;
    }
    world.destroy_scene();
    engine::Scene* loaded = world.create_scene("Restored scene");
    if (!expect(loaded != nullptr, "create scene for restored component data"))
    {
        return false;
    }
    auto* loaded_root = loaded->create_node("Environment");
    auto* loaded_camera = loaded->create_node("Camera");
    auto* loaded_environment = loaded_root == nullptr
                                   ? nullptr
                                   : loaded_root->add_component<engine::EnvironmentComponent>();
    auto* loaded_post_process = loaded_camera == nullptr
                                    ? nullptr
                                    : loaded_camera->add_component<engine::PostProcessComponent>();
    if (!expect(loaded_environment != nullptr && loaded_post_process != nullptr &&
                    loaded_camera->add_component<engine::Camera>() != nullptr &&
                    loaded_environment->restore_data(*environment_data) &&
                    loaded_post_process->restore_data(*post_process_data) &&
                    loaded_environment->settings() == environment_settings &&
                    loaded_post_process->settings() == post_process_settings,
                "public component contract restores environment and post-process behavior"))
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
