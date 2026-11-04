#include "plugins/editor/asset_import/fbx_document.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{

ufbx_matrix bind_pose(double unit, double angle, double displacement, double scale)
{
    auto transform = ufbx_identity_transform;
    transform.translation.x = 0.087 + displacement;
    transform.translation.y = 1.032;
    transform.translation.z = 0.008;
    transform.rotation.z = std::sin(angle * 0.5);
    transform.rotation.w = std::cos(angle * 0.5);
    transform.scale.x = unit * scale;
    transform.scale.y = unit * scale;
    transform.scale.z = unit * scale;
    return ufbx_transform_to_matrix(&transform);
}

bool converts(const ufbx_matrix& first, const ufbx_matrix& second, double unit)
{
    using namespace engine::plugins::asset_import;
    FbxDocument document;
    ufbx_node bone{};
    bone.is_root = true;
    bone.local_transform = ufbx_identity_transform;
    bone.node_to_world = ufbx_identity_matrix;
    document.joints.push_back(&bone);
    document.joint_indices.emplace(&bone, 0);
    std::array<ufbx_node, 2> nodes{};
    std::array<ufbx_mesh, 2> meshes{};
    std::array<ufbx_skin_deformer, 2> skins{};
    std::array<ufbx_skin_cluster, 2> clusters{};
    std::array<ufbx_skin_cluster*, 2> cluster_pointers{&clusters[0], &clusters[1]};
    std::array<ufbx_skin_deformer*, 2> skin_pointers{&skins[0], &skins[1]};
    const std::array bindings{first, second};
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        nodes[index].mesh = &meshes[index];
        nodes[index].geometry_to_world = ufbx_identity_matrix;
        nodes[index].geometry_to_world.m00 = unit;
        nodes[index].geometry_to_world.m11 = unit;
        nodes[index].geometry_to_world.m22 = unit;
        clusters[index].bone_node = &bone;
        clusters[index].geometry_to_bone =
            ufbx_matrix_mul(&bindings[index], &nodes[index].geometry_to_world);
        skins[index].clusters = {&cluster_pointers[index], 1};
        meshes[index].skin_deformers = {&skin_pointers[index], 1};
        document.meshes.emplace(std::to_string(index), &nodes[index]);
    }
    engine::schema::SkeletonDataT output;
    std::string error;
    const bool success = convert_skeleton(document, output, error);
    return success && error.empty() && output.joints.size() == 1;
}

bool unit_independent_bind_comparison()
{
    for (const double unit : {1.0, 0.01, 0.0001, 100.0})
    {
        const auto reference_pose = bind_pose(unit, 1.0, 0.0, 1.0);
        const auto reference = ufbx_matrix_invert(&reference_pose);
        const auto approximate_pose = bind_pose(unit, 1.0 + 0.0000005, 0.0000002, 1.0000001);
        const auto approximate = ufbx_matrix_invert(&approximate_pose);
        if (!converts(reference, approximate, unit) || !converts(approximate, reference, unit))
        {
            std::fprintf(stderr, "FBX bind: equivalent noisy poses rejected at unit %.8g\n", unit);
            return false;
        }
        for (const auto& different_pose :
             {bind_pose(unit, 1.0, 0.001, 1.0), bind_pose(unit, 1.001, 0.0, 1.0),
              bind_pose(unit, 1.0, 0.0, 1.001)})
        {
            const auto different = ufbx_matrix_invert(&different_pose);
            if (converts(reference, different, unit) || converts(different, reference, unit))
            {
                std::fprintf(stderr, "FBX bind: incompatible pose accepted at unit %.8g\n", unit);
                return false;
            }
        }
        auto invalid = reference;
        invalid.m00 = invalid.m10 = invalid.m20 = 0.0;
        if (converts(reference, invalid, unit) || converts(invalid, reference, unit))
        {
            std::fputs("FBX bind: singular inverse binding accepted\n", stderr);
            return false;
        }
        invalid = reference;
        invalid.m00 = std::numeric_limits<double>::infinity();
        if (converts(reference, invalid, unit))
        {
            std::fputs("FBX bind: nonfinite inverse binding accepted\n", stderr);
            return false;
        }
    }
    return true;
}

bool expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FBX coordinates: %s\n", message.c_str());
    }
    return condition;
}

bool motion_track_validation()
{
    using namespace engine::plugins::asset_import;
    FbxDocument source;
    FbxDocument skeleton;
    ufbx_anim_stack stack{};
    ufbx_node joint{};
    source.animations.emplace("animations/motion", &stack);
    source.animation_motion["animations/motion"].joint = "root/Joint";
    skeleton.nodes.emplace("root/Joint", &joint);
    skeleton.joint_indices.emplace(&joint, 0);
    engine::schema::AnimationDataT animation;
    animation.duration = 1.0F;
    auto track = std::make_unique<engine::schema::AnimationTrackDataT>();
    track->translations = {{0.0F, {2.0, 3.0, 4.0}}, {1.0F, {5.0, 6.0, 7.0}}};
    track->rotations = {{0.0F, {0.0, 0.0, 0.0, 1.0}},
                        {1.0F, {0.0, std::sqrt(0.5), 0.0, std::sqrt(0.5)}}};
    track->scales = {{0.0F, {1.0, 2.0, 3.0}}, {1.0F, {2.0, 3.0, 4.0}}};
    animation.tracks.push_back(std::move(track));
    const auto original = animation;
    std::string error;
    if (!expect(apply_animation_motion(source, skeleton, stack, animation, error) &&
                    animation.tracks[0]->translations.back().value() ==
                        engine::schema::Vec3(2.0, 6.0, 4.0) &&
                    animation.tracks[0]->rotations == original.tracks[0]->rotations &&
                    animation.tracks[0]->scales == original.tracks[0]->scales,
                "default motion extraction removes XZ but preserves Y, rotation and scale"))
    {
        return false;
    }
    const auto rejects = [&](engine::schema::AnimationDataT candidate)
    {
        const auto before = candidate;
        error.clear();
        return expect(!apply_animation_motion(source, skeleton, stack, candidate, error) &&
                          !error.empty() && candidate == before,
                      "missing target joints or samples fail without mutating the animation");
    };
    source.animation_motion["animations/motion"].joint = "root/Missing";
    if (!rejects(original))
    {
        return false;
    }
    source.animation_motion["animations/motion"].joint = "root/Joint";
    skeleton.joint_indices.clear();
    if (!rejects(original))
    {
        return false;
    }
    skeleton.joint_indices.emplace(&joint, 0);
    auto missing = original;
    missing.tracks.clear();
    if (!rejects(missing))
    {
        return false;
    }
    missing = original;
    missing.tracks[0]->joint = 1;
    if (!rejects(missing))
    {
        return false;
    }
    missing = original;
    missing.tracks[0]->translations.clear();
    if (!rejects(missing))
    {
        return false;
    }
    missing = original;
    missing.tracks[0]->rotations.clear();
    if (!rejects(missing))
    {
        return false;
    }
    source.animation_motion.clear();
    animation = original;
    return expect(apply_animation_motion(source, skeleton, stack, animation, error) &&
                      animation == original,
                  "no motion configuration retains the original samples");
}

bool motion_rotation_axes()
{
    using namespace engine::plugins::asset_import;
    FbxDocument source;
    FbxDocument skeleton;
    ufbx_anim_stack stack{};
    ufbx_node joint{};
    source.animations.emplace("animations/motion", &stack);
    auto& motion = source.animation_motion["animations/motion"];
    motion.joint = "root/Joint";
    motion.position = {false, false, false};
    skeleton.nodes.emplace("root/Joint", &joint);
    skeleton.joint_indices.emplace(&joint, 0);
    constexpr double angle = 0.7;
    for (std::size_t animated_axis = 0; animated_axis < 3; ++animated_axis)
    {
        std::array<double, 3> quaternion{};
        quaternion[animated_axis] = std::sin(angle * 0.5);
        const double w = std::cos(angle * 0.5);
        engine::schema::AnimationDataT original;
        original.duration = 1.0F;
        auto track = std::make_unique<engine::schema::AnimationTrackDataT>();
        track->translations = {{0.0F, {2.0, 3.0, 4.0}}, {1.0F, {2.0, 3.0, 4.0}}};
        track->rotations = {{0.0F, {0.0, 0.0, 0.0, 1.0}},
                            {1.0F, {quaternion[0], quaternion[1], quaternion[2], w}}};
        track->scales = {{0.0F, {1.0, 2.0, 3.0}}, {1.0F, {2.0, 3.0, 4.0}}};
        original.tracks.push_back(std::move(track));
        for (std::size_t selected_axis = 0; selected_axis < 3; ++selected_axis)
        {
            motion.rotation = {};
            motion.rotation[selected_axis] = true;
            auto animation = original;
            std::string error;
            const auto label = "animated axis " + std::to_string(animated_axis) +
                               ", selected axis " + std::to_string(selected_axis);
            if (!expect(apply_animation_motion(source, skeleton, stack, animation, error), label))
            {
                return false;
            }
            const auto& baked = *animation.tracks.front();
            const auto& q = baked.rotations.back().value();
            const bool selected = animated_axis == selected_axis;
            const double dot = selected ? q.w()
                                        : q.x() * quaternion[0] + q.y() * quaternion[1] +
                                              q.z() * quaternion[2] + q.w() * w;
            const auto& p = baked.translations.back().value();
            ufbx_vec3 input{};
            input.x = 2.0;
            input.y = 3.0;
            input.z = 4.0;
            ufbx_quat inverse{};
            inverse.x = -quaternion[0];
            inverse.y = -quaternion[1];
            inverse.z = -quaternion[2];
            inverse.w = w;
            const auto expected = selected ? ufbx_quat_rotate_vec3(inverse, input) : input;
            if (!expect(std::abs(std::abs(dot) - 1.0) < 0.000001 &&
                            std::hypot(p.x() - expected.x, p.y() - expected.y,
                                       p.z() - expected.z) < 0.000001 &&
                            baked.scales == original.tracks[0]->scales,
                        "XYZ extraction removes only the selected axis and keeps upstream "
                        "translation recomposition: " + label))
            {
                return false;
            }
        }
    }
    return true;
}

struct TemporarySources
{
    std::filesystem::path root;
    ~TemporarySources()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

bool write_meta(const std::filesystem::path& source, const std::string& parameters)
{
    auto path = source;
    path += ".meta";
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << "sample_rate = 4\n" << parameters;
    stream.close();
    return static_cast<bool>(stream);
}

ufbx_matrix matrix(const std::vector<float>& values)
{
    ufbx_matrix result{};
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 3; ++row)
        {
            result.cols[column].v[row] = values[column * 4 + row];
        }
    }
    return result;
}

ufbx_matrix matrix(const engine::schema::Vec3& position, const engine::schema::Vec4& rotation,
                   const engine::schema::Vec3& scale)
{
    auto transform = ufbx_identity_transform;
    transform.translation.x = position.x();
    transform.translation.y = position.y();
    transform.translation.z = position.z();
    transform.rotation.x = rotation.x();
    transform.rotation.y = rotation.y();
    transform.rotation.z = rotation.z();
    transform.rotation.w = rotation.w();
    transform.scale.x = scale.x();
    transform.scale.y = scale.y();
    transform.scale.z = scale.z();
    return ufbx_transform_to_matrix(&transform);
}

bool close(const ufbx_matrix& left, const ufbx_matrix& right)
{
    for (std::size_t index = 0; index < 12; ++index)
    {
        if (!std::isfinite(left.v[index]) || !std::isfinite(right.v[index]) ||
            std::abs(left.v[index] - right.v[index]) >
                0.0001 * std::max({1.0, std::abs(left.v[index]), std::abs(right.v[index])}))
        {
            return false;
        }
    }
    return true;
}

std::vector<ufbx_matrix> animated_globals(const engine::schema::SkeletonDataT& skeleton,
                                          const engine::schema::AnimationDataT& animation)
{
    std::vector<ufbx_matrix> result;
    for (const auto& joint : skeleton.joints)
    {
        result.push_back(matrix(joint->translation, joint->rotation, joint->scale));
    }
    for (const auto& track : animation.tracks)
    {
        const auto frame = track->translations.size() / 2;
        result[track->joint] =
            matrix(track->translations[frame].value(), track->rotations[frame].value(),
                   track->scales[frame].value());
    }
    for (std::size_t index = 0; index < result.size(); ++index)
    {
        const auto parent = skeleton.joints[index]->parent;
        if (parent >= 0)
        {
            result[index] =
                ufbx_matrix_mul(&result[static_cast<std::size_t>(parent)], &result[index]);
        }
    }
    return result;
}

struct ConvertedFbx
{
    engine::plugins::asset_import::FbxDocument model_document;
    engine::plugins::asset_import::FbxDocument animation_document;
    engine::schema::SkeletonDataT skeleton;
    engine::schema::MeshDataT mesh;
    engine::schema::AnimationDataT animation;
};

bool convert_sources(const TemporarySources& sources, const std::string& parameters,
                     ConvertedFbx& output)
{
    using namespace engine::plugins::asset_import;
    std::string error;
    const auto model = sources.root / "model.fbx";
    const auto animation = sources.root / "animation.fbx";
    if (!write_meta(model, parameters) || !write_meta(animation, parameters) ||
        !load_document(model, output.model_document, error) ||
        !load_document(animation, output.animation_document, error) ||
        !document_settings_match(model, output.model_document, error) ||
        output.model_document.meshes.empty() || output.animation_document.animations.empty() ||
        !convert_skeleton(output.model_document, output.skeleton, error) ||
        !convert_mesh(output.model_document, *output.model_document.meshes.begin()->second,
                      output.mesh, error) ||
        !convert_animation(output.animation_document, output.model_document,
                           *output.animation_document.animations.begin()->second, output.animation,
                           error))
    {
        return expect(false, "production conversion failed: " + error);
    }
    return true;
}

bool animation_sample_boundaries(const ConvertedFbx& source)
{
    using namespace engine::plugins::asset_import;
    const auto& document = source.animation_document;
    const auto& skeleton = source.model_document;
    auto stack = *document.animations.begin()->second;
    stack.time_begin = 0.0;
    const auto boundary = 2.0 / document.sample_rate;
    for (const auto duration : {std::nextafter(boundary, 0.0), boundary,
                                std::nextafter(boundary, std::numeric_limits<double>::infinity()),
                                boundary - 0.000001, boundary + 0.000001})
    {
        stack.time_end = duration;
        engine::schema::AnimationDataT animation;
        std::string error;
        if (!expect(convert_animation(document, skeleton, stack, animation, error), error))
        {
            return false;
        }
        const std::size_t expected_count = duration == boundary + 0.000001 ? 4 : 3;
        for (const auto& track : animation.tracks)
        {
            const auto* joint = skeleton.joints[track->joint];
            const auto* node = document.nodes.at(skeleton.node_paths.at(joint));
            const auto end = ufbx_evaluate_transform(stack.anim, node, stack.time_end);
            const auto& final_position = track->translations.back().value();
            if (!expect(track->translations.size() == expected_count &&
                            track->rotations.size() == expected_count &&
                            track->scales.size() == expected_count &&
                            track->translations.front().time() == 0.0F &&
                            track->translations.back().time() == animation.duration &&
                            final_position.x() == end.translation.x &&
                            final_position.y() == end.translation.y &&
                            final_position.z() == end.translation.z,
                        "boundary sampling preserves the exact endpoint without duplicate times"))
            {
                return false;
            }
            for (std::size_t index = 1; index < expected_count; ++index)
            {
                if (!expect(track->translations[index].time() >
                                track->translations[index - 1].time(),
                            "boundary samples strictly increase"))
                {
                    return false;
                }
            }
        }
    }
    stack.time_end = std::numeric_limits<double>::denorm_min();
    engine::schema::AnimationDataT invalid;
    std::string error;
    return expect(!convert_animation(document, skeleton, stack, invalid, error) && !error.empty(),
                  "duration that cannot advance a float timestamp is rejected");
}

bool coordinate_settings()
{
    using namespace engine::plugins::asset_import;
    std::error_code code;
    const auto temporary = std::filesystem::temp_directory_path(code);
    if (code)
    {
        return false;
    }
    TemporarySources sources{
        temporary / ("engine-fbx-coordinate-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    std::filesystem::create_directory(sources.root, code);
    if (code)
    {
        return false;
    }
    const auto model = sources.root / "model.fbx";
    std::filesystem::copy_file(std::filesystem::path(ENGINE_FBX_TEST_DATA_DIR) / "skeleton.fbx",
                               model, code);
    if (code)
    {
        return false;
    }
    std::filesystem::copy_file(std::filesystem::path(ENGINE_FBX_TEST_DATA_DIR) / "locomotions.fbx",
                               sources.root / "animation.fbx", code);
    if (code)
    {
        return false;
    }
    ConvertedFbx baseline;
    if (!convert_sources(sources, {}, baseline) || !animation_sample_boundaries(baseline))
    {
        return false;
    }
    const auto base_globals = animated_globals(baseline.skeleton, baseline.animation);
    const auto base_mesh = matrix(baseline.mesh.mesh_to_skeleton);
    for (const auto& parameters : {"scale_factor = 0.01\nrotation_offset = [0, 0, 0]\n",
                                   "scale_factor = 2.0\nrotation_offset = [25, -45, 70]\n",
                                   "scale_factor = 100.0\nrotation_offset = [90, 180, -30]\n"})
    {
        ConvertedFbx adjusted;
        if (!convert_sources(sources, parameters, adjusted))
        {
            return false;
        }
        auto transform = ufbx_identity_transform;
        const auto& document = adjusted.model_document;
        transform.scale.x = transform.scale.y = transform.scale.z = document.scale_factor;
        ufbx_vec3 angles{};
        std::copy(document.rotation_offset.begin(), document.rotation_offset.end(), angles.v);
        transform.rotation = ufbx_euler_to_quat(angles, UFBX_ROTATION_ORDER_XYZ);
        const auto correction = ufbx_transform_to_matrix(&transform);
        const auto inverse = ufbx_matrix_invert(&correction);
        if (!expect(document.nodes.size() == baseline.model_document.nodes.size() &&
                        adjusted.skeleton.joints.size() == baseline.skeleton.joints.size() &&
                        adjusted.mesh.vertices.size() == baseline.mesh.vertices.size(),
                    "coordinate import preserves hierarchy, joints and mesh topology"))
        {
            return false;
        }
        for (const auto& [path, node] : document.nodes)
        {
            const auto* original = baseline.model_document.nodes.at(path);
            if (!expect(close(node->node_to_world,
                              ufbx_matrix_mul(&correction, &original->node_to_world)),
                        "model hierarchy composes correction with native axes/units"))
            {
                return false;
            }
        }
        const auto mesh = matrix(adjusted.mesh.mesh_to_skeleton);
        if (!expect(close(mesh, ufbx_matrix_mul(&correction, &base_mesh)),
                    "mesh_to_skeleton includes the same import correction"))
        {
            return false;
        }
        const auto globals = animated_globals(adjusted.skeleton, adjusted.animation);
        for (std::size_t index = 0; index < adjusted.skeleton.joints.size(); ++index)
        {
            const auto base_bind = matrix(baseline.skeleton.joints[index]->inverse_bind);
            const auto bind = matrix(adjusted.skeleton.joints[index]->inverse_bind);
            const auto base_skin = ufbx_matrix_mul(&base_globals[index], &base_bind);
            const auto skin = ufbx_matrix_mul(&globals[index], &bind);
            const auto base_vertex = ufbx_matrix_mul(&base_skin, &base_mesh);
            if (!expect(close(bind, ufbx_matrix_mul(&base_bind, &inverse)),
                        "inverse bindings compensate import correction") ||
                !expect(close(globals[index], ufbx_matrix_mul(&correction, &base_globals[index])),
                        "animation samples and rest joints use the same coordinates") ||
                !expect(close(ufbx_matrix_mul(&skin, &mesh),
                              ufbx_matrix_mul(&correction, &base_vertex)),
                        "animated skin positions scale and rotate once"))
            {
                return false;
            }
        }
        engine::schema::AnimationDataT mismatched;
        std::string error;
        if (!expect(!convert_animation(adjusted.animation_document, baseline.model_document,
                                       *adjusted.animation_document.animations.begin()->second,
                                       mismatched, error) &&
                        error.find("identical scale_factor") != std::string::npos,
                    "external skeleton coordinate mismatch fails explicitly"))
        {
            return false;
        }
    }
    for (const auto& invalid :
         {"scale_factor = 0\n", "scale_factor = -1\n", "scale_factor = inf\n",
          "scale_factor = 0.00001\n", "scale_factor = 100001\n", "scale_factor = true\n",
          "rotation_offset = [0, 1]\n", "rotation_offset = [0, 0, 0, 0]\n",
          "rotation_offset = [0, inf, 0]\n", "rotation_offset = [0, '90', 0]\n"})
    {
        FbxDocument rejected;
        std::string error;
        if (!write_meta(model, invalid) ||
            !expect(!load_document(model, rejected, error) && !error.empty(),
                    "invalid import coordinates are rejected"))
        {
            return false;
        }
    }
    std::string error;
    return write_meta(model, "scale_factor = 2.0\n") &&
           expect(!document_settings_match(model, baseline.model_document, error),
                  "in-flight coordinate settings changes invalidate the batch");
}

bool imported_tangent_frames()
{
    using namespace engine::plugins::asset_import;
    std::error_code code;
    const auto temporary = std::filesystem::temp_directory_path(code);
    if (code)
    {
        return false;
    }
    TemporarySources sources{
        temporary / ("engine-fbx-tangents-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    std::filesystem::create_directory(sources.root, code);
    if (code)
    {
        return false;
    }
    const auto model = sources.root / "tangents.fbx";
    std::filesystem::copy_file(std::filesystem::path(ENGINE_UFBX_TEST_DATA_DIR) /
                                   "maya_tangent_sign_mixed_7700_ascii.fbx",
                               model, code);
    if (code || !write_meta(model, ""))
    {
        return false;
    }
    FbxDocument document;
    std::string error;
    if (!expect(load_document(model, document, error) && !document.meshes.empty(),
                "load ufbx mixed-handedness fixture: " + error))
    {
        return false;
    }
    const auto* node = document.meshes.begin()->second;
    const auto& source = *node->mesh;
    engine::schema::MeshDataT converted;
    if (!expect(source.vertex_tangent.exists && source.vertex_bitangent.exists &&
                    convert_mesh(document, *node, converted, error) &&
                    converted.tangents.size() == converted.vertices.size(),
                "FBX tangent and bitangent reach the converted mesh: " + error))
    {
        return false;
    }

    // convert_mesh preserves one vertex per FBX face corner, reusing a corner
    // when a polygon is triangulated. Verify those original authored frames.
    std::map<std::uint32_t, std::uint32_t> corners;
    std::vector<std::uint32_t> triangles(source.max_face_triangles * 3);
    bool positive = false;
    bool negative = false;
    for (std::size_t face_index = 0; face_index < source.faces.count; ++face_index)
    {
        const auto face = source.faces.data[face_index];
        if (face.num_indices < 3)
        {
            continue;
        }
        const auto count = ufbx_triangulate_face(triangles.data(), triangles.size(), &source, face);
        for (std::size_t i = 0; i < count * 3; ++i)
        {
            const auto corner = triangles[i];
            const auto [entry, inserted] =
                corners.emplace(corner, static_cast<std::uint32_t>(corners.size()));
            if (!inserted)
            {
                continue;
            }
            const auto tangent = ufbx_get_vertex_vec3(&source.vertex_tangent, corner);
            const auto bitangent = ufbx_get_vertex_vec3(&source.vertex_bitangent, corner);
            const auto normal = ufbx_get_vertex_vec3(&source.vertex_normal, corner);
            const double orientation =
                (normal.y * tangent.z - normal.z * tangent.y) * bitangent.x +
                (normal.z * tangent.x - normal.x * tangent.z) * bitangent.y +
                (normal.x * tangent.y - normal.y * tangent.x) * bitangent.z;
            const auto& actual = converted.tangents[entry->second];
            const double sign = orientation < 0 ? -1.0 : 1.0;
            if (!expect(std::abs(actual.x() - tangent.x) < 1e-5 &&
                            std::abs(actual.y() - tangent.y) < 1e-5 &&
                            std::abs(actual.z() - tangent.z) < 1e-5 && actual.w() == sign,
                        "authored tangent direction and handedness survive conversion"))
            {
                return false;
            }
            positive |= sign > 0;
            negative |= sign < 0;
        }
    }
    if (!expect(positive && negative, "fixture exercises both UV orientations"))
    {
        return false;
    }
    document.normals_mode = "generate";
    engine::schema::MeshDataT regenerated;
    return expect(convert_mesh(document, *node, regenerated, error) &&
                      regenerated.tangents.size() == regenerated.vertices.size(),
                  "regenerated normals receive generated MikkTSpace frames");
}

bool generated_tangent_frames()
{
    using namespace engine::plugins::asset_import;
    // A bow-tie UV quad produces opposite UV orientations across the shared
    // diagonal regardless of which diagonal the FBX triangulator selects.
    const auto vec2 = [](double x, double y)
    {
        ufbx_vec2 result{};
        result.x = x;
        result.y = y;
        return result;
    };
    const auto vec3 = [](double x, double y, double z)
    {
        ufbx_vec3 result{};
        result.x = x;
        result.y = y;
        result.z = z;
        return result;
    };
    std::array<ufbx_vec3, 7> positions{vec3(0, 0, 0), vec3(1, 0, 0), vec3(1, 1, 0),
                                       vec3(0, 1, 0), vec3(2, 0, 0), vec3(3, 0, 0),
                                       vec3(2, 1, 0)};
    std::array<ufbx_vec3, 7> normals{};
    normals.fill(vec3(0, 0, 1));
    std::array<ufbx_vec2, 7> uv{vec2(0, 0), vec2(1, 1), vec2(0, 1), vec2(1, 0),
                                vec2(0, 0), vec2(1, 0), vec2(0, 1)};
    std::array<ufbx_vec4, 7> colors{};
    std::array<std::uint32_t, 7> indices{};
    std::array<ufbx_skin_vertex, 7> vertices{};
    std::array<ufbx_skin_weight, 7> weights{};
    std::array<ufbx_node, 7> bones{};
    std::array<ufbx_skin_cluster, 7> clusters{};
    std::array<ufbx_skin_cluster*, 7> cluster_pointers{};
    FbxDocument document;
    for (std::size_t i = 0; i < indices.size(); ++i)
    {
        indices[i] = static_cast<std::uint32_t>(i);
        colors[i].x = static_cast<double>(i) / 7;
        colors[i].y = 0.25;
        colors[i].z = 0.5;
        colors[i].w = 1;
        vertices[i].weight_begin = static_cast<std::uint32_t>(i);
        vertices[i].num_weights = 1;
        weights[i].cluster_index = static_cast<std::uint32_t>(i);
        weights[i].weight = 1;
        clusters[i].bone_node = &bones[i];
        cluster_pointers[i] = &clusters[i];
        document.joint_indices.emplace(&bones[i], static_cast<std::uint32_t>(i));
    }
    ufbx_skin_deformer skin{};
    skin.clusters = {cluster_pointers.data(), cluster_pointers.size()};
    skin.vertices = {vertices.data(), vertices.size()};
    skin.weights = {weights.data(), weights.size()};
    ufbx_skin_deformer* skin_pointer = &skin;
    std::array<ufbx_face, 2> faces{{{0, 4}, {4, 3}}};
    std::array<std::uint32_t, 2> materials{0, 1};
    ufbx_mesh mesh{};
    mesh.num_vertices = indices.size();
    mesh.num_indices = indices.size();
    mesh.num_faces = faces.size();
    mesh.num_triangles = 3;
    mesh.max_face_triangles = 2;
    mesh.faces = {faces.data(), faces.size()};
    mesh.face_material = {materials.data(), materials.size()};
    mesh.vertex_indices = {indices.data(), indices.size()};
    mesh.vertex_position.exists = true;
    mesh.vertex_position.values = {positions.data(), positions.size()};
    mesh.vertex_position.indices = {indices.data(), indices.size()};
    mesh.vertex_normal.exists = true;
    mesh.vertex_normal.values = {normals.data(), normals.size()};
    mesh.vertex_normal.indices = {indices.data(), indices.size()};
    mesh.vertex_uv.exists = true;
    mesh.vertex_uv.values = {uv.data(), uv.size()};
    mesh.vertex_uv.indices = {indices.data(), indices.size()};
    mesh.vertex_color.exists = true;
    mesh.vertex_color.values = {colors.data(), colors.size()};
    mesh.vertex_color.indices = {indices.data(), indices.size()};
    mesh.skin_deformers = {&skin_pointer, 1};
    ufbx_node node{};
    node.mesh = &mesh;
    node.geometry_to_world = ufbx_identity_matrix;
    engine::schema::MeshDataT output;
    output.path = "models/generated.fbx/meshes/quad";
    output.skeleton_guid = "e7ac4008-9f4b-4ed9-b16c-76c2663e0248";
    std::string error;
    if (!expect(convert_mesh(document, node, output, error),
                "generate MikkTSpace frames on skinned mirrored quad: " + error) ||
        !expect(output.vertices.size() > indices.size() &&
                    output.tangents.size() == output.vertices.size() &&
                    output.texcoords.size() == output.vertices.size() &&
                    output.colors.size() == output.vertices.size() &&
                    output.skin.size() == output.vertices.size(),
                "different per-corner frames split vertices and copy all attribute streams") ||
        !expect(output.submeshes.size() == 2 && output.submeshes[0].first_index() == 0 &&
                    output.submeshes[0].index_count() == 6 &&
                    output.submeshes[0].material_slot() == 0 &&
                    output.submeshes[1].first_index() == 6 &&
                    output.submeshes[1].index_count() == 3 &&
                    output.submeshes[1].material_slot() == 1,
                "splitting preserves submesh index ranges and material slots"))
    {
        return false;
    }
    if (!expect(output.path == "models/generated.fbx/meshes/quad" &&
                    output.skeleton_guid == "e7ac4008-9f4b-4ed9-b16c-76c2663e0248",
                "tangent conversion preserves the resource identity and skeleton link"))
    {
        return false;
    }
    bool positive = false;
    bool negative = false;
    for (std::size_t i = 0; i < output.vertices.size(); ++i)
    {
        const auto& p = output.vertices[i];
        std::size_t original = 0;
        while (original < positions.size() &&
               (p.x() != positions[original].x || p.y() != positions[original].y))
        {
            ++original;
        }
        if (!expect(original < positions.size(), "split vertex position is preserved"))
        {
            return false;
        }
        if (!expect(output.texcoords[i].x() == uv[original].x &&
                        output.texcoords[i].y() == uv[original].y &&
                        std::abs(output.colors[i].r() - colors[original].x) < 1e-6 &&
                        output.skin[i].j0() == original && output.skin[i].w0() == 1.0F,
                    "split vertex preserves UV, color, and skin influence"))
        {
            return false;
        }
    }
    for (std::size_t i = 0; i < 6; ++i)
    {
        const auto& tangent = output.tangents[output.indices[i]];
        positive |= tangent.w() > 0;
        negative |= tangent.w() < 0;
    }
    if (!expect(positive && negative, "mirrored quad keeps both tangent handedness values"))
    {
        return false;
    }
    // The second submesh has an analytically known basis: U follows +X, V
    // follows +Y and the normal is +Z, so its frame is (+X, sign +1).
    for (std::size_t i = 6; i < 9; ++i)
    {
        const auto& tangent = output.tangents[output.indices[i]];
        if (!expect(std::abs(tangent.x() - 1.0) < 1e-6 && std::abs(tangent.y()) < 1e-6 &&
                        std::abs(tangent.z()) < 1e-6 && tangent.w() == 1.0,
                    "generated tangent follows the known UV frame"))
        {
            return false;
        }
    }
    const auto snapshot = [](const engine::schema::MeshDataT& data)
    {
        flatbuffers::FlatBufferBuilder builder;
        engine::schema::FinishMeshDataBuffer(
            builder, engine::schema::MeshData::Pack(builder, &data));
        return std::vector<std::uint8_t>(builder.GetBufferPointer(),
                                         builder.GetBufferPointer() + builder.GetSize());
    };
    const auto valid_snapshot = snapshot(output);
    if (!expect(convert_mesh(document, node, output, error) &&
                    snapshot(output) == valid_snapshot,
                "reusing an output replaces the mesh rather than appending old corners"))
    {
        return false;
    }
    for (const double invalid : {std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::max()})
    {
        const auto original_position = positions.back();
        positions.back().x = invalid;
        error.clear();
        const bool position_failed = !convert_mesh(document, node, output, error);
        positions.back() = original_position;
        if (!expect(position_failed && !error.empty() && snapshot(output) == valid_snapshot,
                    "invalid position fails before MikkTSpace and preserves prior complete output"))
        {
            return false;
        }
        const auto original_uv = uv.back();
        uv.back().x = invalid;
        error.clear();
        const bool uv_failed = !convert_mesh(document, node, output, error);
        uv.back() = original_uv;
        if (!expect(uv_failed && !error.empty() && snapshot(output) == valid_snapshot,
                    "invalid UV fails before MikkTSpace and preserves prior complete output"))
        {
            return false;
        }
    }
    std::array<ufbx_vec3, 7> invalid_tangents{};
    invalid_tangents.fill(vec3(1, 0, 0));
    invalid_tangents.back() = vec3(0, 0, 0);
    std::array<ufbx_vec3, 7> source_bitangents{};
    source_bitangents.fill(vec3(0, 1, 0));
    mesh.vertex_tangent.exists = true;
    mesh.vertex_tangent.values = {invalid_tangents.data(), invalid_tangents.size()};
    mesh.vertex_tangent.indices = {indices.data(), indices.size()};
    mesh.vertex_bitangent.exists = true;
    mesh.vertex_bitangent.values = {source_bitangents.data(), source_bitangents.size()};
    mesh.vertex_bitangent.indices = {indices.data(), indices.size()};
    engine::schema::MeshDataT invalid_source;
    if (!expect(convert_mesh(document, node, invalid_source, error) &&
                    invalid_source.tangents.size() == invalid_source.vertices.size() &&
                    std::hypot(invalid_source.tangents[0].x(), invalid_source.tangents[0].y(),
                               invalid_source.tangents[0].z()) > 0.5,
                "invalid authored tangents are replaced by valid generated frames: " + error))
    {
        return false;
    }
    mesh.vertex_tangent.exists = false;
    mesh.vertex_bitangent.exists = false;
    uv.fill(vec2(0, 0));
    engine::schema::MeshDataT degenerate;
    if (!expect(convert_mesh(document, node, degenerate, error) &&
                    degenerate.tangents.size() == degenerate.vertices.size(),
                "degenerate UVs receive valid MikkTSpace fallback frames: " + error))
    {
        return false;
    }
    mesh.vertex_uv.exists = false;
    engine::schema::MeshDataT without_uv;
    if (!expect(convert_mesh(document, node, without_uv, error) && without_uv.tangents.empty(),
                "mesh without UVs does not fabricate a texture tangent basis"))
    {
        return false;
    }
    normals.back() = vec3(0, 0, 0);
    error.clear();
    return expect(!convert_mesh(document, node, output, error) && !error.empty() &&
                      snapshot(output) == valid_snapshot,
                  "invalid normals preserve the previous complete mesh after partial conversion");
}

} // namespace

int main()
{
    return unit_independent_bind_comparison() && coordinate_settings() && imported_tangent_frames() &&
                   generated_tangent_frames() && motion_track_validation() && motion_rotation_axes()
               ? 0
               : 1;
}
