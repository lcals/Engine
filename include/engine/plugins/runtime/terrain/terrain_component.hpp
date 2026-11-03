#pragma once

#include "plugins/runtime/terrain/terrain_volume.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "runtime/scene/component.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace engine::plugins::terrain
{

class TerrainSystem;

struct TerrainComponentRuntime
{
    ResourcePtr resource_;
    TerrainVolume volume_;
    // Weak tokens identify this exact component even if a node removes and recreates it.
    std::shared_ptr<const std::uint8_t> lifetime_ = std::make_shared<const std::uint8_t>(0);
    std::uint64_t revision_ = 0;
    std::size_t triangles_ = 0;
    bool ready_ = false;
    bool pending_ = false;
    // Remains true if generated nodes were externally removed or the system was shut down.
    // A static-only state must never silently omit previously extracted soil.
    bool detached_ = false;
    std::string last_error_;
};

class TerrainComponent final : public Component
{
public:
    using Data = schema::TerrainDataT;
    TerrainComponent() noexcept = default;
    explicit TerrainComponent(Component component) noexcept : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    using Runtime = TerrainComponentRuntime;
    [[nodiscard]] Runtime* runtime() const noexcept;
    static constexpr ComponentType component_type = ComponentType::TerrainData;
    [[nodiscard]] static bool valid_settings(const schema::TerrainDataT& settings) noexcept;
    [[nodiscard]] schema::TerrainDataT settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const schema::TerrainDataT& settings);
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
    [[nodiscard]] bool apply_sphere(const Vec3& center, float radius, bool fill);
    [[nodiscard]] bool collapse_detached(std::uint32_t min_samples, std::uint32_t max_fragments,
                                         std::uint32_t fragment_cells = 4, float density = 1600);
    [[nodiscard]] bool retire_fragments(std::uint32_t max_count, double max_age_seconds,
                                        std::uint32_t& retired_count);
    [[nodiscard]] std::size_t fragment_count() const noexcept;
    [[nodiscard]] bool reset();
    [[nodiscard]] bool snapshot(ByteBuffer& output) const;
    [[nodiscard]] bool restore(ByteView bytes);
    [[nodiscard]] bool save_state(const std::filesystem::path& path) const;
    [[nodiscard]] bool load_state(const std::filesystem::path& path);
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] std::size_t chunk_count() const noexcept;
    [[nodiscard]] std::size_t rebuilt_chunks() const noexcept;
    [[nodiscard]] std::size_t triangle_count() const noexcept;
    [[nodiscard]] std::string last_error() const noexcept;
};

} // namespace engine::plugins::terrain
