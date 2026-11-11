#include "plugins/editor/modeling/editable_mesh.hpp"
#include "plugins/editor/modeling/mesh_primitives.hpp"
#include "plugins/editor/modeling/mesh_topology.hpp"
#include "runtime/utils/mesh_utils.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace
{

using namespace engine;

bool expect(bool condition, const char* message, const std::string& error = {})
{
    if (!condition)
    {
        std::fprintf(stderr, "modeling core: %s: %s\n", message, error.c_str());
    }
    return condition;
}

double volume(const schema::MeshDataT& mesh)
{
    double result = 0.0;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
    {
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1]];
        const auto& c = mesh.vertices[mesh.indices[i + 2]];
        result += (static_cast<double>(a.x()) *
                       (static_cast<double>(b.y()) * c.z() - static_cast<double>(b.z()) * c.y()) +
                   static_cast<double>(a.y()) *
                       (static_cast<double>(b.z()) * c.x() - static_cast<double>(b.x()) * c.z()) +
                   static_cast<double>(a.z()) *
                       (static_cast<double>(b.x()) * c.y() - static_cast<double>(b.y()) * c.x())) /
                  6.0;
    }
    return result;
}

bool closed(const schema::MeshDataT& mesh)
{
    using Position = std::tuple<float, float, float>;
    std::map<Position, std::uint64_t> positions;
    std::vector<std::uint64_t> ids;
    for (const auto& vertex : mesh.vertices)
    {
        const auto key = std::make_tuple(vertex.x(), vertex.y(), vertex.z());
        const auto found = positions.emplace(key, positions.size());
        ids.push_back(found.first->second);
    }
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::pair<int, int>> edges;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
    {
        for (std::size_t j = 0; j < 3; ++j)
        {
            const auto a = ids[mesh.indices[i + j]];
            const auto b = ids[mesh.indices[i + (j + 1) % 3]];
            auto& edge = edges[std::minmax(a, b)];
            ++edge.first;
            edge.second += a < b ? 1 : -1;
        }
    }
    for (const auto& [edge, value] : edges)
    {
        (void)edge;
        if (value.first != 2 || value.second != 0)
        {
            return false;
        }
    }
    return true;
}

bool mirrored_uv_tangents()
{
    engine::schema::ModelingPrimitiveDataT options;
    options.type = engine::schema::ModelingPrimitive::plane;
    schema::EditableMeshDataT model;
    schema::MeshDataT mesh;
    std::string error;
    if (!engine::plugins::modeling::mesh::create(options, model, error) ||
        !expect(model.faces.size() == 1 && model.faces.front()->vertices.size() == 4,
                "create quad for mirrored UV tangent test"))
    {
        return false;
    }
    model.faces.front()->texcoords = {{0, 0}, {1, 1}, {0, 1}, {1, 0}};
    if (!expect(engine::plugins::modeling::mesh::compile(model, "mirrored.mesh", mesh, error),
                "compile modeling mesh with mirrored UVs", error) ||
        !expect(mesh.vertices.size() > 4 && mesh.indices.size() == 6 &&
                    mesh.tangents.size() == mesh.vertices.size() &&
                    mesh.colors.size() == mesh.vertices.size(),
                "modeling compilation splits the shared mirrored UV frame"))
    {
        return false;
    }
    bool positive = false;
    bool negative = false;
    for (auto index : mesh.indices)
    {
        positive |= mesh.tangents[index].w() == 1;
        negative |= mesh.tangents[index].w() == -1;
    }
    return expect(positive && negative, "modeling mesh retains both UV handedness values");
}

bool primitives()
{
    std::string error;
    for (const auto type :
         {engine::schema::ModelingPrimitive::cube, engine::schema::ModelingPrimitive::plane,
          engine::schema::ModelingPrimitive::cylinder, engine::schema::ModelingPrimitive::cone,
          engine::schema::ModelingPrimitive::sphere, engine::schema::ModelingPrimitive::torus,
          engine::schema::ModelingPrimitive::prism, engine::schema::ModelingPrimitive::stairs,
          engine::schema::ModelingPrimitive::arch, engine::schema::ModelingPrimitive::pipe,
          engine::schema::ModelingPrimitive::door, engine::schema::ModelingPrimitive::curved_stairs,
          engine::schema::ModelingPrimitive::sprite, engine::schema::ModelingPrimitive::icosphere})
    {
        engine::schema::ModelingPrimitiveDataT options;
        options.type = type;
        schema::EditableMeshDataT model;
        schema::MeshDataT mesh;
        if (!expect(engine::plugins::modeling::mesh::create(options, model, error),
                    "primitive creation", error) ||
            !expect(engine::plugins::modeling::mesh::compile(model, "model.mesh", mesh, error),
                    "primitive compiles", error) ||
            !expect(MeshUtils::validate(mesh), "primitive render mesh validates") ||
            !expect(type == engine::schema::ModelingPrimitive::plane ||
                        type == engine::schema::ModelingPrimitive::sprite ||
                        (closed(mesh) && volume(mesh) > 0.0),
                    "solid primitive is closed, consistent and outward"))
        {
            std::fprintf(stderr, "primitive type: %d\n", static_cast<int>(type));
            return false;
        }
    }
    return true;
}

bool icosphere_tangent_frames()
{
    for (const auto subdivisions : {0U, 1U, 3U})
    {
        schema::ModelingPrimitiveDataT options;
        options.type = schema::ModelingPrimitive::icosphere;
        options.subdivisions = subdivisions;
        options.radius = 1.25;
        options.uv_tiles_per_unit = 2;
        schema::EditableMeshDataT model;
        schema::MeshDataT mesh;
        std::string error;
        if (!expect(plugins::modeling::mesh::create(options, model, error) &&
                        plugins::modeling::mesh::compile(model, "icosphere.mesh", mesh, error) &&
                        MeshUtils::validate(mesh),
                    "subdivided icosphere generates valid UV tangent frames", error))
        {
            return false;
        }
        std::size_t poles = 0;
        for (std::size_t index = 0; index < mesh.vertices.size(); ++index)
        {
            const auto& vertex = mesh.vertices[index];
            const auto& tangent = mesh.tangents[index];
            poles += vertex.x() == 0 && vertex.z() == 0;
            const auto length = std::hypot(tangent.x(), tangent.y(), tangent.z());
            const auto projection = vertex.nx() * tangent.x() + vertex.ny() * tangent.y() +
                                    vertex.nz() * tangent.z();
            if (!expect(std::abs(length - 1.0) < 1e-5 && std::abs(projection) < 1e-5,
                        "icosphere tangents remain unit length and orthogonal at seams and poles"))
            {
                return false;
            }
        }
        if (!expect(subdivisions == 0 || poles > 0, "subdivided sphere exercises both poles"))
        {
            return false;
        }
    }
    return true;
}

bool primitive_contract()
{
    schema::ModelingPrimitiveDataT options;
    options.size = {2, 3, 4};
    options.uv_tiles_per_unit = 2;
    schema::EditableMeshDataT model;
    std::string error;
    if (!expect(plugins::modeling::mesh::create(options, model, error), "metric cube", error))
    {
        return false;
    }
    for (const auto& face : model.faces)
    {
        for (std::size_t i = 0; i < face->vertices.size(); ++i)
        {
            const auto next = (i + 1) % face->vertices.size();
            const auto* a = plugins::modeling::mesh::find_vertex(model, face->vertices[i]);
            const auto* b = plugins::modeling::mesh::find_vertex(model, face->vertices[next]);
            const double length = std::hypot(a->x() - b->x(), a->y() - b->y(), a->z() - b->z());
            const auto& u = face->texcoords[i];
            const auto& v = face->texcoords[next];
            if (!expect(std::abs(std::hypot(u.x() - v.x(), u.y() - v.y()) - 2 * length) < 1e-9,
                        "UV density follows physical edge length on every cube face"))
            {
                return false;
            }
        }
    }
    std::vector<std::uint8_t> bytes;
    schema::EditableMeshDataT decoded;
    if (!expect(plugins::modeling::mesh::serialize(model, bytes, error) &&
                    plugins::modeling::mesh::deserialize(bytes, decoded, error) &&
                    decoded.primitive && *decoded.primitive == options,
                "generation recipe round trips", error))
    {
        return false;
    }
    for (const auto version : {0U, 1U})
    {
        model.schema_version = version;
        flatbuffers::FlatBufferBuilder builder;
        schema::FinishEditableMeshDataBuffer(builder,
                                             schema::EditableMeshData::Pack(builder, &model));
        if (!expect(!plugins::modeling::mesh::deserialize(
                        {builder.GetBufferPointer(), builder.GetSize()}, decoded, error) &&
                        decoded.vertices.empty(),
                    "reject missing and obsolete source versions"))
        {
            return false;
        }
    }
    options.type = schema::ModelingPrimitive::sphere;
    return expect(!plugins::modeling::mesh::create(options, decoded, error) &&
                      decoded.vertices.empty(),
                  "unused native parameters are rejected atomically");
}

bool polygon_hole()
{
    std::string error;
    schema::EditableMeshDataT model;
    const std::vector<schema::Vec3> outer{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}};
    const std::vector<std::vector<schema::Vec3>> holes{
        {{1, 1, 0}, {1, 3, 0}, {3, 3, 0}, {3, 1, 0}}};
    schema::MeshDataT mesh;
    if (!expect(engine::plugins::modeling::mesh::polygon(outer, holes, model, error),
                "polygon hole", error) ||
        !expect(engine::plugins::modeling::mesh::compile(model, "hole.mesh", mesh, error),
                "hole compile", error))
    {
        return false;
    }
    double area = 0;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
    {
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1]];
        const auto& c = mesh.vertices[mesh.indices[i + 2]];
        area += (static_cast<double>(b.x()) - a.x()) * (static_cast<double>(c.y()) - a.y()) * 0.5 -
                (static_cast<double>(b.y()) - a.y()) * (static_cast<double>(c.x()) - a.x()) * 0.5;
    }
    if (!expect(std::abs(area - 12.0) < 1.0e-6, "hole area and winding preserved"))
    {
        return false;
    }
    std::vector<std::uint8_t> bytes;
    schema::EditableMeshDataT decoded;
    if (!expect(engine::plugins::modeling::mesh::serialize(model, bytes, error), "serialize",
                error) ||
        !expect(engine::plugins::modeling::mesh::deserialize(bytes, decoded, error), "deserialize",
                error) ||
        !expect(decoded.faces.front()->holes.size() == 1 && decoded.next_id == model.next_id,
                "schema round trip preserves hole and identity"))
    {
        return false;
    }
    bytes[0] = 0xFF;
    if (!expect(!engine::plugins::modeling::mesh::deserialize(bytes, decoded, error) &&
                    decoded.faces.empty(),
                "bad binary returns empty output"))
    {
        return false;
    }
    const std::vector<schema::Vec3> bowtie{{0, 0, 0}, {2, 2, 0}, {0, 2, 0}, {2, 0, 0}};
    return expect(!engine::plugins::modeling::mesh::polygon(bowtie, {}, decoded, error),
                  "self-intersection rejected");
}

bool topology()
{
    std::string error;
    schema::EditableMeshDataT model;
    if (!engine::plugins::modeling::mesh::create({}, model, error))
    {
        return expect(false, "cube", error);
    }
    const auto face = model.faces.front()->id;
    const std::array<std::uint64_t, 1> selected{face};
    std::vector<std::uint8_t> before;
    std::vector<std::uint8_t> after;
    if (!engine::plugins::modeling::mesh::serialize(model, before, error))
    {
        return false;
    }
    if (!expect(!engine::plugins::modeling::mesh::inset(model, selected, 2.0, error),
                "invalid inset fails") ||
        !engine::plugins::modeling::mesh::serialize(model, after, error) ||
        !expect(before == after, "failed operation is atomic") ||
        !expect(engine::plugins::modeling::mesh::extrude(model, selected, 0.5, false, error),
                "extrude", error) ||
        !expect(model.faces.size() == 10 && engine::plugins::modeling::mesh::find_face(model, face),
                "extrude retains cap identity and adds side walls") ||
        !expect(engine::plugins::modeling::mesh::inset(model, selected, 0.1, error), "inset",
                error) ||
        !expect(engine::plugins::modeling::mesh::subdivide(model, selected, 1, error), "subdivide",
                error) ||
        !expect(engine::plugins::modeling::mesh::triangulate(model, selected, error), "triangulate",
                error))
    {
        return false;
    }
    schema::MeshDataT compiled;
    return expect(engine::plugins::modeling::mesh::compile(model, "edited.mesh", compiled, error),
                  "edited model compiles", error);
}

bool attributes_and_edges()
{
    std::string error;
    schema::EditableMeshDataT model;
    engine::schema::ModelingPrimitiveDataT options;
    options.type = engine::schema::ModelingPrimitive::plane;
    if (!engine::plugins::modeling::mesh::create(options, model, error))
    {
        return false;
    }
    const auto original_face = model.faces.front()->id;
    auto& face = *model.faces.front();
    face.material_slot = 3;
    const auto a = face.vertices[0];
    const auto b = face.vertices[1];
    const auto c = face.vertices[2];
    if (!expect(engine::plugins::modeling::mesh::split_edge(model, {a, b}, 0.5, error),
                "split edge", error) ||
        !expect(model.faces.front()->vertices.size() == 5 &&
                    model.faces.front()->texcoords.size() == 5,
                "split edge interpolates corner attributes") ||
        !expect(
            engine::plugins::modeling::mesh::connect_vertices(model, original_face, a, c, error),
            "connect vertices", error) ||
        !expect(model.faces.size() == 2, "connect splits polygon") ||
        !expect(engine::plugins::modeling::mesh::dissolve_edge(model, {a, c}, error),
                "dissolve edge", error) ||
        !expect(model.faces.size() == 1 && model.faces.front()->material_slot == 3,
                "dissolve keeps material"))
    {
        return false;
    }
    schema::MeshDataT compiled;
    schema::EditableMeshDataT imported;
    if (!engine::plugins::modeling::mesh::compile(model, "model.mesh", compiled, error) ||
        !engine::plugins::modeling::mesh::from_mesh(compiled, imported, error))
    {
        return expect(false, "compile and import", error);
    }
    return expect(imported.faces.front()->material_slot == 3 &&
                      imported.faces.front()->texcoords.size() == 3,
                  "mesh import preserves material and per-corner UV");
}

bool loop_cut_and_normals()
{
    std::string error;
    schema::EditableMeshDataT model;
    schema::MeshDataT compiled;
    if (!engine::plugins::modeling::mesh::create({}, model, error))
    {
        return false;
    }
    const engine::plugins::modeling::MeshEdge edge{model.faces.front()->vertices[0],
                                                   model.faces.front()->vertices[1]};
    std::vector<engine::plugins::modeling::MeshEdge> ring;
    if (!expect(engine::plugins::modeling::mesh::edge_ring(model, edge, ring, error) &&
                    ring.size() == 4,
                "cube has four-edge ring", error) ||
        !expect(engine::plugins::modeling::mesh::loop_cut(model, edge, 2, 0.2, 0.1, error),
                "loop cut", error) ||
        !expect(model.faces.size() == 14, "loop cut splits four faces into twelve") ||
        !expect(engine::plugins::modeling::mesh::compile(model, "cut.mesh", compiled, error),
                "loop cut compile", error) ||
        !expect(closed(compiled) && std::abs(volume(compiled) - 1.0) < 1.0e-6,
                "loop cut retains closed volume"))
    {
        return false;
    }
    schema::EditableMeshDataT imported;
    schema::MeshDataT restored;
    if (!engine::plugins::modeling::mesh::from_mesh(compiled, imported, error) ||
        !engine::plugins::modeling::mesh::compile(imported, "imported.mesh", restored, error))
    {
        return expect(false, "import smooth groups", error);
    }
    for (const auto& vertex : restored.vertices)
    {
        const auto axis_sum = std::abs(vertex.nx()) + std::abs(vertex.ny()) + std::abs(vertex.nz());
        if (!expect(std::abs(axis_sum - 1.0F) < 1.0e-5F, "hard cube edges survive import"))
        {
            return false;
        }
    }
    std::vector<std::uint64_t> selection;
    const std::array<std::uint64_t, 1> face{model.faces.front()->id};
    return expect(engine::plugins::modeling::mesh::select_faces(
                      model, face, engine::plugins::modeling::MeshFaceSelection::connected,
                      selection, error) &&
                      selection.size() == 14,
                  "connected selection reaches all cube faces", error);
}

bool extruded_hole()
{
    std::string error;
    schema::EditableMeshDataT model;
    const std::vector<schema::Vec3> outer{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}};
    const std::vector<std::vector<schema::Vec3>> holes{
        {{1, 1, 0}, {3, 1, 0}, {3, 3, 0}, {1, 3, 0}}};
    if (!engine::plugins::modeling::mesh::polygon(outer, holes, model, error))
    {
        return false;
    }
    const std::array<std::uint64_t, 1> cap{model.faces.front()->id};
    if (!engine::plugins::modeling::mesh::duplicate_faces(model, cap, error))
    {
        return false;
    }
    const std::array<std::uint64_t, 1> bottom{model.faces.back()->id};
    if (!engine::plugins::modeling::mesh::reverse_faces(model, bottom, error) ||
        !engine::plugins::modeling::mesh::extrude(model, cap, 1.0, false, error))
    {
        return expect(false, "extrude hole", error);
    }
    schema::MeshDataT compiled;
    return expect(
               engine::plugins::modeling::mesh::compile(model, "holed-solid.mesh", compiled, error),
               "extruded hole compiles", error) &&
           expect(closed(compiled) && std::abs(volume(compiled) - 12.0) < 1.0e-6,
                  "extruded hole closes with inward hole walls and outward outer walls");
}

bool boundary_and_attributes()
{
    std::string error;
    schema::EditableMeshDataT model;
    if (!engine::plugins::modeling::mesh::create({}, model, error))
    {
        return false;
    }
    const auto ring = model.faces.front()->vertices;
    const std::array<std::uint64_t, 1> selected{model.faces.front()->id};
    schema::MeshDataT compiled;
    if (!expect(engine::plugins::modeling::mesh::erase_faces(model, selected, error),
                "delete cube cap", error) ||
        !expect(engine::plugins::modeling::mesh::fill_hole(model, ring, error), "fill cube cap",
                error) ||
        !expect(engine::plugins::modeling::mesh::compile(model, "filled.mesh", compiled, error),
                "filled compile", error) ||
        !expect(closed(compiled) && std::abs(volume(compiled) - 1.0) < 1.0e-6,
                "filled cap follows boundary winding"))
    {
        return false;
    }
    const std::array<std::uint64_t, 1> cap{model.faces.back()->id};
    if (!expect(engine::plugins::modeling::mesh::set_material(model, cap, 2, error),
                "set face material", error) ||
        !expect(
            engine::plugins::modeling::mesh::set_color(model, cap, {0.2F, 0.4F, 0.6F, 1.0F}, error),
            "set face color", error) ||
        !expect(engine::plugins::modeling::mesh::compile(model, "colored.mesh", compiled, error),
                "colored compile", error) ||
        !expect(compiled.submeshes.size() == 2 && compiled.submeshes.back().material_slot() == 2,
                "face material becomes a render submesh"))
    {
        return false;
    }
    std::vector<std::uint64_t> selection;
    if (!expect(
            engine::plugins::modeling::mesh::select_faces(
                model, cap, engine::plugins::modeling::MeshFaceSelection::grow, selection, error) &&
                selection.size() == 5,
            "grow selection includes edge neighbors", error) ||
        !expect(engine::plugins::modeling::mesh::select_faces(
                    model, cap, engine::plugins::modeling::MeshFaceSelection::shrink, selection,
                    error) &&
                    selection.empty(),
                "shrink single face empties selection", error))
    {
        return false;
    }
    return true;
}

bool bridge_caps()
{
    const std::vector<schema::Vec3> bottom{{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}};
    const std::vector<schema::Vec3> top{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    schema::EditableMeshDataT model;
    schema::EditableMeshDataT other;
    std::string error;
    if (!engine::plugins::modeling::mesh::polygon(bottom, {}, model, error) ||
        !engine::plugins::modeling::mesh::polygon(top, {}, other, error) ||
        !engine::plugins::modeling::mesh::append(model, other, error))
    {
        return false;
    }
    const auto first = model.faces.front()->vertices;
    const auto& upper = model.faces.back()->vertices;
    const std::array<std::uint64_t, 4> second{upper[0], upper[3], upper[2], upper[1]};
    schema::MeshDataT compiled;
    return expect(engine::plugins::modeling::mesh::bridge(model, first, second, error),
                  "bridge caps", error) &&
           expect(engine::plugins::modeling::mesh::compile(model, "bridge.mesh", compiled, error),
                  "bridge compile", error) &&
           expect(closed(compiled) && std::abs(volume(compiled) - 1.0) < 1.0e-6,
                  "bridge has consistent outward winding");
}

bool extruded_edges()
{
    engine::schema::ModelingPrimitiveDataT options;
    options.type = engine::schema::ModelingPrimitive::plane;
    schema::EditableMeshDataT model;
    std::string error;
    if (!engine::plugins::modeling::mesh::create(options, model, error))
    {
        return false;
    }
    const auto& corners = model.faces.front()->vertices;
    const std::array<engine::plugins::modeling::MeshEdge, 2> edges{
        {{corners[0], corners[1]}, {corners[1], corners[2]}}};
    if (!expect(engine::plugins::modeling::mesh::extrude_edges(model, edges, {0, 1, 0}, error),
                "extrude connected open edges", error) ||
        !expect(model.faces.size() == 3 && model.vertices.size() == 7,
                "edge extrusion shares copied corner"))
    {
        return false;
    }
    schema::MeshDataT mesh;
    return expect(engine::plugins::modeling::mesh::compile(model, "walls.mesh", mesh, error),
                  "edge extruded walls compile", error) &&
           expect(!engine::plugins::modeling::mesh::extrude_edges(model, edges, {0, 1, 0}, error),
                  "edge extrusion rejects now-interior edges");
}

bool point_edge_transform()
{
    std::string error;
    schema::EditableMeshDataT model;
    if (!engine::plugins::modeling::mesh::create({}, model, error))
    {
        return false;
    }
    const auto original_face = model.faces.front()->id;
    const auto selected_id = model.vertices.front().id();
    model.faces.front()->material_slot = 2;
    model.faces.front()->smoothing_group = 7;
    model.faces.front()->colors.assign(4, schema::Color(1, 0, 0, 1));
    engine::plugins::modeling::MeshSelection selection;
    selection.vertices = {selected_id};
    const std::array<double, 16> translate{1, 0, 0, 0, 0,     1,    0,    0,
                                           0, 0, 1, 0, -0.25, -0.2, -0.1, 1};
    if (!expect(engine::plugins::modeling::mesh::transform(model, selection, translate, error),
                "move single cube vertex", error) ||
        !expect(model.faces.size() == 9, "only three affected quads are triangulated") ||
        !expect(
            engine::plugins::modeling::mesh::find_face(model, original_face)->vertices.size() ==
                    3 &&
                engine::plugins::modeling::mesh::find_face(model, original_face)->material_slot ==
                    2 &&
                engine::plugins::modeling::mesh::find_face(model, original_face)->smoothing_group ==
                    7 &&
                engine::plugins::modeling::mesh::find_face(model, original_face)->colors.size() ==
                    3 &&
                engine::plugins::modeling::mesh::find_face(model, original_face)
                        ->texcoords.size() == 3,
            "point edit preserves first face identity and corner attributes"))
    {
        return false;
    }
    schema::MeshDataT compiled;
    if (!expect(engine::plugins::modeling::mesh::compile(model, "point.mesh", compiled, error),
                "point edited cube compiles", error) ||
        !expect(closed(compiled) && volume(compiled) > 0.0, "point edit keeps closed outward mesh"))
    {
        return false;
    }
    selection = {};
    selection.edges.push_back({model.faces.front()->vertices[0], model.faces.front()->vertices[1]});
    if (!expect(engine::plugins::modeling::mesh::transform(model, selection, translate, error),
                "move edge", error))
    {
        return false;
    }
    selection = {};
    selection.all = true;
    const std::array<double, 16> reflect_scale{-2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0.5, 0, 0, 0, 0, 1};
    if (!engine::plugins::modeling::mesh::compile(model, "before.mesh", compiled, error))
    {
        return false;
    }
    const auto before_volume = volume(compiled);
    return expect(
               engine::plugins::modeling::mesh::transform(model, selection, reflect_scale, error),
               "negative nonuniform scale", error) &&
           expect(engine::plugins::modeling::mesh::compile(model, "scaled.mesh", compiled, error),
                  "scaled compile", error) &&
           expect(closed(compiled) && std::abs(volume(compiled) - before_volume * 3.0) < 1.0e-5,
                  "negative scale preserves outward winding and determinant volume");
}

double planar_area(const schema::MeshDataT& mesh)
{
    double total = 0.0;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
    {
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1]];
        const auto& c = mesh.vertices[mesh.indices[i + 2]];
        const std::array<double, 3> ab{static_cast<double>(b.x()) - a.x(),
                                       static_cast<double>(b.y()) - a.y(),
                                       static_cast<double>(b.z()) - a.z()};
        const std::array<double, 3> ac{static_cast<double>(c.x()) - a.x(),
                                       static_cast<double>(c.y()) - a.y(),
                                       static_cast<double>(c.z()) - a.z()};
        total += std::hypot(ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                            ab[0] * ac[1] - ab[1] * ac[0]) *
                 0.5;
    }
    return total;
}

bool group_inset()
{
    engine::schema::ModelingPrimitiveDataT options;
    options.type = engine::schema::ModelingPrimitive::plane;
    options.size = {2, 1, 1};
    options.width_segments = 2;
    schema::EditableMeshDataT model;
    std::string error;
    if (!engine::plugins::modeling::mesh::create(options, model, error))
    {
        return false;
    }
    const std::array<std::uint64_t, 2> faces{model.faces[0]->id, model.faces[1]->id};
    model.faces[1]->material_slot = 1;
    for (auto& uv : model.faces[1]->texcoords)
    {
        uv = {uv.x() + 2.0, uv.y()};
    }
    if (!expect(engine::plugins::modeling::mesh::inset(model, faces, 0.1, error, true),
                "group inset adjacent faces", error) ||
        !expect(model.faces.size() == 8,
                "group inset creates only outer boundary, not internal border"))
    {
        return false;
    }
    const auto* first = engine::plugins::modeling::mesh::find_face(model, faces[0]);
    const auto* second = engine::plugins::modeling::mesh::find_face(model, faces[1]);
    std::size_t shared = 0;
    for (std::size_t i = 0; i < first->vertices.size(); ++i)
    {
        for (std::size_t j = 0; j < second->vertices.size(); ++j)
        {
            if (first->vertices[i] == second->vertices[j])
            {
                ++shared;
                if (!expect(std::abs(second->texcoords[j].x() - first->texcoords[i].x() - 2.0) <
                                1.0e-6,
                            "group inset retains per-face UV seam"))
                {
                    return false;
                }
            }
        }
    }
    schema::MeshDataT compiled;
    if (!expect(shared == 2 && second->material_slot == 1,
                "group keeps internal shared edge and material") ||
        !expect(engine::plugins::modeling::mesh::compile(model, "group.mesh", compiled, error),
                "group compile", error) ||
        !expect(std::abs(planar_area(compiled) - 2.0) < 1.0e-6,
                "group inset partitions original area"))
    {
        return false;
    }
    options.depth_segments = 2;
    options.size = {2, 1, 2};
    if (!engine::plugins::modeling::mesh::create(options, model, error))
    {
        return false;
    }
    const std::array<std::uint64_t, 1> remove{model.faces.back()->id};
    if (!engine::plugins::modeling::mesh::erase_faces(model, remove, error))
    {
        return false;
    }
    std::vector<std::uint64_t> concave;
    for (const auto& face : model.faces)
    {
        concave.push_back(face->id);
    }
    if (!expect(engine::plugins::modeling::mesh::inset(model, concave, 0.1, error, true),
                "group inset concave L region", error) ||
        !expect(engine::plugins::modeling::mesh::compile(model, "l.mesh", compiled, error),
                "concave group compile", error) ||
        !expect(std::abs(planar_area(compiled) - 3.0) < 1.0e-6,
                "concave inset preserves total region area"))
    {
        return false;
    }
    const std::vector<schema::Vec3> outer{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}};
    const std::vector<std::vector<schema::Vec3>> holes{
        {{1, 1, 0}, {3, 1, 0}, {3, 3, 0}, {1, 3, 0}}};
    if (!engine::plugins::modeling::mesh::polygon(outer, holes, model, error))
    {
        return false;
    }
    const std::array<std::uint64_t, 1> holed{model.faces.front()->id};
    if (!expect(engine::plugins::modeling::mesh::inset(model, holed, 0.1, error, true),
                "group inset outer and hole boundaries", error) ||
        !expect(
            engine::plugins::modeling::mesh::compile(model, "holed-inset.mesh", compiled, error),
            "holed inset compile", error) ||
        !expect(std::abs(planar_area(compiled) - 12.0) < 1.0e-5,
                "holed inset preserves original region area"))
    {
        return false;
    }
    std::vector<std::uint8_t> before;
    std::vector<std::uint8_t> after;
    return engine::plugins::modeling::mesh::serialize(model, before, error) &&
           expect(!engine::plugins::modeling::mesh::inset(model, holed, 10.0, error, true),
                  "excess group inset fails") &&
           engine::plugins::modeling::mesh::serialize(model, after, error) &&
           expect(before == after, "failed group inset preserves all original bytes");
}

bool split_and_edge_loop()
{
    std::string error;
    schema::EditableMeshDataT model;
    if (!engine::plugins::modeling::mesh::create({}, model, error))
    {
        return false;
    }
    const auto face_ids = std::array<std::uint64_t, 2>{model.faces[0]->id, model.faces[1]->id};
    std::vector<std::uint8_t> before;
    std::vector<std::uint8_t> after;
    if (!engine::plugins::modeling::mesh::serialize(model, before, error) ||
        !expect(!engine::plugins::modeling::mesh::inset(model, face_ids, 0.1, error, true),
                "noncoplanar group inset fails") ||
        !engine::plugins::modeling::mesh::serialize(model, after, error) ||
        !expect(before == after, "noncoplanar group inset is atomic"))
    {
        return false;
    }
    const std::array<std::uint64_t, 1> corner{model.vertices.front().id()};
    const auto old_uv = model.faces.front()->texcoords;
    if (!expect(engine::plugins::modeling::mesh::split_vertices(model, corner, error),
                "split shared cube vertex", error) ||
        !expect(model.vertices.size() == 10, "split vertex creates per-face identities") ||
        !expect(model.faces.front()->texcoords[0].x() == old_uv[0].x() &&
                    model.faces.front()->texcoords[0].y() == old_uv[0].y(),
                "split vertex preserves corner UV"))
    {
        return false;
    }
    engine::schema::ModelingPrimitiveDataT options;
    options.type = engine::schema::ModelingPrimitive::plane;
    options.width_segments = 4;
    options.depth_segments = 2;
    if (!engine::plugins::modeling::mesh::create(options, model, error))
    {
        return false;
    }
    const engine::plugins::modeling::MeshEdge seed{model.vertices[6].id(), model.vertices[7].id()};
    std::vector<engine::plugins::modeling::MeshEdge> loop;
    std::vector<engine::plugins::modeling::MeshEdge> ring;
    return expect(engine::plugins::modeling::mesh::edge_loop(model, seed, loop, error) &&
                      loop.size() == 4,
                  "edge loop follows connected horizontal edges", error) &&
           expect(engine::plugins::modeling::mesh::edge_ring(model, seed, ring, error) &&
                      ring.size() == 3,
                  "edge ring follows opposite edges through quad rows", error);
}

} // namespace

int main()
{
    return mirrored_uv_tangents() && primitives() && icosphere_tangent_frames() &&
                   primitive_contract() && polygon_hole() &&
                   topology() && attributes_and_edges() && loop_cut_and_normals() &&
                   extruded_hole() && boundary_and_attributes() && bridge_caps() &&
                   extruded_edges() && point_edge_transform() && group_inset() &&
                   split_and_edge_loop()
               ? 0
               : 1;
}
