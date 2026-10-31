#include "plugins/editor/destruction/destruction_authoring.hpp"

#include "plugins/runtime/destruction/destruction_graph.hpp"

#include <NvBlastExtAuthoring.h>
#include <NvBlastExtAuthoringBondGenerator.h>
#include <NvBlastExtAuthoringFractureTool.h>
#include <NvBlastExtAuthoringMesh.h>
#include <NvBlastGlobals.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <vector>

namespace engine::plugins::destruction
{
namespace
{
struct Release
{
    template <typename T> void operator()(T* object) const noexcept
    {
        if (object)
        {
            object->release();
        }
    }
};

struct BlastFree
{
    template <typename T> void operator()(T* object) const noexcept
    {
        NVBLAST_FREE(object);
    }
};

class Random final : public Nv::Blast::RandomGeneratorBase
{
public:
    explicit Random(std::uint32_t value) : generator_(value)
    {
    }
    float getRandomValue() override
    {
        // Explicit conversion gives the same sequence with libc++ and MSVC standard libraries.
        return static_cast<float>(generator_() >> 8) / 16777216.0F;
    }
    void seed(std::int32_t value) override
    {
        generator_.seed(static_cast<std::uint32_t>(value));
    }

private:
    std::mt19937 generator_;
};

bool validate_settings(const schema::DestructionGeneratorDataT& settings, std::string& error)
{
    const auto& size = settings.size;
    if (!std::isfinite(size.x()) || !std::isfinite(size.y()) || !std::isfinite(size.z()) ||
        size.x() <= 0 || size.y() <= 0 || size.z() <= 0 || size.x() > 1000 || size.y() > 1000 ||
        size.z() > 1000 || !std::isfinite(settings.anchor_height) || settings.anchor_height < 0 ||
        settings.anchor_height > size.y() || !std::isfinite(settings.bond_health) ||
        settings.bond_health <= 0 || settings.bond_health > 1e6F)
    {
        error = "Destruction generator size, anchor height or bond health is invalid";
        return false;
    }
    if (settings.fracture == schema::DestructionFracture::slices)
    {
        if (settings.slices_x == 0 || settings.slices_y == 0 || settings.slices_z == 0 ||
            settings.slices_x > 256 || settings.slices_y > 256 || settings.slices_z > 256 ||
            std::uint64_t(settings.slices_x) * settings.slices_y * settings.slices_z > 256)
        {
            error = "Destruction slices require 1..256 resulting fragments";
            return false;
        }
    }
    else if (settings.fracture == schema::DestructionFracture::voronoi)
    {
        if (settings.sites < 2 || settings.sites > 256)
        {
            error = "Destruction Voronoi requires 2..256 sites";
            return false;
        }
    }
    else
    {
        error = "Unknown destruction fracture method";
        return false;
    }
    return true;
}

std::unique_ptr<Nv::Blast::Mesh, Release> create_box(const schema::Vec3& size)
{
    const float x = static_cast<float>(size.x() * 0.5);
    const float z = static_cast<float>(size.z() * 0.5);
    const float y = static_cast<float>(size.y());
    const std::array<NvcVec3, 8> corners{{{-x, 0, -z},
                                          {x, 0, -z},
                                          {x, y, -z},
                                          {-x, y, -z},
                                          {-x, 0, z},
                                          {x, 0, z},
                                          {x, y, z},
                                          {-x, y, z}}};
    constexpr std::array<std::uint32_t, 36> indices{{0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7,
                                                     0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5,
                                                     0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2}};
    constexpr std::array<NvcVec3, 6> normals{
        {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}}};
    std::array<NvcVec3, 36> positions;
    std::array<NvcVec3, 36> vertex_normals;
    std::array<NvcVec2, 36> texcoords;
    std::array<std::uint32_t, 36> sequential;
    for (std::uint32_t i = 0; i < indices.size(); ++i)
    {
        positions[i] = corners[indices[i]];
        vertex_normals[i] = normals[i / 6];
        texcoords[i] = {positions[i].x + positions[i].z, positions[i].y};
        sequential[i] = i;
    }
    return std::unique_ptr<Nv::Blast::Mesh, Release>(NvBlastExtAuthoringCreateMesh(
        positions.data(), vertex_normals.data(), texcoords.data(),
        static_cast<std::uint32_t>(positions.size()), sequential.data(),
        static_cast<std::uint32_t>(sequential.size())));
}
} // namespace

bool DestructionAuthoring::generate(const schema::DestructionGeneratorDataT& settings,
                                    schema::DestructionAssetDataT& output, std::string& error)
{
    error.clear();
    output = {};
    if (!validate_settings(settings, error))
    {
        return false;
    }
    auto source = create_box(settings.size);
    std::unique_ptr<Nv::Blast::FractureTool, Release> tool(NvBlastExtAuthoringCreateFractureTool());
    if (!source || !tool)
    {
        error = "Blast cannot create authoring input";
        return false;
    }
    const Nv::Blast::Mesh* meshes[] = {source.get()};
    if (!tool->setSourceMeshes(meshes, 1))
    {
        error = "Blast rejected the authoring mesh";
        return false;
    }
    Random random(settings.seed);
    int result = 0;
    if (settings.fracture == schema::DestructionFracture::voronoi)
    {
        std::unique_ptr<Nv::Blast::VoronoiSitesGenerator, Release> sites(
            NvBlastExtAuthoringCreateVoronoiSitesGenerator(source.get(), &random));
        if (!sites)
        {
            error = "Blast cannot create Voronoi sites generator";
            return false;
        }
        sites->uniformlyGenerateSitesInMesh(settings.sites);
        const NvcVec3* positions = nullptr;
        const auto count = sites->getVoronoiSites(positions);
        if (count < 2)
        {
            error = "Blast generated too few Voronoi sites";
            return false;
        }
        result = tool->voronoiFracturing(0, count, positions, false);
    }
    else if (settings.slices_x > 1 || settings.slices_y > 1 || settings.slices_z > 1)
    {
        Nv::Blast::SlicingConfiguration configuration;
        configuration.x_slices = static_cast<std::int32_t>(settings.slices_x - 1);
        configuration.y_slices = static_cast<std::int32_t>(settings.slices_y - 1);
        configuration.z_slices = static_cast<std::int32_t>(settings.slices_z - 1);
        result = tool->slicing(0, configuration, false, &random);
    }
    if (result != 0)
    {
        error = "Blast fracture operation failed: " + std::to_string(result);
        return false;
    }
    tool->finalizeFracturing();
    const auto chunk_count = tool->getChunkCount();
    if (chunk_count == 0 || chunk_count > 257)
    {
        error = "Blast generated an unsupported fragment count";
        return false;
    }
    std::vector<Nv::Blast::Triangle> geometry;
    std::vector<std::uint32_t> geometry_offsets{0};
    schema::DestructionAssetDataT candidate;
    for (std::uint32_t i = 0; i < chunk_count; ++i)
    {
        bool leaf = true;
        for (std::uint32_t child = 0; child < chunk_count; ++child)
        {
            if (tool->getChunkInfo(static_cast<std::int32_t>(child)).parentChunkId ==
                tool->getChunkId(static_cast<std::int32_t>(i)))
            {
                leaf = false;
                break;
            }
        }
        if (!leaf)
        {
            continue;
        }
        Nv::Blast::Triangle* raw_triangles = nullptr;
        const auto triangle_count = tool->getBaseMesh(static_cast<std::int32_t>(i), raw_triangles);
        std::unique_ptr<Nv::Blast::Triangle[]> triangles(raw_triangles);
        if (triangle_count == 0 || !triangles)
        {
            error = "Blast produced an empty fragment";
            return false;
        }
        auto chunk = std::make_unique<schema::DestructionChunkDataT>();
        std::vector<Nv::Blast::Vertex> triangle_vertices;
        triangle_vertices.reserve(static_cast<std::size_t>(triangle_count) * 3);
        chunk->mesh = std::make_unique<schema::MeshDataT>();
        chunk->mesh->path = "destruction/chunk/" + std::to_string(candidate.chunks.size());
        chunk->anchor_health = settings.bond_health;
        float bottom = std::numeric_limits<float>::max();
        for (std::uint32_t triangle = 0; triangle < triangle_count; ++triangle)
        {
            const auto& value = triangles[triangle];
            const Nv::Blast::Vertex* vertices[] = {&value.a, &value.b, &value.c};
            for (const auto* vertex : vertices)
            {
                triangle_vertices.push_back(*vertex);
                chunk->mesh->indices.push_back(
                    static_cast<std::uint32_t>(chunk->mesh->vertices.size()));
                chunk->mesh->vertices.emplace_back(vertex->p.x, vertex->p.y, vertex->p.z,
                                                   vertex->n.x, vertex->n.y, vertex->n.z);
                chunk->mesh->texcoords.emplace_back(vertex->uv[0].x, vertex->uv[0].y);
                bottom = std::min(bottom, vertex->p.y);
            }
        }
        // The official ProcessFracture path also integrates its finalized triangle geometry.
        // Polygon export can contain multiple loops; triangulated mesh integration avoids
        // interpreting those loops as a single polygon fan and matches rendering/collision.
        std::unique_ptr<Nv::Blast::Mesh, Release> mesh(NvBlastExtAuthoringCreateMeshOnlyTriangles(
            triangle_vertices.data(), static_cast<std::uint32_t>(triangle_vertices.size()),
            chunk->mesh->indices.data(), static_cast<std::uint32_t>(chunk->mesh->indices.size()),
            nullptr, 0));
        if (!mesh)
        {
            error = "Blast cannot create a triangle mesh for fragment mass properties";
            return false;
        }
        NvcVec3 centroid{};
        chunk->volume = mesh->getMeshVolumeAndCentroid(centroid);
        chunk->centroid = schema::Vec3(centroid.x, centroid.y, centroid.z);
        chunk->anchored = bottom <= settings.anchor_height;
        geometry.insert(geometry.end(), triangles.get(), triangles.get() + triangle_count);
        geometry_offsets.push_back(static_cast<std::uint32_t>(geometry.size()));
        candidate.chunks.push_back(std::move(chunk));
    }
    std::unique_ptr<Nv::Blast::BlastBondGenerator, Release> generator(
        NvBlastExtAuthoringCreateBondGenerator(nullptr));
    if (!generator)
    {
        error = "Blast cannot create the exact fracture bond generator";
        return false;
    }
    NvBlastBondDesc* raw_bonds = nullptr;
    const auto fragment_count = static_cast<std::uint32_t>(candidate.chunks.size());
    auto support = std::make_unique<bool[]>(fragment_count);
    std::fill_n(support.get(), fragment_count, true);
    // EXACT clips coplanar triangles to their actual contact area. The faster cutting-plane
    // descriptor path also connects fragments that only touch at an edge/corner in a grid.
    const Nv::Blast::BondGenerationConfig bond_settings{0, Nv::Blast::BondGenerationConfig::EXACT};
    const auto bond_count =
        generator->bondsFromPrefractured(fragment_count, geometry_offsets.data(), geometry.data(),
                                         support.get(), raw_bonds, bond_settings);
    std::unique_ptr<NvBlastBondDesc, BlastFree> bonds(raw_bonds);
    if (bond_count < 0 || bond_count > 8192 || (bond_count > 0 && !bonds))
    {
        error = "Blast cannot build the fracture bond graph";
        return false;
    }
    for (std::int32_t i = 0; i < bond_count; ++i)
    {
        const auto& desc = raw_bonds[i];
        if (desc.chunkIndices[0] >= fragment_count || desc.chunkIndices[1] >= fragment_count)
        {
            error = "Blast generated a bond outside the support fragments";
            return false;
        }
        auto bond = std::make_unique<schema::DestructionBondDataT>();
        bond->chunk_a = desc.chunkIndices[0];
        bond->chunk_b = desc.chunkIndices[1];
        bond->area = desc.bond.area;
        bond->health = settings.bond_health;
        bond->centroid =
            schema::Vec3(desc.bond.centroid[0], desc.bond.centroid[1], desc.bond.centroid[2]);
        bond->normal = schema::Vec3(desc.bond.normal[0], desc.bond.normal[1], desc.bond.normal[2]);
        candidate.bonds.push_back(std::move(bond));
    }
    if (!DestructionGraph::validate(candidate, error))
    {
        return false;
    }
    output = std::move(candidate);
    return true;
}
} // namespace engine::plugins::destruction
