#pragma once

#include "plugins/editor/scene_bake/ibl_bake_job.hpp"
#include "runtime/schema/light_probe_generated.h"
#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/scene_generated.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine
{
class RenderSystem;
class ResourceSystem;
class TaskSystem;
class Scene;
} // namespace engine
namespace engine::plugins::scene_bake
{
struct ProbeBakeTarget
{
    std::uint64_t node_id = 0;
    schema::Vec3 bounds_min{-5, -5, -5};
    schema::Vec3 bounds_max{5, 5, 5};
    std::array<std::uint32_t, 3> grid{3, 3, 3};
};
struct LightingBakeSettings
{
    std::uint16_t lightmap_resolution = 64;
    std::uint16_t hemisphere_resolution = 16;
    std::uint8_t bounce_count = 2;
    std::uint8_t interpolation_passes = 2;
    float interpolation_threshold = 0.01F;
    std::vector<std::uint64_t> light_node_ids;
    std::vector<ProbeBakeTarget> probes;
    std::string source_fingerprint;
};
struct BakedMeshLighting
{
    std::uint64_t node_id = 0;
    std::array<float, 16> transform{};
    std::shared_ptr<schema::MeshDataT> mesh;
    std::shared_ptr<schema::TextureDataT> lightmap;
};
struct BakedProbeLighting
{
    std::uint64_t node_id = 0;
    schema::LightProbeDataT data;
};
struct LightingBakeOutputs
{
    std::vector<BakedMeshLighting> meshes;
    std::vector<BakedProbeLighting> probes;
};
class LightingBakeJob final
{
public:
    LightingBakeJob(RenderSystem& renderer, ResourceSystem& resources, TaskSystem& tasks);
    ~LightingBakeJob() noexcept;
    LightingBakeJob(const LightingBakeJob&) = delete;
    LightingBakeJob& operator=(const LightingBakeJob&) = delete;
    [[nodiscard]] bool start(const schema::SceneDataT& scene, const Scene& active_scene,
                             const LightingBakeSettings& settings, std::string& error);
    [[nodiscard]] BakeProgress tick(std::string& error);
    void cancel() noexcept;
    void shutdown() noexcept;
    [[nodiscard]] std::uint32_t completed() const noexcept;
    [[nodiscard]] std::uint32_t total() const noexcept;
    [[nodiscard]] const LightingBakeOutputs& outputs() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace engine::plugins::scene_bake
