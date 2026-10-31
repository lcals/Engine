#pragma once

#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/terrain_generated.h"

#include <cstddef>
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
