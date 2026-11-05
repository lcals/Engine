#pragma once

#include "plugins/runtime/destruction/destruction_graph.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "runtime/scene/component.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace engine
{
class Collider;
}

namespace engine::plugins::destruction
{

class DestructionSystem;

struct DestructionComponentRuntime;

class DestructionComponent final : public Component
{
public:
    using Data = schema::DestructionDataT;
    DestructionComponent() noexcept = default;
    explicit DestructionComponent(Component component) noexcept
        : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    using Runtime = DestructionComponentRuntime;
    static constexpr ComponentType component_type = ComponentType::DestructionData;
    [[nodiscard]] static bool valid_settings(const schema::DestructionDataT& settings) noexcept;
    [[nodiscard]] schema::DestructionDataT settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const schema::DestructionDataT& settings);
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
    // World-space sphere; impulse is applied to dynamic groups after successful damage.
    [[nodiscard]] bool damage_sphere(const Vec3& center, float radius, float damage,
                                     float impulse = 0);
    // Explicit single-collider foundation check, after its physics geometry is committed.
    [[nodiscard]] bool refresh_support(const Collider& support, float max_gap, float min_coverage);
    // One explicit gravity-stress solve; pressure limits are Pa. Failure clears broken_bonds.
    // Call at a fixed cadence: official Blast partial damage accumulates per call, not per dt.
    [[nodiscard]] bool apply_stress(const Vec3& gravity, float elastic_limit, float fatal_limit,
                                    std::uint32_t iterations, std::uint32_t& broken_bonds);
    [[nodiscard]] bool owns_fragment(std::uint64_t node_id) const noexcept;
    // Explicit owner budget; zero age disables expiry, zero count retires all dynamic groups.
    [[nodiscard]] bool retire_fragments(std::uint32_t max_count, double max_age_seconds,
                                        std::uint32_t& retired_count);
    [[nodiscard]] bool reset();
    [[nodiscard]] bool snapshot(ByteBuffer& output) const;
    [[nodiscard]] bool restore(ByteView bytes);
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] std::size_t chunk_count() const noexcept;
    [[nodiscard]] std::size_t group_count() const noexcept;
    [[nodiscard]] std::size_t dynamic_groups() const noexcept;
    [[nodiscard]] std::string last_error() const noexcept;

private:
    template <class T> friend auto engine::ecs_detail::component_runtime(T component) noexcept;
    [[nodiscard]] Runtime* runtime() const noexcept;
};

} // namespace engine::plugins::destruction
