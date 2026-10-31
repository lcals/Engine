#pragma once

#include "plugins/runtime/destruction/destruction_graph.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "runtime/scene/component.hpp"

#include <cstdint>
#include <string>

namespace engine
{
class Collider;
}

namespace engine::plugins::destruction
{

class DestructionSystem;

class DestructionComponent final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::DestructionData;
    [[nodiscard]] static bool valid_settings(const schema::DestructionDataT& settings) noexcept;
    [[nodiscard]] const schema::DestructionDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::DestructionDataT& settings);
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;
    // World-space sphere; impulse is applied to dynamic groups after successful damage.
    [[nodiscard]] bool damage_sphere(const Vec3& center, float radius, float damage,
                                     float impulse = 0);
    // Explicit single-collider foundation check, after its physics geometry is committed.
    [[nodiscard]] bool refresh_support(const Collider& support, float max_gap, float min_coverage);
    [[nodiscard]] bool owns_fragment(std::uint64_t node_id) const noexcept;
    [[nodiscard]] bool reset();
    [[nodiscard]] bool snapshot(ByteBuffer& output) const;
    [[nodiscard]] bool restore(ByteView bytes);
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] std::size_t chunk_count() const noexcept;
    [[nodiscard]] std::size_t group_count() const noexcept;
    [[nodiscard]] std::size_t dynamic_groups() const noexcept;
    [[nodiscard]] const std::string& last_error() const noexcept;

private:
    friend class engine::Node;
    friend class DestructionSystem;
    explicit DestructionComponent(Node& node);
    ResourcePtr resource_;
    DestructionGraph graph_;
    std::uint64_t revision_ = 0;
    bool ready_ = false;
    bool pending_ = false;
    std::string last_error_;
};

} // namespace engine::plugins::destruction
