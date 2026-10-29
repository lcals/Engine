#include "plugins/editor/fbx_import/fbx_document.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <span>

namespace engine::plugins::fbx_import
{
namespace
{
using namespace editor::asset_pipeline;

std::string safe_name(ufbx_string name)
{
    constexpr char hex[] = "0123456789abcdef";
    std::string output;
    for (std::size_t index = 0; index < name.length; ++index)
    {
        const auto value = static_cast<unsigned char>(name.data[index]);
        if (value < 32 || value == 127 || value == '/' || value == '\\' || value == ':' ||
            value == '%')
        {
            output += '%';
            output += hex[value >> 4];
            output += hex[value & 15];
        }
        else
        {
            output += static_cast<char>(value);
        }
    }
    if (output.empty())
    {
        output = "unnamed";
    }
    if (output == "." || output == "..")
    {
        output = "%2e" + output.substr(1);
    }
    return output;
}

bool collect_nodes(FbxDocument& document, const ufbx_node& node, const std::string& parent,
                   std::string& error)
{
    const auto path = parent.empty() ? "root" : parent + "/" + safe_name(node.name);
    if (!document.nodes.emplace(path, &node).second)
    {
        error = "FBX has ambiguous sibling node names: " + path;
        return false;
    }
    document.node_paths.emplace(&node, path);
    if (node.mesh != nullptr)
    {
        if (node.mesh->skin_deformers.count > 1 || node.mesh->blend_deformers.count != 0 ||
            node.mesh->cache_deformers.count != 0)
        {
            error = "FBX mesh requires unsupported multiple skin, blend shape or cache deformers";
            return false;
        }
        if (node.mesh->skin_deformers.count != 0 &&
            node.mesh->skin_deformers.data[0]->skinning_method != UFBX_SKINNING_METHOD_LINEAR &&
            node.mesh->skin_deformers.data[0]->skinning_method != UFBX_SKINNING_METHOD_RIGID)
        {
            error = "FBX mesh requires unsupported dual quaternion skinning";
            return false;
        }
        document.meshes.emplace("meshes/" + path, &node);
    }
    for (const auto* child : node.children)
    {
        if (!collect_nodes(document, *child, path, error))
        {
            return false;
        }
    }
    return true;
}

void collect_joints(FbxDocument& document, const ufbx_node& node,
                    const std::set<const ufbx_node*>& included)
{
    if (included.contains(&node))
    {
        document.joint_indices.emplace(&node, static_cast<std::uint32_t>(document.joints.size()));
        document.joints.push_back(&node);
    }
    for (const auto* child : node.children)
    {
        collect_joints(document, *child, included);
    }
}

bool matrix_close(const ufbx_matrix& left, const ufbx_matrix& right)
{
    for (std::size_t index = 0; index < 12; ++index)
    {
        if (std::abs(left.v[index] - right.v[index]) >
            0.00001 * std::max({1.0, std::abs(left.v[index]), std::abs(right.v[index])}))
        {
            return false;
        }
    }
    return true;
}

schema::Vec3 vector(ufbx_vec3 value)
{
    return {value.x, value.y, value.z};
}

schema::Vec4 rotation(ufbx_quat value)
{
    return {value.x, value.y, value.z, value.w};
}

bool skin_influence(const FbxDocument& document, const ufbx_skin_deformer& skin,
                    std::uint32_t vertex, schema::SkinInfluence& output, std::string& error)
{
    if (vertex >= skin.vertices.count)
    {
        error = "FBX skin has no weights for a mesh control point";
        return false;
    }
    const auto& influence = skin.vertices.data[vertex];
    const auto count = std::min<std::size_t>(4, influence.num_weights);
    std::array<std::uint16_t, 4> joints{};
    std::array<float, 4> weights{};
    double total = 0.0;
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto& weight = skin.weights.data[influence.weight_begin + index];
        if (weight.cluster_index >= skin.clusters.count || !std::isfinite(weight.weight) ||
            weight.weight < 0.0)
        {
            error = "Invalid FBX skin cluster or weight";
            return false;
        }
        const auto* bone = skin.clusters.data[weight.cluster_index]->bone_node;
        const auto found = document.joint_indices.find(bone);
        if (found == document.joint_indices.end() ||
            found->second > std::numeric_limits<std::uint16_t>::max())
        {
            error = "FBX skin references a missing joint";
            return false;
        }
        joints[index] = static_cast<std::uint16_t>(found->second);
        weights[index] = static_cast<float>(weight.weight);
        total += weight.weight;
    }
    if (!std::isfinite(total) || total <= 0.000001)
    {
        error = "FBX skin control point has no positive weights";
        return false;
    }
    for (auto& weight : weights)
    {
        weight = static_cast<float>(weight / total);
    }
    output = {joints[0],  joints[1],  joints[2],  joints[3],
              weights[0], weights[1], weights[2], weights[3]};
    return true;
}
} // namespace

void SceneDeleter::operator()(ufbx_scene* scene) const noexcept
{
    ufbx_free_scene(scene);
}

std::vector<float> matrix_values(const ufbx_matrix& matrix)
{
    std::vector<float> result(16, 0.0F);
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 3; ++row)
        {
            result[column * 4 + row] = static_cast<float>(matrix.cols[column].v[row]);
        }
    }
    result[15] = 1.0F;
    return result;
}

bool load_document(const std::filesystem::path& path, FbxDocument& document, std::string& error)
{
    document = {};
    auto meta = path;
    meta += ".meta";
    toml::table settings;
    if (!read_table(meta, settings, error))
    {
        return false;
    }
    if ((settings.contains("sample_rate") && !settings["sample_rate"].is_number()) ||
        (settings.contains("skeleton") && !settings["skeleton"].is_string()))
    {
        error = "FBX sample_rate must be a number and skeleton must be a string";
        return false;
    }
    document.sample_rate = settings["sample_rate"].value_or(30.0);
    document.external_skeleton = settings["skeleton"].value_or(std::string{});
    const auto normals_mode = settings["normals"].value<std::string>();
    if ((settings.contains("normals") && !normals_mode) ||
        (normals_mode && *normals_mode != "import" && *normals_mode != "generate"))
    {
        error = "FBX normals must be import or generate";
        return false;
    }
    document.normals_mode = normals_mode.value_or("import");
    if (!std::isfinite(document.sample_rate) || document.sample_rate < 1.0 ||
        document.sample_rate > 240.0)
    {
        error = "FBX sample_rate must be between 1 and 240";
        return false;
    }
    std::vector<std::byte> bytes;
    if (!ResourceSystem::read_binary(path, bytes))
    {
        error = "Cannot read FBX source: " + PathUtils::to_utf8(path);
        return false;
    }
    document.source_hash = hash_bytes(bytes);
    ufbx_load_opts options{};
    options.load_external_files = false;
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0;
    options.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    options.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
    options.generate_missing_normals = true;
    ufbx_error load_error{};
    document.scene.reset(ufbx_load_memory(bytes.data(), bytes.size(), &options, &load_error));
    if (!document.scene)
    {
        std::array<char, 2048> message{};
        ufbx_format_error(message.data(), message.size(), &load_error);
        error = "Cannot parse FBX: " + std::string(message.data());
        return false;
    }
    if (!collect_nodes(document, *document.scene->root_node, {}, error))
    {
        return false;
    }
    if (!document.external_skeleton.empty() && !document.meshes.empty())
    {
        error = "External skeleton mapping is supported for animation-only FBX sources";
        return false;
    }
    std::set<const ufbx_node*> included;
    const auto include = [&](const ufbx_node* node)
    {
        while (node != nullptr)
        {
            included.insert(node);
            node = node->parent;
        }
    };
    for (const auto* node : document.scene->nodes)
    {
        if (node->bone != nullptr)
        {
            include(node);
        }
    }
    for (const auto* cluster : document.scene->skin_clusters)
    {
        include(cluster->bone_node);
    }
    collect_joints(document, *document.scene->root_node, included);
    if (document.joints.size() > 1024)
    {
        error = "FBX skeleton exceeds the supported 1024 joints";
        return false;
    }
    for (const auto* stack : document.scene->anim_stacks)
    {
        if (stack->time_end <= stack->time_begin || document.joints.empty())
        {
            continue;
        }
        const auto key = "animations/" + safe_name(stack->name);
        if (!document.animations.emplace(key, stack).second)
        {
            error = "FBX has ambiguous animation names: " + key;
            return false;
        }
    }
    return true;
}

bool convert_skeleton(const FbxDocument& document, schema::SkeletonDataT& output,
                      std::string& error)
{
    std::map<const ufbx_node*, ufbx_matrix> bindings;
    for (const auto& [key, node] : document.meshes)
    {
        (void)key;
        const auto inverse_mesh = ufbx_matrix_invert(&node->geometry_to_world);
        for (const auto* skin : node->mesh->skin_deformers)
        {
            for (const auto* cluster : skin->clusters)
            {
                const auto inverse_bind =
                    ufbx_matrix_mul(&cluster->geometry_to_bone, &inverse_mesh);
                const auto [found, inserted] = bindings.emplace(cluster->bone_node, inverse_bind);
                if (!inserted && !matrix_close(found->second, inverse_bind))
                {
                    error = "FBX meshes have incompatible bind poses for a shared skeleton";
                    return false;
                }
            }
        }
    }
    for (const auto* node : document.joints)
    {
        auto joint = std::make_unique<schema::JointDataT>();
        joint->name = node->is_root ? "root" : safe_name(node->name);
        if (node->parent != nullptr)
        {
            const auto parent = document.joint_indices.find(node->parent);
            if (parent == document.joint_indices.end())
            {
                error = "FBX skeleton has an omitted parent transform";
                return false;
            }
            joint->parent = static_cast<std::int32_t>(parent->second);
        }
        joint->translation = vector(node->local_transform.translation);
        joint->rotation = rotation(node->local_transform.rotation);
        joint->scale = vector(node->local_transform.scale);
        const auto bind = bindings.find(node);
        joint->inverse_bind = matrix_values(
            bind != bindings.end() ? bind->second : ufbx_matrix_invert(&node->node_to_world));
        output.joints.push_back(std::move(joint));
    }
    return !output.joints.empty();
}

bool convert_mesh(const FbxDocument& document, const ufbx_node& node, schema::MeshDataT& output,
                  std::string& error)
{
    const auto& mesh = *node.mesh;
    if (mesh.num_triangles == 0 ||
        mesh.num_triangles > std::numeric_limits<std::uint32_t>::max() / 3 ||
        node.materials.count > 16)
    {
        error = "FBX mesh has no triangles, exceeds index limits or uses more than 16 materials";
        return false;
    }
    const auto* skin = mesh.skin_deformers.count != 0 ? mesh.skin_deformers.data[0] : nullptr;
    if (skin != nullptr)
    {
        output.mesh_to_skeleton = matrix_values(node.geometry_to_world);
    }
    std::vector<std::uint32_t> normal_indices;
    std::vector<ufbx_vec3> generated_normals;
    if (document.normals_mode == "generate")
    {
        std::vector<ufbx_topo_edge> topology(mesh.num_indices);
        normal_indices.resize(mesh.num_indices);
        ufbx_panic panic{};
        ufbx_catch_compute_topology(&panic, &mesh, topology.data(), topology.size());
        if (panic.did_panic)
        {
            error = "Cannot build FBX topology for normals";
            return false;
        }
        const auto normal_count =
            ufbx_catch_generate_normal_mapping(&panic, &mesh, topology.data(), topology.size(),
                                               normal_indices.data(), normal_indices.size(), true);
        if (panic.did_panic || normal_count == 0 || normal_count > mesh.num_indices)
        {
            error = "Cannot map FBX normals from smoothing groups";
            return false;
        }
        generated_normals.resize(normal_count);
        ufbx_catch_compute_normals(&panic, &mesh, &mesh.vertex_position, normal_indices.data(),
                                   normal_indices.size(), generated_normals.data(),
                                   generated_normals.size());
        if (panic.did_panic)
        {
            error = "Cannot generate FBX normals";
            return false;
        }
    }
    std::map<std::uint32_t, std::uint32_t> corners;
    std::map<std::uint32_t, std::vector<std::uint32_t>> parts;
    std::vector<std::uint32_t> triangles(mesh.max_face_triangles * 3);
    for (std::size_t face_index = 0; face_index < mesh.faces.count; ++face_index)
    {
        const auto face = mesh.faces.data[face_index];
        if (face.num_indices < 3)
        {
            continue;
        }
        const auto count = ufbx_triangulate_face(triangles.data(), triangles.size(), &mesh, face);
        if (count == 0)
        {
            error = "FBX face triangulation failed";
            return false;
        }
        const auto material =
            mesh.face_material.count != 0 ? mesh.face_material.data[face_index] : 0;
        if (material > 15)
        {
            error = "FBX material index exceeds the runtime material slots";
            return false;
        }
        auto& indices = parts[material];
        for (std::size_t index = 0; index < count * 3; ++index)
        {
            const auto corner = triangles[index];
            const auto [found, inserted] =
                corners.emplace(corner, static_cast<std::uint32_t>(output.vertices.size()));
            if (inserted)
            {
                const auto position = ufbx_get_vertex_vec3(&mesh.vertex_position, corner);
                auto normal = generated_normals.empty()
                                  ? ufbx_get_vertex_vec3(&mesh.vertex_normal, corner)
                                  : generated_normals[normal_indices[corner]];
                const auto length =
                    std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                if (!std::isfinite(length) || length <= 0.000001)
                {
                    error = "FBX mesh has an invalid vertex normal";
                    return false;
                }
                normal.x /= length;
                normal.y /= length;
                normal.z /= length;
                output.vertices.emplace_back(position.x, position.y, position.z, normal.x, normal.y,
                                             normal.z);
                if (mesh.vertex_uv.exists)
                {
                    const auto uv = ufbx_get_vertex_vec2(&mesh.vertex_uv, corner);
                    output.texcoords.emplace_back(uv.x, uv.y);
                }
                if (mesh.vertex_color.exists)
                {
                    const auto color = ufbx_get_vertex_vec4(&mesh.vertex_color, corner);
                    output.colors.emplace_back(color.x, color.y, color.z, color.w);
                }
                if (skin != nullptr)
                {
                    schema::SkinInfluence influence;
                    if (!skin_influence(document, *skin, mesh.vertex_indices.data[corner],
                                        influence, error))
                    {
                        return false;
                    }
                    output.skin.push_back(influence);
                }
            }
            indices.push_back(found->second);
        }
    }
    for (const auto& [material, indices] : parts)
    {
        output.submeshes.emplace_back(static_cast<std::uint32_t>(output.indices.size()),
                                      static_cast<std::uint32_t>(indices.size()), material);
        output.indices.insert(output.indices.end(), indices.begin(), indices.end());
    }
    return !output.indices.empty();
}

bool convert_animation(const FbxDocument& source, const FbxDocument& skeleton,
                       const ufbx_anim_stack& stack, schema::AnimationDataT& output,
                       std::string& error)
{
    const auto duration = stack.time_end - stack.time_begin;
    if (!std::isfinite(duration) || duration <= 0 || duration * source.sample_rate > 1000000.0)
    {
        error = "FBX animation duration is invalid or exceeds the sampling limit";
        return false;
    }
    output.duration = static_cast<float>(duration);
    output.name.assign(stack.name.data, stack.name.length);
    if (output.name.empty())
    {
        output.name = "unnamed";
    }
    const auto frames = static_cast<std::size_t>(std::ceil(duration * source.sample_rate));
    std::set<const ufbx_node*> required;
    for (const auto* cluster : skeleton.scene->skin_clusters)
    {
        for (auto* node = cluster->bone_node; node != nullptr; node = node->parent)
        {
            required.insert(node);
        }
    }
    for (std::size_t index = 0; index < skeleton.joints.size(); ++index)
    {
        const auto* target = skeleton.joints[index];
        const auto found = source.nodes.find(skeleton.node_paths.at(target));
        if (found == source.nodes.end())
        {
            if (required.contains(target))
            {
                error = "FBX animation is missing a weighted joint or ancestor: " +
                        skeleton.node_paths.at(target);
                return false;
            }
            continue;
        }
        auto track = std::make_unique<schema::AnimationTrackDataT>();
        track->joint = static_cast<std::uint32_t>(index);
        float previous = -1.0F;
        for (std::size_t frame = 0; frame <= frames; ++frame)
        {
            const auto seconds =
                std::min(duration, static_cast<double>(frame) / source.sample_rate);
            const auto time = static_cast<float>(seconds);
            if (time <= previous)
            {
                error = "FBX animation sample times exceed float precision";
                return false;
            }
            previous = time;
            const auto pose =
                ufbx_evaluate_transform(stack.anim, found->second, stack.time_begin + seconds);
            track->translations.emplace_back(time, vector(pose.translation));
            track->rotations.emplace_back(time, rotation(pose.rotation));
            track->scales.emplace_back(time, vector(pose.scale));
        }
        output.tracks.push_back(std::move(track));
    }
    return !output.tracks.empty();
}
} // namespace engine::plugins::fbx_import
