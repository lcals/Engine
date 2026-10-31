#pragma once

#include "plugins/runtime/terrain/terrain_volume.hpp"
#include "runtime/resource/resource.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "runtime/scene/component.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace engine::plugins::terrain
{

class TerrainSystem;

class TerrainComponent final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::TerrainData;
    [[nodiscard]] static bool valid_settings(const schema::TerrainDataT& settings) noexcept;
    [[nodiscard]] const schema::TerrainDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::TerrainDataT& settings);
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;
    [[nodiscard]] bool apply_sphere(const Vec3& center, float radius, bool fill);
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
    [[nodiscard]] const std::string& last_error() const noexcept;

private:
    friend class engine::Node;
    friend class TerrainSystem;
    explicit TerrainComponent(Node& node);
    ResourcePtr resource_;
    TerrainVolume volume_;
    std::uint64_t revision_ = 0;
    std::size_t triangles_ = 0;
    bool ready_ = false;
    bool pending_ = false;
    std::string last_error_;
};

} // namespace engine::plugins::terrain
