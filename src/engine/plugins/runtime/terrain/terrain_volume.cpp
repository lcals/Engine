#include "plugins/runtime/terrain/terrain_volume.hpp"

// Eric Lengyel's official regular-cell tables, pinned by the transvoxel submodule.
// The upstream file contains its table types as well as the data; keep it unmodified.
#include <Transvoxel.cpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace engine::plugins::terrain
{
namespace
{
using Data = schema::TerrainAssetDataT;
constexpr std::size_t max_samples = 4U * 1024U * 1024U;

bool finite(const schema::Vec3& v)
{
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
}

std::size_t offset(const Data& d, unsigned x, unsigned y, unsigned z)
{
    return (static_cast<std::size_t>(z) * (d.cells_y + 1) + y) * (d.cells_x + 1) + x;
}

float sample(const Data& d, int x, int y, int z)
{
    return d.density[offset(d, std::clamp(x, 0, static_cast<int>(d.cells_x)),
                            std::clamp(y, 0, static_cast<int>(d.cells_y)),
                            std::clamp(z, 0, static_cast<int>(d.cells_z)))];
}

schema::Vec3 gradient(const Data& d, int x, int y, int z)
{
    return {sample(d, x + 1, y, z) - sample(d, x - 1, y, z),
            sample(d, x, y + 1, z) - sample(d, x, y - 1, z),
            sample(d, x, y, z + 1) - sample(d, x, y, z - 1)};
}

bool dimensions(const Data& d)
{
    if ((d.chunk_cells != 16 && d.chunk_cells != 32) || d.cells_x == 0 || d.cells_y == 0 ||
        d.cells_z == 0 || d.cells_x > 256 || d.cells_y > 256 || d.cells_z > 256 ||
        d.cells_x % d.chunk_cells || d.cells_y % d.chunk_cells || d.cells_z % d.chunk_cells ||
        !std::isfinite(d.voxel_size) || d.voxel_size < 0.05F || d.voxel_size > 100.0F ||
        !finite(d.origin))
    {
        return false;
    }
    return static_cast<std::size_t>(d.cells_x + 1) * (d.cells_y + 1) * (d.cells_z + 1) <=
           max_samples;
}

bool boundary(const Data& d, unsigned x, unsigned y, unsigned z)
{
    return x == 0 || y == 0 || z == 0 || x == d.cells_x || y == d.cells_y || z == d.cells_z;
}

float nonzero(float v)
{
    return std::abs(v) < 0.00001F ? 0.00001F : v;
}

void mesh_chunk(const Data& d, unsigned cx, unsigned cy, unsigned cz, schema::MeshDataT& mesh)
{
    mesh = {};
    mesh.path = "builtin/terrain/transient.mesh";
    for (unsigned z = cz; z < cz + d.chunk_cells; ++z)
    {
        for (unsigned y = cy; y < cy + d.chunk_cells; ++y)
        {
            for (unsigned x = cx; x < cx + d.chunk_cells; ++x)
            {
                std::array<float, 8> values{};
                std::array<schema::Vec3, 8> normals{};
                unsigned mask = 0;
                for (unsigned c = 0; c < 8; ++c)
                {
                    const unsigned px = x + (c & 1U);
                    const unsigned py = y + ((c >> 1U) & 1U);
                    const unsigned pz = z + ((c >> 2U) & 1U);
                    values[c] = d.density[offset(d, px, py, pz)];
                    normals[c] = gradient(d, px, py, pz);
                    if (values[c] < 0)
                    {
                        mask |= 1U << c;
                    }
                }
                const auto& cell = regularCellData[regularCellClass[mask]];
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                for (long v = 0; v < cell.GetVertexCount(); ++v)
                {
                    const unsigned edge = regularVertexData[mask][v];
                    const unsigned a = (edge >> 4U) & 15U;
                    const unsigned b = edge & 15U;
                    const double t = values[a] / static_cast<double>(values[a] - values[b]);
                    const auto interpolate = [t](double first, double second)
                    { return first + t * (second - first); };
                    double nx = interpolate(normals[a].x(), normals[b].x());
                    double ny = interpolate(normals[a].y(), normals[b].y());
                    double nz = interpolate(normals[a].z(), normals[b].z());
                    const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
                    if (length > 1.0e-12)
                    {
                        nx /= length;
                        ny /= length;
                        nz /= length;
                    }
                    else
                    {
                        nx = 0;
                        ny = 1;
                        nz = 0;
                    }
                    mesh.vertices.emplace_back(
                        static_cast<float>(d.origin.x() +
                                           d.voxel_size * (x + interpolate(a & 1U, b & 1U))),
                        static_cast<float>(d.origin.y() +
                                           d.voxel_size *
                                               (y + interpolate((a >> 1U) & 1U, (b >> 1U) & 1U))),
                        static_cast<float>(d.origin.z() +
                                           d.voxel_size *
                                               (z + interpolate((a >> 2U) & 1U, (b >> 2U) & 1U))),
                        static_cast<float>(nx), static_cast<float>(ny), static_cast<float>(nz));
                }
                for (long i = 0; i < cell.GetTriangleCount() * 3; i += 3)
                {
                    auto ia = base + cell.vertexIndex[i];
                    auto ib = base + cell.vertexIndex[i + 1];
                    auto ic = base + cell.vertexIndex[i + 2];
                    const auto& a = mesh.vertices[ia];
                    const auto& b = mesh.vertices[ib];
                    const auto& c = mesh.vertices[ic];
                    const double abx = b.x() - a.x(), aby = b.y() - a.y(), abz = b.z() - a.z();
                    const double acx = c.x() - a.x(), acy = c.y() - a.y(), acz = c.z() - a.z();
                    const double nx = aby * acz - abz * acy;
                    const double ny = abz * acx - abx * acz;
                    const double nz = abx * acy - aby * acx;
                    if (nx * nx + ny * ny + nz * nz < 1.0e-18)
                    {
                        continue;
                    }
                    if (nx * (a.nx() + b.nx() + c.nx()) + ny * (a.ny() + b.ny() + c.ny()) +
                            nz * (a.nz() + b.nz() + c.nz()) <
                        0)
                    {
                        std::swap(ib, ic);
                    }
                    mesh.indices.insert(mesh.indices.end(), {ia, ib, ic});
                }
            }
        }
    }
}
} // namespace

bool TerrainVolume::validate(const Data& data, std::string& error)
{
    error.clear();
    if (data.schema_version != 1 || !dimensions(data) ||
        data.density.size() !=
            static_cast<std::size_t>(data.cells_x + 1) * (data.cells_y + 1) * (data.cells_z + 1))
    {
        error = "Terrain requires bounded dimensions, 16/32-cell chunks and complete samples";
        return false;
    }
    for (float v : data.density)
    {
        if (!std::isfinite(v) || std::abs(v) > 1000000.0F)
        {
            error = "Terrain density is not finite or exceeds supported range";
            return false;
        }
    }
    return true;
}

bool TerrainVolume::generate(const schema::TerrainGeneratorDataT& settings, Data& output,
                             std::string& error)
{
    output = {};
    error.clear();
    Data data;
    data.cells_x = settings.cells_x;
    data.cells_y = settings.cells_y;
    data.cells_z = settings.cells_z;
    data.voxel_size = settings.voxel_size;
    data.chunk_cells = settings.chunk_cells;
    data.origin = settings.origin;
    if (!dimensions(data) || !std::isfinite(settings.height) ||
        !std::isfinite(settings.amplitude) || settings.amplitude < 0 ||
        settings.amplitude > 10000 || !std::isfinite(settings.frequency) ||
        settings.frequency <= 0 || settings.frequency > 10 || settings.cavities.size() > 1024)
    {
        error = "Invalid terrain generation settings";
        return false;
    }
    for (const auto& sphere : settings.cavities)
    {
        if (!sphere || !finite(sphere->center) || !std::isfinite(sphere->radius) ||
            sphere->radius <= 0 || sphere->radius > 10000)
        {
            error = "Invalid terrain cavity";
            return false;
        }
    }
    data.density.resize(static_cast<std::size_t>(data.cells_x + 1) * (data.cells_y + 1) *
                        (data.cells_z + 1));
    const double phase = (settings.seed % 4096) * 0.013;
    for (unsigned z = 0; z <= data.cells_z; ++z)
    {
        for (unsigned y = 0; y <= data.cells_y; ++y)
        {
            for (unsigned x = 0; x <= data.cells_x; ++x)
            {
                const double px = data.origin.x() + x * data.voxel_size;
                const double py = data.origin.y() + y * data.voxel_size;
                const double pz = data.origin.z() + z * data.voxel_size;
                const double height =
                    settings.height +
                    settings.amplitude *
                        (0.65 * std::sin(px * settings.frequency + phase) *
                             std::cos(pz * settings.frequency) +
                         0.35 * std::sin((px + pz) * settings.frequency * 0.47 + phase));
                double value = py - height;
                for (const auto& sphere : settings.cavities)
                {
                    const double dx = px - sphere->center.x(), dy = py - sphere->center.y(),
                                 dz = pz - sphere->center.z();
                    value =
                        std::max(value, sphere->radius - std::sqrt(dx * dx + dy * dy + dz * dz));
                }
                // Finite volume boundary is air, producing a closed surface instead of open outer
                // walls.
                data.density[offset(data, x, y, z)] =
                    boundary(data, x, y, z) ? data.voxel_size : nonzero(static_cast<float>(value));
            }
        }
    }
    if (!validate(data, error))
    {
        return false;
    }
    output = std::move(data);
    return true;
}

bool TerrainVolume::initialize(const Data& data, std::string& error)
{
    if (!validate(data, error))
    {
        return false;
    }
    data_ = data;
    chunks_.clear();
    chunks_.resize(static_cast<std::size_t>(data.cells_x / data.chunk_cells) *
                   (data.cells_y / data.chunk_cells) * (data.cells_z / data.chunk_cells));
    rebuilt_chunks_ = 0;
    return true;
}

bool TerrainVolume::apply_sphere(const schema::Vec3& center, float radius, bool fill,
                                 std::string& error)
{
    error.clear();
    if (chunks_.empty() || !finite(center) || !std::isfinite(radius) || radius <= 0 ||
        radius > 10000)
    {
        error = "Invalid terrain sphere";
        return false;
    }
    const auto& d = data_;
    const unsigned nx = d.cells_x / d.chunk_cells, ny = d.cells_y / d.chunk_cells;
    const double reach = radius + 2.0 * d.voxel_size;
    const auto low = [&](double position, double origin, unsigned cells)
    {
        return static_cast<unsigned>(
            std::clamp(std::floor((position - reach - origin) / d.voxel_size), 1.0,
                       static_cast<double>(cells)));
    };
    const auto high = [&](double position, double origin, unsigned cells)
    {
        return static_cast<unsigned>(
            std::clamp(std::ceil((position + reach - origin) / d.voxel_size), 0.0,
                       static_cast<double>(cells - 1)));
    };
    const unsigned lx = low(center.x(), d.origin.x(), d.cells_x),
                   hx = high(center.x(), d.origin.x(), d.cells_x);
    const unsigned ly = low(center.y(), d.origin.y(), d.cells_y),
                   hy = high(center.y(), d.origin.y(), d.cells_y);
    const unsigned lz = low(center.z(), d.origin.z(), d.cells_z),
                   hz = high(center.z(), d.origin.z(), d.cells_z);
    for (unsigned z = lz; z <= hz; ++z)
    {
        for (unsigned y = ly; y <= hy; ++y)
        {
            for (unsigned x = lx; x <= hx; ++x)
            {
                const double dx = d.origin.x() + x * d.voxel_size - center.x();
                const double dy = d.origin.y() + y * d.voxel_size - center.y();
                const double dz = d.origin.z() + z * d.voxel_size - center.z();
                const float sphere =
                    static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz) - radius);
                const auto index = offset(d, x, y, z);
                const float previous = data_.density[index];
                const float changed =
                    nonzero(fill ? std::min(previous, sphere) : std::max(previous, -sphere));
                if (changed == previous)
                {
                    continue;
                }
                data_.density[index] = changed;
                // One sample affects adjacent cells and their central-difference normals.
                const unsigned min_x = (x > 1 ? x - 2 : 0) / d.chunk_cells;
                const unsigned min_y = (y > 1 ? y - 2 : 0) / d.chunk_cells;
                const unsigned min_z = (z > 1 ? z - 2 : 0) / d.chunk_cells;
                const unsigned max_x = std::min(x + 1, d.cells_x - 1) / d.chunk_cells;
                const unsigned max_y = std::min(y + 1, d.cells_y - 1) / d.chunk_cells;
                const unsigned max_z = std::min(z + 1, d.cells_z - 1) / d.chunk_cells;
                for (unsigned cz = min_z; cz <= max_z; ++cz)
                {
                    for (unsigned cy = min_y; cy <= max_y; ++cy)
                    {
                        for (unsigned cx = min_x; cx <= max_x; ++cx)
                        {
                            chunks_[(cz * ny + cy) * nx + cx].dirty = true;
                        }
                    }
                }
            }
        }
    }
    return true;
}

bool TerrainVolume::build_mesh(schema::MeshDataT& output, std::string& error)
{
    output = {};
    error.clear();
    if (chunks_.empty())
    {
        error = "Terrain volume is not initialized";
        return false;
    }
    schema::MeshDataT mesh;
    mesh.path = "builtin/terrain/transient.mesh";
    std::size_t index = 0;
    rebuilt_chunks_ = 0;
    for (unsigned z = 0; z < data_.cells_z; z += data_.chunk_cells)
    {
        for (unsigned y = 0; y < data_.cells_y; y += data_.chunk_cells)
        {
            for (unsigned x = 0; x < data_.cells_x; x += data_.chunk_cells)
            {
                auto& chunk = chunks_[index++];
                if (chunk.dirty)
                {
                    mesh_chunk(data_, x, y, z, chunk.mesh);
                    chunk.dirty = false;
                    ++rebuilt_chunks_;
                }
                if (mesh.vertices.size() + chunk.mesh.vertices.size() > 4000000U)
                {
                    error = "Terrain mesh exceeds the synchronous mesh budget";
                    return false;
                }
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.insert(mesh.vertices.end(), chunk.mesh.vertices.begin(),
                                     chunk.mesh.vertices.end());
                for (auto i : chunk.mesh.indices)
                {
                    mesh.indices.push_back(base + i);
                }
            }
        }
    }
    if (mesh.indices.empty())
    {
        mesh.vertices.clear();
    }
    output = std::move(mesh);
    return true;
}

const Data& TerrainVolume::data() const noexcept
{
    return data_;
}
std::size_t TerrainVolume::chunk_count() const noexcept
{
    return chunks_.size();
}
std::size_t TerrainVolume::rebuilt_chunks() const noexcept
{
    return rebuilt_chunks_;
}

} // namespace engine::plugins::terrain
