#pragma once

#include "runtime/schema/animation_generated.h"
#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/model_generated.h"
#include "runtime/schema/skeleton_generated.h"

#include <ufbx.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace engine::plugins::fbx_import
{
struct SceneDeleter
{
    void operator()(ufbx_scene* scene) const noexcept;
};

struct FbxDocument
{
    std::unique_ptr<ufbx_scene, SceneDeleter> scene;
    std::map<const ufbx_node*, std::string> node_paths;
    std::map<std::string, const ufbx_node*> nodes;
    std::map<std::string, const ufbx_node*> meshes;
    std::map<std::string, const ufbx_anim_stack*> animations;
    std::vector<const ufbx_node*> joints;
    std::map<const ufbx_node*, std::uint32_t> joint_indices;
    std::string external_skeleton;
    std::string normals_mode = "import";
    std::string source_hash;
    double sample_rate = 30.0;
};

[[nodiscard]] bool load_document(const std::filesystem::path& path, FbxDocument& document,
                                 std::string& error);
[[nodiscard]] bool convert_skeleton(const FbxDocument& document, schema::SkeletonDataT& output,
                                    std::string& error);
[[nodiscard]] bool convert_mesh(const FbxDocument& document, const ufbx_node& node,
                                schema::MeshDataT& output, std::string& error);
[[nodiscard]] bool convert_animation(const FbxDocument& source, const FbxDocument& skeleton,
                                     const ufbx_anim_stack& stack, schema::AnimationDataT& output,
                                     std::string& error);
[[nodiscard]] std::vector<float> matrix_values(const ufbx_matrix& matrix);
} // namespace engine::plugins::fbx_import
