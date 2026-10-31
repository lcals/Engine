#pragma once

#include "plugins/runtime/terrain/terrain_fragment.hpp"
#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/terrain_generated.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::plugins::terrain
{

// Owns sampled data and a regular-cell mesh cache. No scene, GPU, physics or file access.
class TerrainVolume final
{
public:
    [[nodiscard]] static bool validate(const schema::TerrainAssetDataT& data, std::string& error);
    [[nodiscard]] static bool generate(const schema::TerrainGeneratorDataT& settings,
                                       schema::TerrainAssetDataT& output, std::string& error);
    [[nodiscard]] bool initialize(const schema::TerrainAssetDataT& data, std::string& error);
    [[nodiscard]] bool apply_sphere(const schema::Vec3& center, float radius, bool fill,
                                    std::string& error);
    [[nodiscard]] bool build_mesh(schema::MeshDataT& output, std::string& error);
    // Six-connected solid islands not touching the bottom/side anchoring shell. Atomic on failure.
    // Small islands remain in place; eligible islands split into bounded cell groups.
    [[nodiscard]] bool extract_detached(std::uint32_t min_samples, std::uint32_t max_fragments,
                                        std::uint32_t fragment_cells,
                                        std::vector<TerrainFragment>& output, std::string& error);
    [[nodiscard]] const schema::TerrainAssetDataT& data() const noexcept;
    [[nodiscard]] std::size_t chunk_count() const noexcept;
    [[nodiscard]] std::size_t rebuilt_chunks() const noexcept;

private:
    struct Chunk
    {
        schema::MeshDataT mesh;
        bool dirty = true;
    };
    schema::TerrainAssetDataT data_;
    std::vector<Chunk> chunks_;
    std::size_t rebuilt_chunks_ = 0;
};

} // namespace engine::plugins::terrain
