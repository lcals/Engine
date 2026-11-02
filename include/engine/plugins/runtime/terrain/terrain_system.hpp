#pragma once

#include "runtime/system/system.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace engine
{
class WorldSystem;
class PhysicsSystem;
class TaskSystem;
namespace schema
{
struct Vec3;
struct MeshDataT;
} // namespace schema
} // namespace engine

namespace engine::plugins::terrain
{
class TerrainComponent;
class TerrainVolume;

class TerrainSystem final : public System
{
public:
    // Borrowed world/physics must outlive this system. Called synchronously at frame boundaries.
    TerrainSystem(WorldSystem& world, PhysicsSystem& physics, TaskSystem* tasks = nullptr,
                  std::size_t max_pending_requests = 64,
                  std::size_t max_commits_per_tick = 1) noexcept;
    ~TerrainSystem() noexcept override;
    [[nodiscard]] static TerrainSystem* instance() noexcept;
    [[nodiscard]] bool commit(TerrainComponent& component, TerrainVolume candidate);
    // true 仅表示排队；完成委托由 synchronize/tick 在主线程调用，取消结果为 false。
    // 不接受无任务域或空委托；关闭丢弃委托，后台候选不借用场景/组件。
    [[nodiscard]] bool request_apply_sphere(TerrainComponent& component, const schema::Vec3& center,
                                            float radius, bool fill,
                                            std::function<void(bool)> completed);
    [[nodiscard]] bool collapse_detached(TerrainComponent& component, std::uint32_t min_samples,
                                         std::uint32_t max_fragments, std::uint32_t fragment_cells,
                                         float density);
    void clear_fragments(TerrainComponent& component) noexcept;
    [[nodiscard]] std::size_t fragment_count(const TerrainComponent& component) const noexcept;
    [[nodiscard]] bool synchronize();

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    [[nodiscard]] bool commit_prepared(TerrainComponent& component, TerrainVolume candidate,
                                       schema::MeshDataT mesh);
    void process_requests();
    struct AsyncState;
    WorldSystem& world_;
    PhysicsSystem& physics_;
    TaskSystem* tasks_ = nullptr;
    std::size_t max_pending_requests_ = 64;
    std::size_t max_commits_per_tick_ = 1;
    std::unique_ptr<AsyncState> async_;
    std::uint64_t scene_id_ = 0;
    std::set<std::uint64_t> generated_;
    std::map<std::uint64_t, std::vector<std::uint64_t>> fragments_;
};

} // namespace engine::plugins::terrain
