#include "editor/modeling/modeling_commands.hpp"

#include "editor/commands/editor_commands.hpp"
#include "editor/commands/editor_edit.hpp"
#include "plugins/modeling/editable_mesh.hpp"
#include "plugins/modeling/mesh_geometry.hpp"
#include "plugins/modeling/mesh_primitives.hpp"
#include "plugins/modeling/mesh_topology.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL_filesystem.h>
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace engine
{
namespace
{
using Json = nlohmann::json;
namespace fs = std::filesystem;
constexpr std::size_t maximum_source_size = 64 * 1024 * 1024;

bool fields(const Json& input, std::initializer_list<std::string_view> names)
{
    if (!input.is_object())
    {
        return false;
    }
    for (const auto& item : input.items())
    {
        if (std::find(names.begin(), names.end(), item.key()) == names.end())
        {
            return false;
        }
    }
    return true;
}

bool id(const Json& input, std::uint64_t& output)
{
    if (!input.is_string())
    {
        return false;
    }
    const auto& value = input.get_ref<const std::string&>();
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && output != 0 &&
           value == std::to_string(output);
}

bool id_field(const Json& input, const char* name, std::uint64_t& output)
{
    return input.contains(name) && id(input[name], output);
}

bool number(const Json& input, double& output)
{
    if (!input.is_number())
    {
        return false;
    }
    output = input.get<double>();
    return std::isfinite(output);
}

bool number_field(const Json& input, const char* name, double& output)
{
    return !input.contains(name) || number(input[name], output);
}

bool boolean_field(const Json& input, const char* name, bool& output)
{
    if (!input.contains(name))
    {
        return true;
    }
    if (!input[name].is_boolean())
    {
        return false;
    }
    output = input[name].get<bool>();
    return true;
}

bool uint_field(const Json& input, const char* name, std::uint32_t& output)
{
    if (!input.contains(name))
    {
        return true;
    }
    const auto& value = input[name];
    if (!value.is_number_integer() || value < 0 ||
        value > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    output = value.get<std::uint32_t>();
    return true;
}

template <std::size_t Count> bool vector(const Json& input, std::array<double, Count>& output)
{
    if (!input.is_array() || input.size() != Count)
    {
        return false;
    }
    for (std::size_t index = 0; index < Count; ++index)
    {
        if (!number(input[index], output[index]))
        {
            return false;
        }
    }
    return true;
}

bool vec3_field(const Json& input, const char* name, Vec3& output)
{
    if (!input.contains(name))
    {
        return true;
    }
    std::array<double, 3> value{};
    if (!vector(input[name], value))
    {
        return false;
    }
    output = {value[0], value[1], value[2]};
    return true;
}

bool vec2_field(const Json& input, const char* name, Vec2& output)
{
    if (!input.contains(name))
    {
        return true;
    }
    std::array<double, 2> value{};
    if (!vector(input[name], value))
    {
        return false;
    }
    output = {value[0], value[1]};
    return true;
}

bool ids(const Json& input, std::vector<std::uint64_t>& output)
{
    if (!input.is_array() || input.size() > 100000)
    {
        return false;
    }
    output.clear();
    std::set<std::uint64_t> unique;
    for (const auto& item : input)
    {
        std::uint64_t value = 0;
        if (!id(item, value) || !unique.insert(value).second)
        {
            return false;
        }
        output.push_back(value);
    }
    return true;
}

bool read_selection(const Json& input, const schema::EditableMeshDataT& data, MeshSelection& output,
                    std::string& error)
{
    if (!fields(input, {"vertices", "faces", "edges", "all"}) ||
        !boolean_field(input, "all", output.all) ||
        (input.contains("vertices") && !ids(input["vertices"], output.vertices)) ||
        (input.contains("faces") && !ids(input["faces"], output.faces)))
    {
        error = "Selection requires unique complete vertex/face ID strings and edge ID pairs";
        return false;
    }
    if (input.contains("edges"))
    {
        const auto& edges = input["edges"];
        if (!edges.is_array() || edges.size() > 100000)
        {
            error = "Invalid edge selection";
            return false;
        }
        output.edges.clear();
        std::set<std::pair<std::uint64_t, std::uint64_t>> unique;
        for (const auto& value : edges)
        {
            MeshEdge edge;
            if (!value.is_array() || value.size() != 2 || !id(value[0], edge.first) ||
                !id(value[1], edge.second) || edge.first == edge.second)
            {
                error = "Each edge requires two distinct vertex IDs";
                return false;
            }
            if (!unique
                     .emplace(std::min(edge.first, edge.second), std::max(edge.first, edge.second))
                     .second)
            {
                error = "Selection cannot contain duplicate or reversed duplicate edges";
                return false;
            }
            output.edges.push_back(edge);
        }
    }
    if (!output.all && output.vertices.empty() && output.faces.empty() && output.edges.empty())
    {
        return true;
    }
    std::vector<std::uint64_t> selected;
    return EditableMesh::selected_vertices(data, output, selected, error);
}

Json selection_json(const MeshSelection& selection)
{
    Json vertices = Json::array();
    Json faces = Json::array();
    Json edges = Json::array();
    for (const auto value : selection.vertices)
    {
        vertices.push_back(std::to_string(value));
    }
    for (const auto value : selection.faces)
    {
        faces.push_back(std::to_string(value));
    }
    for (const auto value : selection.edges)
    {
        edges.push_back({std::to_string(value.first), std::to_string(value.second)});
    }
    return {{"vertices", std::move(vertices)},
            {"faces", std::move(faces)},
            {"edges", std::move(edges)},
            {"all", selection.all}};
}

std::vector<std::uint64_t> face_selection(const schema::EditableMeshDataT& data,
                                          const MeshSelection& selection)
{
    auto output = selection.faces;
    if (selection.all)
    {
        output.clear();
        for (const auto& face : data.faces)
        {
            output.push_back(face->id);
        }
    }
    return output;
}

bool points2(const Json& input, std::vector<Vec2>& output)
{
    if (!input.is_array() || input.size() > 10000)
    {
        return false;
    }
    for (const auto& item : input)
    {
        std::array<double, 2> value{};
        if (!vector(item, value))
        {
            return false;
        }
        output.emplace_back(value[0], value[1]);
    }
    return true;
}

bool points3(const Json& input, std::vector<Vec3>& output)
{
    if (!input.is_array() || input.size() > 10000)
    {
        return false;
    }
    for (const auto& item : input)
    {
        std::array<double, 3> value{};
        if (!vector(item, value))
        {
            return false;
        }
        output.emplace_back(value[0], value[1], value[2]);
    }
    return true;
}

bool create_mesh(std::string_view shape, const Json& params, schema::EditableMeshDataT& data,
                 std::string& error)
{
    if (shape == "polygon")
    {
        std::vector<Vec3> points;
        std::vector<std::vector<Vec3>> holes;
        if (!fields(params, {"points", "holes"}) || !params.contains("points") ||
            !points3(params["points"], points))
        {
            error = "Polygon requires points and optional holes";
            return false;
        }
        if (params.contains("holes"))
        {
            if (!params["holes"].is_array() || params["holes"].size() > 1024)
            {
                return false;
            }
            for (const auto& value : params["holes"])
            {
                holes.emplace_back();
                if (!points3(value, holes.back()))
                {
                    return false;
                }
            }
        }
        return MeshPrimitives::polygon(points, holes, data, error);
    }
    if (shape == "sweep")
    {
        MeshSweepOptions options;
        if (!fields(params, {"profile", "path", "closedProfile", "cap", "twistDegrees"}) ||
            !params.contains("profile") || !params.contains("path") ||
            !points2(params["profile"], options.profile) ||
            !points3(params["path"], options.path) ||
            !boolean_field(params, "closedProfile", options.closed_profile) ||
            !boolean_field(params, "cap", options.cap) ||
            !number_field(params, "twistDegrees", options.twist_degrees))
        {
            error = "Invalid sweep profile, path or options";
            return false;
        }
        return MeshGeometry::sweep(options, data, error);
    }
    if (shape == "lathe")
    {
        MeshLatheOptions options;
        if (!fields(params, {"profile", "segments", "angleDegrees", "cap"}) ||
            !params.contains("profile") || !points2(params["profile"], options.profile) ||
            !uint_field(params, "segments", options.segments) ||
            !number_field(params, "angleDegrees", options.angle_degrees) ||
            !boolean_field(params, "cap", options.cap))
        {
            error = "Invalid lathe profile or options";
            return false;
        }
        return MeshGeometry::lathe(options, data, error);
    }
    const std::map<std::string_view, MeshPrimitive> types = {
        {"cube", MeshPrimitive::cube},         {"plane", MeshPrimitive::plane},
        {"cylinder", MeshPrimitive::cylinder}, {"cone", MeshPrimitive::cone},
        {"sphere", MeshPrimitive::sphere},     {"torus", MeshPrimitive::torus},
        {"prism", MeshPrimitive::prism},       {"stairs", MeshPrimitive::stairs},
        {"arch", MeshPrimitive::arch},         {"pipe", MeshPrimitive::pipe},
        {"door", MeshPrimitive::door},         {"curved_stairs", MeshPrimitive::curved_stairs},
        {"sprite", MeshPrimitive::sprite},     {"icosphere", MeshPrimitive::icosphere}};
    const auto type = types.find(shape);
    MeshPrimitiveOptions options;
    if (type == types.end() ||
        !fields(params, {"size", "radius", "innerRadius", "angleDegrees", "segments", "rings",
                         "steps", "widthSegments", "depthSegments", "thickness", "subdivisions"}) ||
        !vec3_field(params, "size", options.size) ||
        !number_field(params, "radius", options.radius) ||
        !number_field(params, "thickness", options.thickness) ||
        !uint_field(params, "subdivisions", options.subdivisions) ||
        !number_field(params, "innerRadius", options.inner_radius) ||
        !number_field(params, "angleDegrees", options.angle_degrees) ||
        !uint_field(params, "segments", options.segments) ||
        !uint_field(params, "rings", options.rings) ||
        !uint_field(params, "steps", options.steps) ||
        !uint_field(params, "widthSegments", options.width_segments) ||
        !uint_field(params, "depthSegments", options.depth_segments))
    {
        error = "Unknown primitive or invalid primitive parameters";
        return false;
    }
    options.type = type->second;
    return MeshPrimitives::create(options, data, error);
}

bool operate(std::string_view operation, const Json& params, const MeshSelection& selection,
             schema::EditableMeshDataT& data, std::string& error)
{
    const auto faces = face_selection(data, selection);
    const auto face_operations = {"extrude",     "inset",        "erase_faces", "reverse",
                                  "triangulate", "subdivide",    "detach",      "duplicate_faces",
                                  "project_uv",  "transform_uv", "unwrap_uv",   "unwrap_face_uv",
                                  "hotspot_uv",  "smoothing",    "material",    "color"};
    if (faces.empty() && std::find(face_operations.begin(), face_operations.end(), operation) !=
                             face_operations.end())
    {
        error = "This operation requires selected faces or selection.all = true";
        return false;
    }
    double distance = 0.1;
    if (operation == "transform")
    {
        std::array<double, 16> matrix{};
        if (!fields(params, {"matrix"}) || !params.contains("matrix") ||
            !vector(params["matrix"], matrix))
        {
            error = "Transform requires a 16-number column-major affine matrix";
            return false;
        }
        return MeshTopology::transform(data, selection, matrix, error);
    }
    if (operation == "extrude")
    {
        bool individual = false;
        return fields(params, {"distance", "individual"}) &&
               number_field(params, "distance", distance) &&
               boolean_field(params, "individual", individual) &&
               MeshTopology::extrude(data, faces, distance, individual, error);
    }
    if (operation == "extrude_edges")
    {
        Vec3 delta;
        return fields(params, {"delta"}) && params.contains("delta") &&
               vec3_field(params, "delta", delta) &&
               MeshTopology::extrude_edges(data, selection.edges, delta, error);
    }
    if (operation == "inset")
    {
        bool group = false;
        return fields(params, {"distance", "group"}) &&
               number_field(params, "distance", distance) &&
               boolean_field(params, "group", group) &&
               MeshTopology::inset(data, faces, distance, error, group);
    }
    if (operation == "erase_faces" || operation == "reverse" || operation == "triangulate" ||
        operation == "detach" || operation == "duplicate_faces")
    {
        if (!params.empty())
        {
            error = "This operation takes no parameters";
            return false;
        }
        if (operation == "erase_faces")
        {
            return MeshTopology::erase_faces(data, faces, error);
        }
        if (operation == "reverse")
        {
            return MeshTopology::reverse_faces(data, faces, error);
        }
        if (operation == "triangulate")
        {
            return MeshTopology::triangulate(data, faces, error);
        }
        if (operation == "detach")
        {
            return MeshTopology::detach_faces(data, faces, error);
        }
        return MeshTopology::duplicate_faces(data, faces, error);
    }
    if (operation == "subdivide")
    {
        std::uint32_t iterations = 1;
        return fields(params, {"iterations"}) && uint_field(params, "iterations", iterations) &&
               MeshTopology::subdivide(data, faces, iterations, error);
    }
    if (operation == "split_vertices")
    {
        std::vector<std::uint64_t> vertices;
        return params.empty() &&
               EditableMesh::selected_vertices(data, selection, vertices, error) &&
               MeshTopology::split_vertices(data, vertices, error);
    }
    if (operation == "weld" || operation == "merge_vertices")
    {
        std::vector<std::uint64_t> vertices;
        if (!EditableMesh::selected_vertices(data, selection, vertices, error))
        {
            return false;
        }
        if (operation == "weld")
        {
            double tolerance = 0.000001;
            return fields(params, {"tolerance"}) && number_field(params, "tolerance", tolerance) &&
                   MeshTopology::weld(data, vertices, tolerance, error);
        }
        Vec3 position;
        return fields(params, {"position"}) && params.contains("position") &&
               vec3_field(params, "position", position) &&
               MeshTopology::merge_vertices(data, vertices, position, error);
    }
    if (operation == "bridge" || operation == "fill_hole")
    {
        std::vector<std::uint64_t> first;
        std::vector<std::uint64_t> second;
        if (operation == "fill_hole")
        {
            return fields(params, {"loop"}) && params.contains("loop") &&
                   ids(params["loop"], first) && MeshTopology::fill_hole(data, first, error);
        }
        return fields(params, {"firstLoop", "secondLoop"}) && params.contains("firstLoop") &&
               params.contains("secondLoop") && ids(params["firstLoop"], first) &&
               ids(params["secondLoop"], second) &&
               MeshTopology::bridge(data, first, second, error);
    }
    if (operation == "loop_cut")
    {
        std::uint32_t cuts = 1;
        double pinch = 0;
        double offset = 0;
        if (selection.edges.size() != 1)
        {
            error = "Loop cut requires exactly one selected seed edge";
            return false;
        }
        return fields(params, {"cuts", "pinch", "offset"}) && uint_field(params, "cuts", cuts) &&
               number_field(params, "pinch", pinch) && number_field(params, "offset", offset) &&
               MeshTopology::loop_cut(data, selection.edges[0], cuts, pinch, offset, error);
    }
    if (operation == "dissolve_edge" || operation == "split_edge")
    {
        if (selection.edges.size() != 1)
        {
            error = "This operation requires exactly one selected edge";
            return false;
        }
        if (operation == "dissolve_edge")
        {
            return params.empty() && MeshTopology::dissolve_edge(data, selection.edges[0], error);
        }
        double fraction = 0.5;
        return fields(params, {"fraction"}) && number_field(params, "fraction", fraction) &&
               MeshTopology::split_edge(data, selection.edges[0], fraction, error);
    }
    if (operation == "connect_vertices")
    {
        std::uint64_t face = 0;
        if (selection.vertices.size() != 2 || !fields(params, {"face"}) ||
            !id_field(params, "face", face))
        {
            error = "Connect requires two selected vertices and a face ID";
            return false;
        }
        return MeshTopology::connect_vertices(data, face, selection.vertices[0],
                                              selection.vertices[1], error);
    }
    if (operation == "mirror")
    {
        MeshMirrorOptions options;
        return fields(params, {"normal", "offset", "duplicate", "weld", "tolerance"}) &&
               vec3_field(params, "normal", options.normal) &&
               number_field(params, "offset", options.offset) &&
               boolean_field(params, "duplicate", options.duplicate) &&
               boolean_field(params, "weld", options.weld) &&
               number_field(params, "tolerance", options.tolerance) &&
               MeshGeometry::mirror(data, options, error);
    }
    if (operation == "clip")
    {
        MeshClipOptions options;
        return fields(params, {"normal", "offset", "keepPositive", "cap", "materialSlot"}) &&
               vec3_field(params, "normal", options.normal) &&
               number_field(params, "offset", options.offset) &&
               boolean_field(params, "keepPositive", options.keep_positive) &&
               boolean_field(params, "cap", options.cap) &&
               uint_field(params, "materialSlot", options.cap_material_slot) &&
               MeshGeometry::clip(data, options, error);
    }
    if (operation == "bevel")
    {
        MeshBevelOptions options;
        options.edges = selection.edges;
        return fields(params, {"width", "segments", "profile", "materialSlot"}) &&
               number_field(params, "width", options.width) &&
               number_field(params, "profile", options.profile) &&
               uint_field(params, "segments", options.segments) &&
               uint_field(params, "materialSlot", options.material_slot) &&
               MeshGeometry::bevel(data, options, error);
    }
    if (operation == "project_uv")
    {
        MeshUvProjectionOptions options;
        const auto kind = params.value("projection", Json("planar"));
        if (kind == "planar")
        {
            options.projection = MeshUvProjection::planar;
        }
        else if (kind == "box")
        {
            options.projection = MeshUvProjection::box;
        }
        else if (kind == "cylindrical")
        {
            options.projection = MeshUvProjection::cylindrical;
        }
        else if (kind == "spherical")
        {
            options.projection = MeshUvProjection::spherical;
        }
        else
        {
            error = "Unknown UV projection";
            return false;
        }
        return fields(params, {"projection", "normal", "scale", "offset"}) &&
               vec3_field(params, "normal", options.normal) &&
               vec2_field(params, "scale", options.scale) &&
               vec2_field(params, "offset", options.offset) &&
               MeshGeometry::project_uv(data, faces, options, error);
    }
    if (operation == "transform_uv")
    {
        MeshUvTransformOptions options;
        return fields(params, {"scale", "offset", "pivot", "rotationDegrees"}) &&
               vec2_field(params, "scale", options.scale) &&
               vec2_field(params, "offset", options.offset) &&
               vec2_field(params, "pivot", options.pivot) &&
               number_field(params, "rotationDegrees", options.rotation_degrees) &&
               MeshGeometry::transform_uv(data, faces, options, error);
    }
    if (operation == "unwrap_uv" || operation == "unwrap_face_uv")
    {
        MeshUvUnwrapOptions options;
        if (operation == "unwrap_face_uv")
        {
            return fields(params, {"padding"}) &&
                   number_field(params, "padding", options.padding) &&
                   MeshGeometry::unwrap_face_uv(data, faces, options, error);
        }
        if (!fields(params, {"padding", "resolution", "iterations", "rotate",
                             "respectExistingSeams", "seams"}) ||
            !number_field(params, "padding", options.padding) ||
            !uint_field(params, "resolution", options.resolution) ||
            !uint_field(params, "iterations", options.iterations) ||
            !boolean_field(params, "rotate", options.rotate) ||
            !boolean_field(params, "respectExistingSeams", options.respect_existing_seams))
        {
            return false;
        }
        if (params.contains("seams"))
        {
            MeshSelection seams;
            if (!read_selection({{"edges", params["seams"]}}, data, seams, error))
            {
                return false;
            }
            options.seams = std::move(seams.edges);
        }
        return MeshGeometry::unwrap_uv(data, faces, options, error);
    }
    if (operation == "hotspot_uv")
    {
        MeshHotspotOptions options;
        if (!fields(params, {"rectangles", "padding", "allowRotation"}) ||
            !params.contains("rectangles") || !params["rectangles"].is_array() ||
            params["rectangles"].size() > 10000 ||
            !number_field(params, "padding", options.padding) ||
            !boolean_field(params, "allowRotation", options.allow_rotation))
        {
            return false;
        }
        for (const auto& rectangle : params["rectangles"])
        {
            std::array<double, 4> value{};
            if (!vector(rectangle, value))
            {
                return false;
            }
            options.rectangles.emplace_back(value[0], value[1], value[2], value[3]);
        }
        return MeshGeometry::hotspot_uv(data, faces, options, error);
    }
    if (operation == "smoothing")
    {
        std::uint32_t group = 0;
        return fields(params, {"group"}) && uint_field(params, "group", group) &&
               MeshGeometry::set_smoothing(data, faces, group, error);
    }
    if (operation == "auto_smooth")
    {
        double angle = 60;
        return fields(params, {"angleDegrees"}) && number_field(params, "angleDegrees", angle) &&
               MeshGeometry::auto_smooth(data, angle, error);
    }
    if (operation == "material" || operation == "color")
    {
        std::uint32_t slot = 0;
        std::array<double, 4> color{};
        if ((operation == "material" &&
             (!fields(params, {"slot"}) || !uint_field(params, "slot", slot) || slot >= 16)) ||
            (operation == "color" &&
             (!fields(params, {"value"}) || !params.contains("value") ||
              !vector(params["value"], color) ||
              std::any_of(color.begin(), color.end(), [](double v) { return v < 0 || v > 1; }))) ||
            faces.empty())
        {
            error = "Material/color requires selected faces and valid values";
            return false;
        }
        return operation == "material"
                   ? MeshTopology::set_material(data, faces, slot, error)
                   : MeshTopology::set_color(
                         data, faces,
                         Color(static_cast<float>(color[0]), static_cast<float>(color[1]),
                               static_cast<float>(color[2]), static_cast<float>(color[3])),
                         error);
    }
    error = "Unknown modeling operation";
    return false;
}

Json capture_material(const Material& material)
{
    Json values = Json::array();
    const auto* shader = ResourceSystem::find_shader(material.shader_path());
    if (shader)
    {
        for (const auto& parameter : shader->parameters)
        {
            Json value;
            std::string type;
            if (parameter->default_value.type == schema::MaterialValue::FloatValue)
            {
                float number = 0;
                if (material.get_float(parameter->name, number))
                {
                    type = "float";
                    value = number;
                }
            }
            else if (parameter->default_value.type == schema::MaterialValue::Vec2Value)
            {
                Vec2 number;
                if (material.get_v2(parameter->name, number))
                {
                    type = "v2";
                    value = {number.x(), number.y()};
                }
            }
            else if (parameter->default_value.type == schema::MaterialValue::Vec3Value)
            {
                Vec3 number;
                if (material.get_v3(parameter->name, number))
                {
                    type = "v3";
                    value = {number.x(), number.y(), number.z()};
                }
            }
            else if (parameter->default_value.type == schema::MaterialValue::Vec4Value)
            {
                Vec4 number;
                if (material.get_v4(parameter->name, number))
                {
                    type = "v4";
                    value = {number.x(), number.y(), number.z(), number.w()};
                }
            }
            if (!type.empty())
            {
                values.push_back({{"name", parameter->name}, {"type", type}, {"value", value}});
            }
        }
        for (const auto& sampler : shader->samplers)
        {
            std::string_view path;
            if (material.get_texture(sampler->name, path))
            {
                values.push_back({{"name", sampler->name}, {"type", "texture"}, {"value", path}});
            }
        }
    }
    return {{"type", material.type_path()},
            {"shader", material.shader_path()},
            {"values", std::move(values)}};
}

bool restore_material(Material& material, const Json& snapshot)
{
    if (!material.set_type(snapshot["type"].get<std::string>()) ||
        !material.set_shader(snapshot["shader"].get<std::string>()))
    {
        return false;
    }
    for (const auto& item : snapshot["values"])
    {
        const auto name = item["name"].get<std::string>();
        const auto& value = item["value"];
        bool success = false;
        if (item["type"] == "float")
        {
            success = material.set_float(name, value.get<float>());
        }
        else if (item["type"] == "v2")
        {
            success = material.set_v2(name, {value[0].get<double>(), value[1].get<double>()});
        }
        else if (item["type"] == "v3")
        {
            success = material.set_v3(
                name, {value[0].get<double>(), value[1].get<double>(), value[2].get<double>()});
        }
        else if (item["type"] == "v4")
        {
            success = material.set_v4(name, {value[0].get<double>(), value[1].get<double>(),
                                             value[2].get<double>(), value[3].get<double>()});
        }
        else if (item["type"] == "texture")
        {
            success = material.set_texture(name, value.get<std::string>());
        }
        if (!success)
        {
            return false;
        }
    }
    return true;
}

Json visual_snapshot(const Node& node)
{
    const auto* transform = node.get_component<Transform>();
    const auto* renderer = node.get_component<MeshRenderer>();
    if (!transform || !renderer)
    {
        return nullptr;
    }
    const auto& settings = transform->settings();
    Json materials = Json::array();
    for (std::size_t slot = 0; slot < renderer->material_count(); ++slot)
    {
        materials.push_back(capture_material(*renderer->material(slot)));
    }
    return {{"position", {settings.position.x(), settings.position.y(), settings.position.z()}},
            {"rotation", {settings.rotation.x(), settings.rotation.y(), settings.rotation.z()}},
            {"scale", {settings.scale.x(), settings.scale.y(), settings.scale.z()}},
            {"materials", std::move(materials)},
            {"enabled", renderer->enabled()},
            {"transformEnabled", transform->enabled()}};
}

bool restore_visual(Node& node, const Json& snapshot)
{
    if (snapshot.is_null())
    {
        return true;
    }
    auto* transform = node.get_component<Transform>();
    auto* renderer = node.get_component<MeshRenderer>();
    auto settings = transform->settings();
    if (!vec3_field(snapshot, "position", settings.position) ||
        !vec3_field(snapshot, "rotation", settings.rotation) ||
        !vec3_field(snapshot, "scale", settings.scale) || !transform->set_settings(settings) ||
        !renderer->set_material_count(snapshot["materials"].size()))
    {
        return false;
    }
    for (std::size_t slot = 0; slot < snapshot["materials"].size(); ++slot)
    {
        if (!restore_material(*renderer->material(slot), snapshot["materials"][slot]))
        {
            return false;
        }
    }
    renderer->set_enabled(snapshot["enabled"].get<bool>());
    transform->set_enabled(snapshot["transformEnabled"].get<bool>());
    return true;
}

bool relative_matrix(const Node& destination, const Node& source, std::array<double, 16>& output)
{
    std::array<float, 16> a{};
    std::array<float, 16> b{};
    const auto* first = destination.get_component<Transform>();
    const auto* second = source.get_component<Transform>();
    if (!first || !second || !first->world_matrix(a.data()) || !second->world_matrix(b.data()))
    {
        return false;
    }
    const double determinant = a[0] * (a[5] * a[10] - a[9] * a[6]) -
                               a[4] * (a[1] * a[10] - a[9] * a[2]) +
                               a[8] * (a[1] * a[6] - a[5] * a[2]);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12)
    {
        return false;
    }
    std::array<double, 16> inverse{};
    inverse[0] = (a[5] * a[10] - a[9] * a[6]) / determinant;
    inverse[4] = (a[8] * a[6] - a[4] * a[10]) / determinant;
    inverse[8] = (a[4] * a[9] - a[8] * a[5]) / determinant;
    inverse[1] = (a[9] * a[2] - a[1] * a[10]) / determinant;
    inverse[5] = (a[0] * a[10] - a[8] * a[2]) / determinant;
    inverse[9] = (a[8] * a[1] - a[0] * a[9]) / determinant;
    inverse[2] = (a[1] * a[6] - a[5] * a[2]) / determinant;
    inverse[6] = (a[4] * a[2] - a[0] * a[6]) / determinant;
    inverse[10] = (a[0] * a[5] - a[4] * a[1]) / determinant;
    inverse[15] = 1;
    for (std::size_t row = 0; row < 3; ++row)
    {
        inverse[12 + row] =
            -(inverse[row] * a[12] + inverse[4 + row] * a[13] + inverse[8 + row] * a[14]);
    }
    output = {};
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                output[column * 4 + row] += inverse[inner * 4 + row] * b[column * 4 + inner];
            }
        }
    }
    return std::all_of(output.begin(), output.end(),
                       [](double value) { return std::isfinite(value); });
}

bool safe_asset_path(const fs::path& root, std::string_view name, fs::path& output,
                     std::string& error)
{
    output.clear();
    const auto relative = PathUtils::from_utf8(name);
    if (name.empty() || name.find('\0') != std::string_view::npos || relative.is_absolute() ||
        relative.has_root_name() || relative.extension() != ".mesh")
    {
        error = "Model path must be a relative .mesh path inside assets";
        return false;
    }
    for (const auto& component : relative)
    {
        if (component == ".." || component == ".")
        {
            error = "Model paths cannot traverse directories";
            return false;
        }
    }
    std::error_code ec;
    const auto canonical_root = fs::canonical(root, ec);
    const auto assets = canonical_root / "assets";
    if (ec || !fs::is_directory(assets, ec) || ec)
    {
        error = "Project assets directory is unavailable";
        return false;
    }
    auto candidate = assets / relative;
    fs::path cursor;
    for (const auto& component : candidate)
    {
        cursor /= component;
        const auto status = fs::symlink_status(cursor, ec);
        if (ec && ec != std::errc::no_such_file_or_directory)
        {
            error = "Cannot inspect model path";
            return false;
        }
        ec.clear();
        if (fs::is_symlink(status))
        {
            error = "Model paths cannot contain symbolic links";
            return false;
        }
    }
    output = std::move(candidate);
    return true;
}

bool save_source(const fs::path& path, const schema::EditableMeshDataT& source,
                 const schema::MeshDataT& mesh, std::string& error)
{
    std::vector<std::uint8_t> bytes;
    if (!EditableMesh::serialize(source, bytes, error))
    {
        return false;
    }
    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    output << "vertices = [\n";
    for (const auto& vertex : mesh.vertices)
    {
        output << "  [" << vertex.x() << ", " << vertex.y() << ", " << vertex.z() << ", "
               << vertex.nx() << ", " << vertex.ny() << ", " << vertex.nz() << "],\n";
    }
    output << "]\nindices = [";
    for (const auto value : mesh.indices)
    {
        output << value << ',';
    }
    output << "]\ntexcoords = [\n";
    for (const auto& value : mesh.texcoords)
    {
        output << "  [" << value.x() << ", " << value.y() << "],\n";
    }
    output << "]\ncolors = [\n";
    for (const auto& value : mesh.colors)
    {
        output << "  [" << value.r() << ", " << value.g() << ", " << value.b() << ", " << value.a()
               << "],\n";
    }
    output << "]\nsubmeshes = [\n";
    for (const auto& submesh : mesh.submeshes)
    {
        output << "  { first_index = " << submesh.first_index()
               << ", index_count = " << submesh.index_count()
               << ", material_slot = " << submesh.material_slot() << " },\n";
    }
    output << "]\n\n[modeling]\nformat = \"flatbuffers-hex\"\ndata = \"";
    constexpr std::string_view digits = "0123456789abcdef";
    for (const auto value : bytes)
    {
        output << digits[value >> 4] << digits[value & 15];
    }
    output << "\"\n";
    const auto text = output.str();
    if (text.size() > maximum_source_size)
    {
        error = "Model source exceeds the 64 MiB limit";
        return false;
    }
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec)
    {
        error = "Cannot create model source directory";
        return false;
    }
    auto temporary = path;
    temporary += ".modeling-write.meta";
    const auto status = fs::symlink_status(temporary, ec);
    if ((!ec && status.type() != fs::file_type::not_found) ||
        (ec && ec != std::errc::no_such_file_or_directory))
    {
        error = "Model temporary destination already exists or cannot be inspected";
        return false;
    }
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    if (!file ||
        !SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(), PathUtils::to_utf8(path).c_str()))
    {
        fs::remove(temporary, ec);
        error = "Cannot publish model source";
        return false;
    }
    return true;
}

bool load_source(const fs::path& path, schema::EditableMeshDataT& source, std::string& error)
{
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    std::string text;
    if (ec || size > maximum_source_size || !ResourceSystem::read_text(path, text) ||
        text.size() > maximum_source_size)
    {
        error = "Cannot read model source or it exceeds 64 MiB";
        return false;
    }
    auto parsed = toml::parse(text);
    if (!parsed)
    {
        error = "Model source contains invalid TOML";
        return false;
    }
    const auto format = parsed["modeling"]["format"].value<std::string>();
    const auto encoded = parsed["modeling"]["data"].value<std::string>();
    if (!format || *format != "flatbuffers-hex" || !encoded || encoded->size() % 2 != 0)
    {
        error = "Model source requires modeling.format = flatbuffers-hex and encoded data";
        return false;
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(encoded->size() / 2);
    const auto digit = [](char value) -> int
    {
        if (value >= '0' && value <= '9')
        {
            return value - '0';
        }
        return value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
    };
    for (std::size_t index = 0; index < encoded->size(); index += 2)
    {
        const auto high = digit((*encoded)[index]);
        const auto low = digit((*encoded)[index + 1]);
        if (high < 0 || low < 0)
        {
            error = "Model source has invalid hexadecimal data";
            return false;
        }
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return EditableMesh::deserialize(bytes, source, error);
}

bool modeling_help(const Json& arguments, Json& result, std::string& error)
{
    if (!fields(arguments, {"operation"}) ||
        (arguments.contains("operation") && !arguments["operation"].is_string()))
    {
        error = "modeling_help accepts an optional operation name";
        return false;
    }
    const Json identity_matrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const Json examples = {
        {"transform", {{"matrix", identity_matrix}}},
        {"extrude", {{"distance", 0.1}, {"individual", false}}},
        {"extrude_edges", {{"delta", {0, 1, 0}}}},
        {"inset", {{"distance", 0.1}, {"group", false}}},
        {"erase_faces", Json::object()},
        {"reverse", Json::object()},
        {"triangulate", Json::object()},
        {"subdivide", {{"iterations", 1}}},
        {"weld", {{"tolerance", 0.000001}}},
        {"merge_vertices", {{"position", {0, 0, 0}}}},
        {"detach", Json::object()},
        {"split_vertices", Json::object()},
        {"duplicate_faces", Json::object()},
        {"bridge", {{"firstLoop", {"1", "2", "3"}}, {"secondLoop", {"4", "5", "6"}}}},
        {"fill_hole", {{"loop", {"1", "2", "3"}}}},
        {"dissolve_edge", Json::object()},
        {"split_edge", {{"fraction", 0.5}}},
        {"loop_cut", {{"cuts", 1}, {"pinch", 0}, {"offset", 0}}},
        {"connect_vertices", {{"face", "7"}}},
        {"mirror",
         {{"normal", {1, 0, 0}},
          {"offset", 0},
          {"duplicate", true},
          {"weld", true},
          {"tolerance", 0.000001}}},
        {"mirror_mode",
         {{"action", "enable"},
          {"normal", {1, 0, 0}},
          {"offset", 0},
          {"weld", true},
          {"tolerance", 0.000001}}},
        {"clip",
         {{"normal", {1, 0, 0}},
          {"offset", 0},
          {"keepPositive", true},
          {"cap", true},
          {"materialSlot", 0}}},
        {"bevel", {{"width", 0.1}, {"segments", 2}, {"profile", 0.5}, {"materialSlot", 0}}},
        {"boolean", {{"otherModelId", "2"}, {"mode", "subtract"}}},
        {"project_uv",
         {{"projection", "planar"}, {"normal", {0, 1, 0}}, {"scale", {1, 1}}, {"offset", {0, 0}}}},
        {"transform_uv",
         {{"scale", {1, 1}}, {"offset", {0, 0}}, {"pivot", {0.5, 0.5}}, {"rotationDegrees", 0}}},
        {"unwrap_uv",
         {{"padding", 0.01},
          {"resolution", 1024},
          {"iterations", 2},
          {"rotate", true},
          {"respectExistingSeams", false},
          {"seams", Json::array()}}},
        {"unwrap_face_uv", {{"padding", 0.01}}},
        {"hotspot_uv",
         {{"rectangles", {{0, 0, 0.5, 1}, {0.5, 0, 0.5, 1}}},
          {"padding", 0.01},
          {"allowRotation", true}}},
        {"smoothing", {{"group", 1}}},
        {"auto_smooth", {{"angleDegrees", 60}}},
        {"material", {{"slot", 0}}},
        {"color", {{"value", {1, 0, 0, 1}}}}};
    Json operations = Json::object();
    for (const auto& entry : examples.items())
    {
        Json properties = Json::object();
        for (const auto& field : entry.value().items())
        {
            const auto& value = field.value();
            const auto type = value.is_boolean()  ? "boolean"
                              : value.is_string() ? "string"
                              : value.is_array()  ? "array"
                                                  : "number";
            properties[field.key()] = {{"type", type}, {"example", value}};
        }
        operations[entry.key()] = {{"parameters",
                                    {{"type", "object"},
                                     {"properties", std::move(properties)},
                                     {"additionalProperties", false}}},
                                   {"example",
                                    {{"sceneId", "<current sceneId>"},
                                     {"modelId", "<modelId>"},
                                     {"operation", entry.key()},
                                     {"selection", {{"all", true}}},
                                     {"parameters", entry.value()}}}};
    }
    operations["transform"]["notes"] = "Column-major affine 4x4; selected geometric vertices.";
    operations["extrude_edges"]["notes"] =
        "Select open boundary edges; delta is in local model space.";
    operations["bevel"]["notes"] = "Select edges; closed convex meshes only; width must fit.";
    operations["mirror_mode"]["notes"] =
        "enable derives mirrored render geometry from editable source; cancel removes the derived "
        "half; finish bakes it into source. Saving bakes the current mirror without ending the "
        "mode.";
    operations["boolean"]["notes"] =
        "mode: union/subtract/intersect. Scene transforms are baked into target local space. "
        "Operands must be closed manifold volumes. Target mirror mode is baked and finished.";
    operations["project_uv"]["notes"] = "projection: planar/box/cylindrical/spherical.";
    operations["unwrap_uv"]["notes"] =
        "xatlas cross-face automatic unwrap and atlas packing; optional seams are vertex ID pairs.";
    operations["unwrap_face_uv"]["notes"] =
        "Independent planar polygon islands with atlas packing.";
    operations["bridge"]["notes"] =
        "Ordered boundary loops; preserve winding; no repeated endpoint.";
    operations["fill_hole"]["notes"] = "Ordered boundary vertex IDs; no repeated endpoint.";
    operations["dissolve_edge"]["notes"] = "Exactly one selected shared edge.";
    operations["loop_cut"]["notes"] = "One seed edge in a quad strip; cuts 1..64, pinch (-1,1).";
    operations["split_edge"]["notes"] = "Exactly one selected edge; fraction in (0,1).";
    operations["connect_vertices"]["notes"] =
        "Exactly two selected vertices within the given face.";
    operations["material"]["notes"] =
        "Face material slots 0..15; set_material_parameter edits slot colors.";
    operations["create"] = {
        {"tool", "modeling_create"},
        {"primitiveParameters",
         {{"size", {1, 1, 1}},
          {"radius", 0.5},
          {"innerRadius", 0.25},
          {"angleDegrees", 180},
          {"segments", 24},
          {"rings", 12},
          {"steps", 8},
          {"widthSegments", 1},
          {"depthSegments", 1},
          {"thickness", 0.1},
          {"subdivisions", 2}}},
        {"shapes",
         {"cube", "plane", "cylinder", "cone", "sphere", "torus", "prism", "stairs", "arch", "pipe",
          "door", "curved_stairs", "sprite", "icosphere", "polygon", "sweep", "lathe"}},
        {"polygon", {{"points", {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}}, {"holes", Json::array()}}},
        {"sweep",
         {{"profile", {{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.5}, {-0.5, 0.5}}},
          {"path", {{0, 0, 0}, {0, 0, 2}}},
          {"closedProfile", true},
          {"cap", true},
          {"twistDegrees", 0}}},
        {"lathe",
         {{"profile", {{0.5, -0.5}, {0.5, 0.5}}},
          {"segments", 24},
          {"angleDegrees", 360},
          {"cap", true}}}};
    operations["select"] = {
        {"tool", "modeling_select"},
        {"modes", {"replace", "grow", "shrink", "connected", "edge_ring", "edge_loop"}},
        {"selection",
         {{"vertices", Json::array()},
          {"edges", Json::array()},
          {"faces", Json::array()},
          {"all", false}}},
        {"notes",
         "IDs are decimal strings from modeling_get. grow/shrink/connected use faces; "
         "edge_ring follows opposite quad edges; edge_loop follows the longitudinal quad strip."}};
    if (arguments.contains("operation"))
    {
        const auto operation = arguments["operation"].get<std::string>();
        if (!operations.contains(operation))
        {
            error = "Unknown modeling operation";
            return false;
        }
        result = {{"operation", operation}, {"contract", operations[operation]}};
    }
    else
    {
        result = {
            {"operations", std::move(operations)},
            {"workflow",
             {"get_scene", "modeling_create", "modeling_get", "modeling_select", "modeling_edit",
              "modeling_save", "stop_project", "sync_assets"}},
            {"identity", "modelId remains stable across undo/redo; query current nodeId."},
            {"selection", "Omitted edit selection uses persistent selection; operations clear it."},
            {"history",
             "All scene mutations reuse execute_batch/undo/redo. Save is not undoable."}};
    }
    return true;
}

struct ModelRecord
{
    std::uint64_t model_id = 0;
    std::uint64_t node_id = 0;
    std::uint64_t mesh_revision = 0;
    std::string name;
    schema::EditableMeshDataT source;
    schema::MeshDataT compiled;
    std::vector<std::uint8_t> bytes;
    MeshSelection selection;
    bool mirror_enabled = false;
    MeshMirrorOptions mirror;
    Json visual = nullptr;
};

struct ModelChange
{
    std::uint64_t model_id = 0;
    std::unique_ptr<ModelRecord> before;
    std::unique_ptr<ModelRecord> after;
};

bool name_field(const Json& input, std::string& name)
{
    if (!input.contains("name"))
    {
        return true;
    }
    if (!input["name"].is_string())
    {
        return false;
    }
    name = input["name"].get<std::string>();
    return !name.empty() && name.size() <= 128 && name.find('\0') == std::string::npos;
}

Json mirror_json(const ModelRecord& record)
{
    return {
        {"enabled", record.mirror_enabled},
        {"halfSpaceOnly", record.mirror.require_half_space},
        {"normal", {record.mirror.normal.x(), record.mirror.normal.y(), record.mirror.normal.z()}},
        {"offset", record.mirror.offset},
        {"weld", record.mirror.weld},
        {"tolerance", record.mirror.tolerance}};
}

bool prepare_record(ModelRecord& record, std::string& error)
{
    auto render_source = record.source;
    return EditableMesh::serialize(record.source, record.bytes, error) &&
           (!record.mirror_enabled || MeshGeometry::mirror(render_source, record.mirror, error)) &&
           EditableMesh::compile(render_source,
                                 "builtin/modeling/" + std::to_string(record.model_id),
                                 record.compiled, error);
}
} // namespace

struct ModelingCommands::State
{
    WorldSystem& world;
    fs::path project_root;
    std::uint64_t scene_id = 0;
    std::uint64_t next_model_id = 1;
    std::map<std::uint64_t, ModelRecord> records;

    State(WorldSystem& value, fs::path root) : world(value), project_root(std::move(root))
    {
    }

    Scene* scene(std::string& error)
    {
        auto* active = world.scene();
        if (!active || active->id() != scene_id)
        {
            error = "The requested modeling scene is no longer active";
            return nullptr;
        }
        return active;
    }

    void synchronize_scene() noexcept
    {
        const auto* active = world.scene();
        const auto generation = active ? active->id() : 0;
        if (scene_id != generation)
        {
            records.clear();
            scene_id = generation;
            if (!active)
            {
                return;
            }
            for (const auto& node : active->nodes())
            {
                const auto* renderer = node->get_component<MeshRenderer>();
                if (!renderer || !renderer->mesh_data() ||
                    !renderer->mesh_path().starts_with("project/"))
                {
                    continue;
                }
                fs::path path;
                std::string error;
                ModelRecord record;
                if (!safe_asset_path(project_root, renderer->mesh_path().substr(8), path, error) ||
                    !load_source(path, record.source, error) ||
                    !EditableMesh::serialize(record.source, record.bytes, error))
                {
                    continue;
                }
                record.model_id = next_model_id++;
                record.node_id = node->id();
                record.mesh_revision = renderer->mesh_revision();
                record.name = node->name();
                record.compiled = *renderer->mesh_data();
                records.emplace(record.model_id, std::move(record));
            }
        }
    }

    bool enter(const Json& arguments, std::string& error)
    {
        synchronize_scene();
        const auto* active = world.scene();
        std::uint64_t requested = 0;
        if (!id_field(arguments, "sceneId", requested) || requested != scene_id || !active)
        {
            error = "modeling commands require the current sceneId decimal string";
            return false;
        }
        return true;
    }

    Node* node(ModelRecord& record, std::string& error)
    {
        auto* active = scene(error);
        auto* value = active ? active->find_node(record.node_id) : nullptr;
        auto* renderer = value ? value->get_component<MeshRenderer>() : nullptr;
        if (!renderer || renderer->mesh_revision() != record.mesh_revision)
        {
            error = "Model node or mesh changed outside modeling commands";
            return nullptr;
        }
        return value;
    }

    ModelRecord* target(const Json& arguments, std::string& error)
    {
        std::uint64_t model_id = 0;
        if (!id_field(arguments, "modelId", model_id))
        {
            error = "modelId must be a complete nonzero decimal string";
            return nullptr;
        }
        const auto found = records.find(model_id);
        if (found == records.end())
        {
            error = "Model does not exist in this scene";
            return nullptr;
        }
        if (arguments.contains("nodeId"))
        {
            std::uint64_t node_id = 0;
            if (!id(arguments["nodeId"], node_id) || node_id != found->second.node_id)
            {
                error = "nodeId is stale; query the stable modelId for its current nodeId";
                return nullptr;
            }
        }
        return node(found->second, error) ? &found->second : nullptr;
    }

    std::unique_ptr<ModelRecord> snapshot(ModelRecord& record, std::string& error)
    {
        auto* value = node(record, error);
        if (!value)
        {
            return nullptr;
        }
        auto result = std::make_unique<ModelRecord>(record);
        result->visual = visual_snapshot(*value);
        return result;
    }

    bool removable(ModelRecord& record, std::string& error)
    {
        auto* value = node(record, error);
        if (!value)
        {
            return false;
        }
        if (value->parent() || value->components().size() != 2)
        {
            error = "Detach parenting and extra components before deleting a modeling object";
            return false;
        }
        for (const auto& other : value->scene().nodes())
        {
            if (other->parent() == value)
            {
                error = "Modeling deletion cannot discard child nodes";
                return false;
            }
        }
        return true;
    }

    bool matches(const ModelRecord* expected, std::uint64_t model_id, bool removing,
                 std::string& error)
    {
        const auto found = records.find(model_id);
        if (!expected)
        {
            if (found == records.end())
            {
                return true;
            }
            error = "Model identity is already in use";
            return false;
        }
        if (found == records.end() || found->second.bytes != expected->bytes ||
            mirror_json(found->second) != mirror_json(*expected) ||
            selection_json(found->second.selection) != selection_json(expected->selection) ||
            !node(found->second, error))
        {
            error = "Model changed outside this edit; undo or redo is stale";
            return false;
        }
        if (removing && (!removable(found->second, error) ||
                         visual_snapshot(*node(found->second, error)) != expected->visual))
        {
            if (error.empty())
            {
                error = "Model appearance changed; deletion undo or redo is stale";
            }
            return false;
        }
        return true;
    }

    bool replace(std::uint64_t model_id, const ModelRecord* replacement, std::string& error)
    {
        auto* active = scene(error);
        if (!active)
        {
            return false;
        }
        auto found = records.find(model_id);
        if (!replacement)
        {
            if (found == records.end() || !active->destroy_node(found->second.node_id))
            {
                error = "Cannot delete model node";
                return false;
            }
            records.erase(found);
            return true;
        }
        if (found != records.end())
        {
            auto* value = node(found->second, error);
            auto* renderer = value ? value->get_component<MeshRenderer>() : nullptr;
            if (!renderer)
            {
                error = "Cannot update model render mesh";
                return false;
            }
            const bool changed = found->second.bytes != replacement->bytes ||
                                 mirror_json(found->second) != mirror_json(*replacement);
            if (changed)
            {
                const auto old_mesh = *renderer->mesh_data();
                const auto old_visual = visual_snapshot(*value);
                const auto old_count = renderer->material_count();
                std::size_t desired = replacement->visual.is_null()
                                          ? old_count
                                          : replacement->visual["materials"].size();
                for (const auto& submesh : replacement->compiled.submeshes)
                {
                    desired =
                        std::max(desired, static_cast<std::size_t>(submesh.material_slot()) + 1);
                }
                bool success = renderer->set_mesh_data(replacement->compiled) &&
                               renderer->set_material_count(desired);
                if (success && !replacement->visual.is_null())
                {
                    for (std::size_t slot = old_count;
                         slot < replacement->visual["materials"].size() && success; ++slot)
                    {
                        success = restore_material(*renderer->material(slot),
                                                   replacement->visual["materials"][slot]);
                    }
                }
                if (!success)
                {
                    const bool restored =
                        renderer->set_mesh_data(old_mesh) && restore_visual(*value, old_visual);
                    found->second.mesh_revision = renderer->mesh_revision();
                    error = restored ? "Cannot update model render mesh or material slots"
                                     : "Cannot roll back model render mesh or material slots";
                    return false;
                }
            }
            auto updated = *replacement;
            updated.node_id = value->id();
            updated.mesh_revision = renderer->mesh_revision();
            found->second = std::move(updated);
            return true;
        }
        auto* value = active->create_node(replacement->name);
        auto* renderer = value ? value->add_component<MeshRenderer>() : nullptr;
        if (!renderer || !renderer->set_mesh_data(replacement->compiled) ||
            !restore_visual(*value, replacement->visual))
        {
            if (value)
            {
                const bool removed = active->destroy_node(value->id());
                if (!removed)
                {
                    error = "Cannot roll back failed model creation";
                    return false;
                }
            }
            error = "Cannot create model node or restore its resources";
            return false;
        }
        auto updated = *replacement;
        updated.node_id = value->id();
        updated.mesh_revision = renderer->mesh_revision();
        records.emplace(model_id, std::move(updated));
        return true;
    }

    class Edit final : public EditorEdit
    {
    public:
        State& owner;
        std::uint64_t scene_id;
        std::vector<ModelChange> changes;

        explicit Edit(State& state) : owner(state), scene_id(state.scene_id)
        {
        }

        bool apply(std::string& error) override
        {
            return change(true, error);
        }

        bool revert(std::string& error) override
        {
            return change(false, error);
        }

    private:
        bool change(bool forward, std::string& error)
        {
            if (owner.scene_id != scene_id || !owner.scene(error))
            {
                error = "Model edit belongs to an inactive scene";
                return false;
            }
            for (const auto& item : changes)
            {
                const auto* before = forward ? item.before.get() : item.after.get();
                const auto* after = forward ? item.after.get() : item.before.get();
                if (!owner.matches(before, item.model_id, !after, error))
                {
                    return false;
                }
            }
            for (std::size_t index = 0; index < changes.size(); ++index)
            {
                const auto& item = changes[index];
                const auto* after = forward ? item.after.get() : item.before.get();
                if (!owner.replace(item.model_id, after, error))
                {
                    for (std::size_t rollback = index; rollback != 0; --rollback)
                    {
                        const auto& previous = changes[rollback - 1];
                        const auto* restore =
                            forward ? previous.before.get() : previous.after.get();
                        std::string rollback_error;
                        if (!owner.replace(previous.model_id, restore, rollback_error))
                        {
                            error += "; rollback failed: " + rollback_error;
                        }
                    }
                    return false;
                }
            }
            // 节点重建后的状态用于之后的删除校验；模型身份保持不变。
            for (auto& item : changes)
            {
                auto* after = forward ? item.after.get() : item.before.get();
                if (after)
                {
                    auto& current = owner.records.at(item.model_id);
                    after->visual = visual_snapshot(*owner.node(current, error));
                }
            }
            return true;
        }
    };

    Json summary(const ModelRecord& record) const
    {
        return {{"modelId", std::to_string(record.model_id)},
                {"nodeId", std::to_string(record.node_id)},
                {"name", record.name},
                {"vertexCount", record.source.vertices.size()},
                {"faceCount", record.source.faces.size()},
                {"triangleCount", record.compiled.indices.size() / 3},
                {"selection", selection_json(record.selection)},
                {"mirror", mirror_json(record)}};
    }

    bool query(const Json& arguments, Json& result, std::string& error)
    {
        if (!fields(arguments, {"sceneId", "modelId", "nodeId", "offset", "limit"}))
        {
            error = "Unknown modeling_get field";
            return false;
        }
        result = {{"sceneId", std::to_string(scene_id)}};
        if (!arguments.contains("modelId"))
        {
            if (arguments.contains("nodeId") || arguments.contains("offset") ||
                arguments.contains("limit"))
            {
                error = "Topology paging requires modelId";
                return false;
            }
            Json models = Json::array();
            for (auto& [model_id, record] : records)
            {
                static_cast<void>(model_id);
                if (!node(record, error))
                {
                    return false;
                }
                models.push_back(summary(record));
            }
            result["models"] = std::move(models);
            result["capabilities"] = {{"stableModelIds", true},
                                      {"undoRedo", true},
                                      {"batch", true},
                                      {"bevelDomain", "closed-convex-mesh"},
                                      {"unwrapAlgorithm", "xatlas"},
                                      {"faceUnwrapAlgorithm", "individual-planar-islands"},
                                      {"nodeIdsMayChangeOnRestore", true}};
            return true;
        }
        auto* record = target(arguments, error);
        std::uint32_t offset = 0;
        std::uint32_t limit = 256;
        if (!record || !uint_field(arguments, "offset", offset) ||
            !uint_field(arguments, "limit", limit) || limit == 0 || limit > 1024)
        {
            if (error.empty())
            {
                error = "Topology page requires offset >= 0 and limit in [1, 1024]";
            }
            return false;
        }
        result["model"] = summary(*record);
        Json vertices = Json::array();
        Json faces = Json::array();
        const auto end = static_cast<std::size_t>(offset) + limit;
        for (std::size_t index = offset; index < record->source.vertices.size() && index < end;
             ++index)
        {
            const auto& vertex = record->source.vertices[index];
            vertices.push_back({{"id", std::to_string(vertex.id())},
                                {"position", {vertex.x(), vertex.y(), vertex.z()}}});
        }
        const auto loop_json = [](const auto& face)
        {
            Json vertices = Json::array();
            Json texcoords = Json::array();
            Json colors = Json::array();
            for (const auto value : face.vertices)
            {
                vertices.push_back(std::to_string(value));
            }
            for (const auto& value : face.texcoords)
            {
                texcoords.push_back({value.x(), value.y()});
            }
            for (const auto& value : face.colors)
            {
                colors.push_back({value.r(), value.g(), value.b(), value.a()});
            }
            return Json{{"vertices", std::move(vertices)},
                        {"texcoords", std::move(texcoords)},
                        {"colors", std::move(colors)}};
        };
        for (std::size_t index = offset; index < record->source.faces.size() && index < end;
             ++index)
        {
            const auto& face = *record->source.faces[index];
            auto item = loop_json(face);
            Json holes = Json::array();
            for (const auto& hole : face.holes)
            {
                holes.push_back(loop_json(*hole));
            }
            item.update({{"id", std::to_string(face.id)},
                         {"holes", std::move(holes)},
                         {"materialSlot", face.material_slot},
                         {"smoothingGroup", face.smoothing_group}});
            faces.push_back(std::move(item));
        }
        result["vertices"] = std::move(vertices);
        result["faces"] = std::move(faces);
        result["offset"] = offset;
        result["limit"] = limit;
        result["hasMore"] =
            end < record->source.vertices.size() || end < record->source.faces.size();
        return true;
    }

    bool create(const Json& arguments, Json& result, std::unique_ptr<EditorEdit>& output,
                std::string& error, bool loading)
    {
        if ((loading && !fields(arguments, {"sceneId", "path", "name"})) ||
            (!loading && !fields(arguments, {"sceneId", "shape", "parameters", "name"})))
        {
            error = "Unknown model creation field";
            return false;
        }
        auto record = std::make_unique<ModelRecord>();
        record->model_id = next_model_id;
        record->name = loading ? "Loaded model" : "Model";
        if (!name_field(arguments, record->name) || next_model_id == 0)
        {
            error = "Invalid model name or exhausted model identity space";
            return false;
        }
        if (loading)
        {
            fs::path path;
            if (!arguments.contains("path") || !arguments["path"].is_string() ||
                !safe_asset_path(project_root, arguments["path"].get<std::string>(), path, error) ||
                !load_source(path, record->source, error))
            {
                return false;
            }
        }
        else
        {
            const auto params = arguments.value("parameters", Json::object());
            if (!arguments.contains("shape") || !arguments["shape"].is_string() ||
                !params.is_object() ||
                !create_mesh(arguments["shape"].get<std::string>(), params, record->source, error))
            {
                return false;
            }
        }
        if (!prepare_record(*record, error))
        {
            return false;
        }
        auto edit = std::make_unique<Edit>(*this);
        result = {{"sceneId", std::to_string(scene_id)},
                  {"modelId", std::to_string(record->model_id)}};
        edit->changes.push_back({record->model_id, nullptr, std::move(record)});
        ++next_model_id;
        output = std::move(edit);
        return true;
    }

    bool change_model(const Json& arguments, Json& result, std::unique_ptr<EditorEdit>& output,
                      std::string& error, bool selecting)
    {
        if ((selecting &&
             !fields(arguments, {"sceneId", "modelId", "nodeId", "selection", "mode"})) ||
            (!selecting && !fields(arguments, {"sceneId", "modelId", "nodeId", "selection",
                                               "operation", "parameters"})))
        {
            error = "Unknown model edit field";
            return false;
        }
        auto* record = target(arguments, error);
        if (!record)
        {
            return false;
        }
        auto before = snapshot(*record, error);
        if (!before)
        {
            return false;
        }
        auto after = std::make_unique<ModelRecord>(*before);
        if (arguments.contains("selection"))
        {
            after->selection = {};
            if (!read_selection(arguments["selection"], after->source, after->selection, error))
            {
                return false;
            }
        }
        else if (selecting && arguments.value("mode", Json("replace")) == "replace")
        {
            error = "Replacing selection requires a selection object";
            return false;
        }
        if (selecting)
        {
            const auto mode = arguments.value("mode", Json("replace"));
            if (mode == "grow" || mode == "shrink" || mode == "connected")
            {
                const auto kind = mode == "grow"     ? MeshFaceSelection::grow
                                  : mode == "shrink" ? MeshFaceSelection::shrink
                                                     : MeshFaceSelection::connected;
                auto faces = face_selection(after->source, after->selection);
                std::vector<std::uint64_t> selected;
                if (!MeshTopology::select_faces(after->source, faces, kind, selected, error))
                {
                    return false;
                }
                after->selection = {};
                after->selection.faces = std::move(selected);
            }
            else if (mode == "edge_ring" || mode == "edge_loop")
            {
                std::vector<MeshEdge> selected;
                if (after->selection.edges.size() != 1 ||
                    !(mode == "edge_ring"
                          ? MeshTopology::edge_ring(after->source, after->selection.edges[0],
                                                    selected, error)
                          : MeshTopology::edge_loop(after->source, after->selection.edges[0],
                                                    selected, error)))
                {
                    if (error.empty())
                    {
                        error = "Edge loop/ring requires exactly one seed edge";
                    }
                    return false;
                }
                after->selection = {};
                after->selection.edges = std::move(selected);
            }
            else if (mode != "replace")
            {
                error = "Unknown selection mode";
                return false;
            }
        }
        if (!selecting)
        {
            const auto parameters = arguments.value("parameters", Json::object());
            if (!arguments.contains("operation") || !arguments["operation"].is_string() ||
                !parameters.is_object())
            {
                error = "Model edit requires an operation string and parameter object";
                return false;
            }
            const auto operation = arguments["operation"].get<std::string>();
            if (operation == "mirror_mode")
            {
                const auto action = parameters.value("action", Json("enable"));
                if (!fields(parameters, {"action", "normal", "offset", "weld", "tolerance"}) ||
                    !vec3_field(parameters, "normal", after->mirror.normal) ||
                    !number_field(parameters, "offset", after->mirror.offset) ||
                    !number_field(parameters, "tolerance", after->mirror.tolerance) ||
                    !boolean_field(parameters, "weld", after->mirror.weld))
                {
                    error = "Invalid mirror mode options";
                    return false;
                }
                if (action == "enable")
                {
                    after->mirror.require_half_space = true;
                    after->mirror_enabled = true;
                }
                else if (action == "cancel")
                {
                    after->mirror_enabled = false;
                }
                else if (action == "finish" && after->mirror_enabled)
                {
                    if (!MeshGeometry::mirror(after->source, after->mirror, error))
                    {
                        return false;
                    }
                    after->mirror_enabled = false;
                }
                else
                {
                    error = "Mirror action requires enable, cancel or finish on an active mode";
                    return false;
                }
            }
            else if (operation == "boolean")
            {
                std::uint64_t other_id = 0;
                if (!fields(parameters, {"otherModelId", "mode"}) ||
                    !id_field(parameters, "otherModelId", other_id) || other_id == record->model_id)
                {
                    error = "Boolean operation requires a different otherModelId";
                    return false;
                }
                const auto other = records.find(other_id);
                if (other == records.end() || !node(other->second, error))
                {
                    error = "Boolean operand model does not exist";
                    return false;
                }
                auto other_source = other->second.source;
                if ((other->second.mirror_enabled &&
                     !MeshGeometry::mirror(other_source, other->second.mirror, error)) ||
                    (after->mirror_enabled &&
                     !MeshGeometry::mirror(after->source, after->mirror, error)))
                {
                    return false;
                }
                after->mirror_enabled = false;
                std::array<double, 16> relative{};
                if (!relative_matrix(*node(*record, error), *node(other->second, error), relative))
                {
                    error = "Boolean operands require finite invertible scene transforms";
                    return false;
                }
                MeshSelection all;
                all.all = true;
                if (!MeshTopology::transform(other_source, all, relative, error))
                {
                    return false;
                }
                const auto other_visual = visual_snapshot(*node(other->second, error));
                const auto slot_offset =
                    static_cast<std::uint32_t>(after->visual["materials"].size());
                if (slot_offset + other_visual["materials"].size() > 16)
                {
                    error = "Boolean operands exceed 16 combined material slots";
                    return false;
                }
                for (const auto& material : other_visual["materials"])
                {
                    after->visual["materials"].push_back(material);
                }
                std::map<std::uint32_t, std::vector<std::uint64_t>> slots;
                for (const auto& face : other_source.faces)
                {
                    slots[face->material_slot].push_back(face->id);
                }
                for (const auto& [slot, faces] : slots)
                {
                    if (!MeshTopology::set_material(other_source, faces, slot + slot_offset, error))
                    {
                        return false;
                    }
                }
                const auto mode = parameters.value("mode", Json("subtract"));
                const auto kind = mode == "union"       ? MeshBooleanOperation::unite
                                  : mode == "intersect" ? MeshBooleanOperation::intersect
                                                        : MeshBooleanOperation::subtract;
                if ((mode != "union" && mode != "intersect" && mode != "subtract") ||
                    !MeshGeometry::boolean_operation(after->source, other_source, kind, error))
                {
                    return false;
                }
            }
            else if (!operate(operation, parameters, after->selection, after->source, error))
            {
                if (error.empty())
                {
                    error = "Invalid parameters or selection for " + operation;
                }
                return false;
            }
            after->selection = {};
            if (!prepare_record(*after, error))
            {
                return false;
            }
        }
        auto edit = std::make_unique<Edit>(*this);
        result = {{"sceneId", std::to_string(scene_id)},
                  {"modelId", std::to_string(record->model_id)}};
        edit->changes.push_back({record->model_id, std::move(before), std::move(after)});
        output = std::move(edit);
        return true;
    }

    bool remove(const Json& arguments, Json& result, std::unique_ptr<EditorEdit>& output,
                std::string& error)
    {
        if (!fields(arguments, {"sceneId", "modelId", "nodeId"}))
        {
            return false;
        }
        auto* record = target(arguments, error);
        if (!record || !removable(*record, error))
        {
            return false;
        }
        auto before = snapshot(*record, error);
        if (!before)
        {
            return false;
        }
        auto edit = std::make_unique<Edit>(*this);
        result = {{"sceneId", std::to_string(scene_id)},
                  {"modelId", std::to_string(record->model_id)}};
        edit->changes.push_back({record->model_id, std::move(before), nullptr});
        output = std::move(edit);
        return true;
    }

    bool duplicate(const Json& arguments, Json& result, std::unique_ptr<EditorEdit>& output,
                   std::string& error, bool separate)
    {
        if (!fields(arguments, {"sceneId", "modelId", "nodeId", "name", "selection"}) ||
            (!separate && arguments.contains("selection")))
        {
            return false;
        }
        auto* original = target(arguments, error);
        if (!original)
        {
            return false;
        }
        auto copy = snapshot(*original, error);
        if (!copy)
        {
            return false;
        }
        copy->model_id = next_model_id;
        copy->name += separate ? " separated" : " copy";
        copy->node_id = 0;
        if (!name_field(arguments, copy->name) || next_model_id == 0)
        {
            error = "Invalid model name or exhausted model identity space";
            return false;
        }
        auto edit = std::make_unique<Edit>(*this);
        if (separate)
        {
            auto selection = original->selection;
            if (arguments.contains("selection"))
            {
                selection = {};
                if (!read_selection(arguments["selection"], original->source, selection, error))
                {
                    return false;
                }
            }
            const auto selected = face_selection(original->source, selection);
            if (selected.empty() || selected.size() == original->source.faces.size())
            {
                error = "Separate requires a nonempty proper subset of faces";
                return false;
            }
            std::vector<std::uint64_t> remainder;
            for (const auto& face : original->source.faces)
            {
                if (std::find(selected.begin(), selected.end(), face->id) == selected.end())
                {
                    remainder.push_back(face->id);
                }
            }
            auto before = snapshot(*original, error);
            auto after = std::make_unique<ModelRecord>(*before);
            if (!MeshTopology::erase_faces(copy->source, remainder, error) ||
                !MeshTopology::erase_faces(after->source, selected, error))
            {
                return false;
            }
            after->selection = {};
            if (!prepare_record(*after, error))
            {
                return false;
            }
            edit->changes.push_back({original->model_id, std::move(before), std::move(after)});
        }
        copy->selection = {};
        auto* original_node = node(*original, error);
        if (!original_node)
        {
            return false;
        }
        if (original_node->parent())
        {
            std::array<float, 16> world_matrix{};
            std::array<double, 16> matrix{};
            if (!original_node->get_component<Transform>()->world_matrix(world_matrix.data()) ||
                (copy->mirror_enabled && !MeshGeometry::mirror(copy->source, copy->mirror, error)))
            {
                error = "Cannot bake parented model transform";
                return false;
            }
            std::copy(world_matrix.begin(), world_matrix.end(), matrix.begin());
            MeshSelection all;
            all.all = true;
            if (!MeshTopology::transform(copy->source, all, matrix, error))
            {
                return false;
            }
            copy->mirror_enabled = false;
            copy->visual["position"] = {0, 0, 0};
            copy->visual["rotation"] = {0, 0, 0};
            copy->visual["scale"] = {1, 1, 1};
        }
        if (!prepare_record(*copy, error))
        {
            return false;
        }
        result = {{"sceneId", std::to_string(scene_id)},
                  {"modelId", std::to_string(copy->model_id)}};
        edit->changes.push_back({copy->model_id, nullptr, std::move(copy)});
        ++next_model_id;
        output = std::move(edit);
        return true;
    }

    bool combine(const Json& arguments, Json& result, std::unique_ptr<EditorEdit>& output,
                 std::string& error)
    {
        std::vector<std::uint64_t> model_ids;
        if (!fields(arguments, {"sceneId", "modelIds", "name"}) ||
            !arguments.contains("modelIds") || !ids(arguments["modelIds"], model_ids) ||
            model_ids.size() < 2 || model_ids.size() > 64 || next_model_id == 0)
        {
            error = "Combine requires 2..64 distinct model IDs";
            return false;
        }
        auto combined = std::make_unique<ModelRecord>();
        combined->model_id = next_model_id;
        combined->name = "Combined model";
        if (!name_field(arguments, combined->name))
        {
            return false;
        }
        Json materials = Json::array();
        for (const auto model_id : model_ids)
        {
            const auto found = records.find(model_id);
            auto* value = found == records.end() ? nullptr : node(found->second, error);
            if (!value)
            {
                error = "Combine operand no longer exists";
                return false;
            }
            auto source = found->second.source;
            if (found->second.mirror_enabled &&
                !MeshGeometry::mirror(source, found->second.mirror, error))
            {
                return false;
            }
            std::array<float, 16> world_matrix{};
            std::array<double, 16> matrix{};
            if (!value->get_component<Transform>()->world_matrix(world_matrix.data()))
            {
                error = "Cannot evaluate combine operand transform";
                return false;
            }
            std::copy(world_matrix.begin(), world_matrix.end(), matrix.begin());
            MeshSelection all;
            all.all = true;
            if (!MeshTopology::transform(source, all, matrix, error))
            {
                return false;
            }
            const auto visual = visual_snapshot(*value);
            const auto offset = static_cast<std::uint32_t>(materials.size());
            if (materials.size() + visual["materials"].size() > 16)
            {
                error = "Combined model exceeds 16 material slots";
                return false;
            }
            for (const auto& material : visual["materials"])
            {
                materials.push_back(material);
            }
            if (!MeshTopology::append(combined->source, source, error, offset))
            {
                return false;
            }
        }
        combined->visual = {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0}},
                            {"scale", {1, 1, 1}},    {"materials", std::move(materials)},
                            {"enabled", true},       {"transformEnabled", true}};
        if (!prepare_record(*combined, error))
        {
            return false;
        }
        auto edit = std::make_unique<Edit>(*this);
        result = {{"sceneId", std::to_string(scene_id)},
                  {"modelId", std::to_string(combined->model_id)}};
        edit->changes.push_back({combined->model_id, nullptr, std::move(combined)});
        ++next_model_id;
        output = std::move(edit);
        return true;
    }

    bool save(const Json& arguments, Json& result, std::string& error)
    {
        fs::path path;
        if (!fields(arguments, {"sceneId", "modelId", "nodeId", "path"}) ||
            !arguments.contains("path") || !arguments["path"].is_string() ||
            !safe_asset_path(project_root, arguments["path"].get<std::string>(), path, error))
        {
            return false;
        }
        auto* record = target(arguments, error);
        if (!record)
        {
            return false;
        }
        auto saved_source = record->source;
        if ((record->mirror_enabled &&
             !MeshGeometry::mirror(saved_source, record->mirror, error)) ||
            !save_source(path, saved_source, record->compiled, error))
        {
            return false;
        }
        result = {{"sceneId", std::to_string(scene_id)},
                  {"modelId", std::to_string(record->model_id)},
                  {"path", "project/" + arguments["path"].get<std::string>()},
                  {"importRequired", true},
                  {"mirrorBaked", record->mirror_enabled}};
        return true;
    }

    bool execute(std::string_view command, const Json& arguments, Json& result,
                 std::unique_ptr<EditorEdit>& edit, std::string& error)
    {
        if (command == "modeling_help")
        {
            return modeling_help(arguments, result, error);
        }
        if (!enter(arguments, error))
        {
            return false;
        }
        if (command == "modeling_create" || command == "modeling_load")
        {
            return create(arguments, result, edit, error, command == "modeling_load");
        }
        if (command == "modeling_get")
        {
            return query(arguments, result, error);
        }
        if (command == "modeling_select" || command == "modeling_edit")
        {
            return change_model(arguments, result, edit, error, command == "modeling_select");
        }
        if (command == "modeling_delete")
        {
            return remove(arguments, result, edit, error);
        }
        if (command == "modeling_duplicate" || command == "modeling_separate")
        {
            return duplicate(arguments, result, edit, error, command == "modeling_separate");
        }
        if (command == "modeling_combine")
        {
            return combine(arguments, result, edit, error);
        }
        if (command == "modeling_save")
        {
            return save(arguments, result, error);
        }
        error = "Unknown modeling command";
        return false;
    }
};

ModelingCommands::ModelingCommands(WorldSystem& world, fs::path project_root)
    : state_(std::make_unique<State>(world, std::move(project_root)))
{
}

ModelingCommands::~ModelingCommands() noexcept = default;

void ModelingCommands::synchronize_scene() noexcept
{
    state_->synchronize_scene();
}

bool ModelingCommands::export_mesh(const Node& node, const fs::path& path, std::string& error)
{
    state_->synchronize_scene();
    if (!state_->world.scene() || &node.scene() != state_->world.scene())
    {
        error = "Mesh export requires a node in the active scene";
        return false;
    }
    const auto* renderer = node.get_component<MeshRenderer>();
    if (!renderer || !renderer->mesh_data())
    {
        error = "Node has no mesh data to save";
        return false;
    }
    schema::EditableMeshDataT source;
    bool found = false;
    for (const auto& [id, record] : state_->records)
    {
        if (record.node_id != node.id())
        {
            continue;
        }
        if (record.mesh_revision != renderer->mesh_revision())
        {
            error = "Model mesh changed outside modeling commands";
            return false;
        }
        source = record.source;
        if (record.mirror_enabled && !MeshGeometry::mirror(source, record.mirror, error))
        {
            return false;
        }
        found = true;
        break;
    }
    if (!found && !EditableMesh::from_mesh(*renderer->mesh_data(), source, error))
    {
        return false;
    }
    return save_source(path, source, *renderer->mesh_data(), error);
}

bool ModelingCommands::register_commands(EditorCommands& commands)
{
    for (const char* name :
         {"modeling_create", "modeling_get", "modeling_select", "modeling_edit", "modeling_delete",
          "modeling_duplicate", "modeling_combine", "modeling_separate", "modeling_save",
          "modeling_load", "modeling_help"})
    {
        const std::string command = name;
        const bool undoable =
            command != "modeling_get" && command != "modeling_save" && command != "modeling_help";
        if (!commands.register_command(
                command, undoable,
                [this, command](const Json& arguments, Json& result,
                                std::unique_ptr<EditorEdit>& edit, std::string& error)
                { return state_->execute(command, arguments, result, edit, error); }))
        {
            return false;
        }
    }
    return true;
}

} // namespace engine
