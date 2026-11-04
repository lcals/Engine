#include "editor/mesh/mesh_tangents.hpp"

#include <mikktspace.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

namespace engine::editor
{
// MikkTSpace emits a tangent frame for each face corner. A vertex shared by
// different frames must be split rather than overwriting one of those frames.
bool generate_mesh_tangents(schema::MeshDataT& mesh, std::string& error)
{
    if (mesh.texcoords.empty() || !mesh.tangents.empty())
    {
        return true;
    }
    // Upstream MakeIndex packs face/corner as (face << 2) | corner in a signed int.
    if (mesh.indices.size() / 3 >
        static_cast<std::size_t>(std::numeric_limits<int>::max() / 4))
    {
        error = "mesh has too many faces for MikkTSpace";
        return false;
    }
    struct Context
    {
        const schema::MeshDataT* mesh;
        std::vector<schema::Vec4> corners;
    } data{&mesh, std::vector<schema::Vec4>(mesh.indices.size())};
    SMikkTSpaceInterface api{};
    api.m_getNumFaces = [](const SMikkTSpaceContext* context) -> int
    {
        const auto& d = *static_cast<Context*>(context->m_pUserData);
        return static_cast<int>(d.mesh->indices.size() / 3);
    };
    api.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) -> int { return 3; };
    api.m_getPosition = [](const SMikkTSpaceContext* context, float out[], int face, int corner)
    {
        const auto& d = *static_cast<Context*>(context->m_pUserData);
        const auto& v = d.mesh->vertices[d.mesh->indices[static_cast<std::size_t>(face) * 3 + corner]];
        out[0] = v.x();
        out[1] = v.y();
        out[2] = v.z();
    };
    api.m_getNormal = [](const SMikkTSpaceContext* context, float out[], int face, int corner)
    {
        const auto& d = *static_cast<Context*>(context->m_pUserData);
        const auto& v = d.mesh->vertices[d.mesh->indices[static_cast<std::size_t>(face) * 3 + corner]];
        const double length = std::hypot(v.nx(), v.ny(), v.nz());
        out[0] = static_cast<float>(v.nx() / length);
        out[1] = static_cast<float>(v.ny() / length);
        out[2] = static_cast<float>(v.nz() / length);
    };
    api.m_getTexCoord = [](const SMikkTSpaceContext* context, float out[], int face, int corner)
    {
        const auto& d = *static_cast<Context*>(context->m_pUserData);
        const auto& uv = d.mesh->texcoords[d.mesh->indices[static_cast<std::size_t>(face) * 3 + corner]];
        out[0] = uv.x();
        out[1] = uv.y();
    };
    api.m_setTSpaceBasic = [](const SMikkTSpaceContext* context, const float tangent[], float sign,
                              int face, int corner)
    {
        auto& d = *static_cast<Context*>(context->m_pUserData);
        d.corners[static_cast<std::size_t>(face) * 3 + corner] =
            schema::Vec4(tangent[0], tangent[1], tangent[2], sign);
    };
    const SMikkTSpaceContext context{&api, &data};
    if (!genTangSpaceDefault(&context))
    {
        error = "MikkTSpace failed to generate mesh tangents";
        return false;
    }
    using FrameKey = std::array<std::uint64_t, 5>;
    std::map<FrameKey, std::uint32_t> frames;
    std::vector<bool> assigned(mesh.vertices.size(), false);
    mesh.tangents.assign(mesh.vertices.size(), schema::Vec4(1.0F, 0.0F, 0.0F, 1.0F));
    for (std::size_t i = 0; i < mesh.indices.size(); ++i)
    {
        const auto source = mesh.indices[i];
        const auto& tangent = data.corners[i];
        if (!std::isfinite(tangent.x()) || !std::isfinite(tangent.y()) ||
            !std::isfinite(tangent.z()) || std::hypot(tangent.x(), tangent.y(), tangent.z()) < 1e-12 ||
            (tangent.w() != 1.0F && tangent.w() != -1.0F))
        {
            error = "MikkTSpace returned an invalid tangent frame at triangle " +
                    std::to_string(i / 3) + ", corner " + std::to_string(i % 3) +
                    ": (" + std::to_string(tangent.x()) + ", " +
                    std::to_string(tangent.y()) + ", " + std::to_string(tangent.z()) +
                    ", " + std::to_string(tangent.w()) + ")";
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                const auto vertex = mesh.indices[(i / 3) * 3 + corner];
                const auto& v = mesh.vertices[vertex];
                const auto& uv = mesh.texcoords[vertex];
                error += " [p=" + std::to_string(v.x()) + "," + std::to_string(v.y()) + "," +
                         std::to_string(v.z()) + " n=" + std::to_string(v.nx()) + "," +
                         std::to_string(v.ny()) + "," + std::to_string(v.nz()) + " uv=" +
                         std::to_string(uv.x()) + "," + std::to_string(uv.y()) + "]";
            }
            return false;
        }
        const FrameKey key{source, std::bit_cast<std::uint64_t>(tangent.x()),
                           std::bit_cast<std::uint64_t>(tangent.y()),
                           std::bit_cast<std::uint64_t>(tangent.z()),
                           std::bit_cast<std::uint64_t>(tangent.w())};
        auto found = frames.find(key);
        if (found == frames.end())
        {
            std::uint32_t target = source;
            if (assigned[source])
            {
                if (mesh.vertices.size() >=
                    std::numeric_limits<std::uint32_t>::max() / (16 * sizeof(float)))
                {
                    error = "tangent splitting exceeds mesh vertex limits";
                    return false;
                }
                target = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(mesh.vertices[source]);
                mesh.texcoords.push_back(mesh.texcoords[source]);
                if (!mesh.lightmap_texcoords.empty())
                {
                    mesh.lightmap_texcoords.push_back(mesh.lightmap_texcoords[source]);
                }
                if (!mesh.colors.empty())
                {
                    mesh.colors.push_back(mesh.colors[source]);
                }
                if (!mesh.skin.empty())
                {
                    mesh.skin.push_back(mesh.skin[source]);
                }
                mesh.tangents.push_back(tangent);
            }
            else
            {
                assigned[source] = true;
                mesh.tangents[source] = tangent;
            }
            found = frames.emplace(key, target).first;
        }
        mesh.indices[i] = found->second;
    }
    return true;
}

} // namespace engine::editor
