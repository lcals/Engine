#include "editor/mesh/mesh_lods.hpp"
#include "runtime/resource/codecs/mesh_resource_codec.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "mesh LOD: %s\n", message);
    }
    return value;
}

engine::schema::MeshDataT grid()
{
    engine::schema::MeshDataT mesh;
    mesh.path = "models/lod-grid.mesh";
    constexpr std::uint32_t width = 24;
    for (std::uint32_t y = 0; y <= width; ++y)
    {
        for (std::uint32_t x = 0; x <= width; ++x)
        {
            mesh.vertices.emplace_back(x, y, 0, 0, 0, 1);
            mesh.texcoords.emplace_back(static_cast<double>(x) / width,
                                        static_cast<double>(y) / width);
            mesh.lightmap_texcoords.push_back(mesh.texcoords.back());
            mesh.tangents.emplace_back(1, 0, 0, 1);
            mesh.colors.emplace_back(static_cast<float>(x) / width, static_cast<float>(y) / width,
                                     0, 1);
        }
    }
    for (std::uint32_t y = 0; y < width; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const auto vertex = y * (width + 1) + x;
            mesh.indices.insert(mesh.indices.end(), {vertex, vertex + 1, vertex + width + 2, vertex,
                                                     vertex + width + 2, vertex + width + 1});
        }
    }
    return mesh;
}

bool tests()
{
    auto mesh = grid();
    const auto original = mesh;
    std::string error;
    auto options = toml::parse("[lod]\nenabled=true\ntriangle_ratios=[0.5,0.25,0.125]\n"
                               "screen_heights=[0.3,0.15,0.075]\nmax_relative_error=0.01\n");
    if (!expect(options && engine::editor::generate_mesh_lods(mesh, options.table(), error),
                "upstream attribute simplifier accepts a textured static grid") ||
        !expect(!mesh.lods.empty() && engine::MeshResourceCodec::validate(mesh),
                "generated levels pass runtime resource validation") ||
        !expect(mesh.indices == original.indices && mesh.texcoords == original.texcoords &&
                    mesh.tangents == original.tangents && mesh.colors == original.colors &&
                    mesh.lightmap_texcoords == original.lightmap_texcoords &&
                    mesh.vertices.size() == original.vertices.size() &&
                    std::memcmp(mesh.vertices.data(), original.vertices.data(),
                                mesh.vertices.size() * sizeof(engine::schema::MeshVertex)) == 0,
                "LOD0 and all shared PBR attributes remain byte-identical"))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return false;
    }
    auto count = mesh.indices.size();
    for (const auto& lod : mesh.lods)
    {
        if (!expect(lod->indices.size() < count && lod->submeshes.size() == 1 &&
                        lod->submeshes[0].material_slot() == 0,
                    "each stored level reduces triangle count and retains material slot"))
        {
            return false;
        }
        count = lod->indices.size();
        // LockBorder keeps all source perimeter vertices at their original positions.
        for (std::uint32_t vertex = 0; vertex < mesh.vertices.size(); ++vertex)
        {
            const auto& point = mesh.vertices[vertex];
            if ((point.x() == 0 || point.x() == 24 || point.y() == 0 || point.y() == 24) &&
                !expect(std::find(lod->indices.begin(), lod->indices.end(), vertex) !=
                            lod->indices.end(),
                        "topological borders survive reduction"))
            {
                return false;
            }
        }
    }
    const auto valid = mesh;
    for (const char* text : {"[lod]\nenabled=1", "[lod]\ntriangle_ratios=[0.5,0.5]",
                             "[lod]\nscreen_heights=[0.2,0.3]", "[lod]\nmax_relative_error=nan",
                             "[lod]\nmax_relative_error=1e-99", "[lod]\nunknown=true"})
    {
        const auto invalid = toml::parse(text);
        error.clear();
        if (!expect(invalid && !engine::editor::generate_mesh_lods(mesh, invalid.table(), error) &&
                        !error.empty() && mesh.lods[0]->indices == valid.lods[0]->indices,
                    "invalid import settings return error without changing existing levels"))
        {
            return false;
        }
    }
    auto corrupt = valid;
    corrupt.lods[0]->indices[0] = static_cast<std::uint32_t>(mesh.vertices.size());
    if (!expect(!engine::MeshResourceCodec::validate(corrupt),
                "out of range LOD index is rejected"))
    {
        return false;
    }
    corrupt = valid;
    corrupt.lods[0]->submeshes[0] = {0, static_cast<std::uint32_t>(corrupt.lods[0]->indices.size()),
                                     3};
    if (!expect(!engine::MeshResourceCodec::validate(corrupt), "changed material slot is rejected"))
    {
        return false;
    }
    corrupt = valid;
    corrupt.lods[0]->screen_height = 0;
    if (!expect(!engine::MeshResourceCodec::validate(corrupt),
                "invalid screen threshold is rejected"))
    {
        return false;
    }
    auto skinned = original;
    skinned.skin.resize(skinned.vertices.size());
    if (!expect(engine::editor::generate_mesh_lods(skinned, options.table(), error) &&
                    skinned.lods.empty(),
                "skinned topology remains LOD0"))
    {
        return false;
    }
    // Different source material groups must remain separate even if one cannot simplify.
    auto multiple = grid();
    const auto half = static_cast<std::uint32_t>(multiple.indices.size() / 2);
    multiple.submeshes = {{0, half, 2}, {half, half, 5}};
    if (!expect(engine::editor::generate_mesh_lods(multiple, options.table(), error) &&
                    !multiple.lods.empty() && engine::MeshResourceCodec::validate(multiple),
                "material partitions simplify independently"))
    {
        return false;
    }
    for (const auto& lod : multiple.lods)
    {
        if (!expect(lod->submeshes[0].material_slot() == 2 &&
                        lod->submeshes[1].material_slot() == 5,
                    "all levels retain the exact original material partition order"))
        {
            return false;
        }
    }
    auto seams = grid();
    std::vector<std::uint32_t> seam_vertices;
    // Split the middle column into two UV charts with opposite tangent handedness.
    for (std::uint32_t row = 0; row <= 24; ++row)
    {
        const auto original_vertex = row * 25 + 12;
        const auto duplicate = static_cast<std::uint32_t>(seams.vertices.size());
        seams.vertices.push_back(seams.vertices[original_vertex]);
        seams.texcoords.emplace_back(1.5, seams.texcoords[original_vertex].y());
        seams.lightmap_texcoords.push_back(seams.lightmap_texcoords[original_vertex]);
        seams.tangents.emplace_back(-1, 0, 0, -1);
        seams.colors.emplace_back(0, 0, 1, 1);
        seam_vertices.insert(seam_vertices.end(), {original_vertex, duplicate});
        for (std::uint32_t y = 0; y < 24; ++y)
        {
            for (std::uint32_t x = 12; x < 24; ++x)
            {
                const auto first = (y * 24 + x) * 6;
                for (std::uint32_t corner = first; corner < first + 6; ++corner)
                {
                    if (seams.indices[corner] == original_vertex)
                    {
                        seams.indices[corner] = duplicate;
                    }
                }
            }
        }
    }
    if (!expect(engine::editor::generate_mesh_lods(seams, options.table(), error) &&
                    !seams.lods.empty() && engine::MeshResourceCodec::validate(seams),
                "UV charts and opposite tangent hands retain useful static levels"))
    {
        return false;
    }
    for (const auto& lod : seams.lods)
    {
        for (const auto vertex : seam_vertices)
        {
            if (!expect(std::find(lod->indices.begin(), lod->indices.end(), vertex) !=
                            lod->indices.end(),
                        "both identities on each PBR seam remain referenced at every level"))
            {
                return false;
            }
        }
    }
    const auto disabled = toml::parse("[lod]\nenabled=false");
    return expect(engine::editor::generate_mesh_lods(mesh, disabled.table(), error) &&
                      mesh.lods.empty() && mesh.indices == original.indices,
                  "disabling generation removes only optional topology");
}

} // namespace

int main()
{
    return tests() ? 0 : 1;
}
