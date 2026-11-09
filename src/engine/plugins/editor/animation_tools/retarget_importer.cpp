#include "plugins/editor/animation_tools/retarget_importer.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "runtime/resource/codecs/animation_retarget_resource_codec.hpp"
#include "runtime/resource/codecs/animation_retarget_validation.hpp"
#include "runtime/resource/codecs/skeleton_resource_codec.hpp"

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <set>
#include <utility>

namespace engine::plugins::animation_tools
{
namespace
{
namespace io = editor::asset_pipeline;

bool load_skeleton(const Guid& guid, const io::ImportContext& context,
                   std::unique_ptr<schema::SkeletonDataT>& output, std::string& error)
{
    std::vector<std::byte> payload;
    if (!io::read_generated_payload(guid, context, payload, error))
    {
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!schema::VerifySkeletonDataBuffer(verifier))
    {
        error = "Invalid retarget skeleton dependency: " + guid;
        return false;
    }
    output = schema::UnPackSkeletonData(bytes);
    if (!SkeletonResourceCodec::validate(*output))
    {
        error = "Retarget skeleton dependency failed validation: " + guid;
        return false;
    }
    return true;
}

template <std::size_t N>
bool numbers(const toml::node_view<const toml::node>& node, std::array<double, N>& output)
{
    const auto* values = node.as_array();
    if (!values || values->size() != N)
    {
        return false;
    }
    for (std::size_t index = 0; index < N; ++index)
    {
        const auto number = (*values)[index].value<double>();
        if (!number)
        {
            return false;
        }
        output[index] = *number;
    }
    return true;
}

bool poses(const toml::table& table, const char* name, std::vector<schema::JointLocalPose>& output)
{
    if (!table.contains(name))
    {
        return true;
    }
    const auto* entries = table[name].as_array();
    if (!entries || entries->size() > 1024)
    {
        return false;
    }
    for (const auto& entry : *entries)
    {
        const auto* item = entry.as_table();
        std::array<double, 3> translation{}, scale{};
        std::array<double, 4> rotation{};
        if (!item || item->size() != 3 || !numbers((*item)["translation"], translation) ||
            !numbers((*item)["rotation"], rotation) || !numbers((*item)["scale"], scale))
        {
            return false;
        }
        output.emplace_back(schema::Vec3(translation[0], translation[1], translation[2]),
                            schema::Vec4(rotation[0], rotation[1], rotation[2], rotation[3]),
                            schema::Vec3(scale[0], scale[1], scale[2]));
    }
    return true;
}

bool read(const io::ImportAsset& asset, schema::AnimationRetargetProfileDataT& output,
          std::string& error)
{
    output = {};
    toml::table table;
    if (!io::read_table(asset.source, table, error))
    {
        return false;
    }
    const std::set<std::string_view> keys{
        "source_skeleton",       "target_skeleton",       "joint_mappings",
        "source_reference_pose", "target_reference_pose", "hips_translation_mode",
        "source_hips_path",      "target_hips_path",      "hips_translation_scale"};
    for (const auto& [key, value] : table)
    {
        (void)value;
        if (!keys.contains(key.str()))
        {
            error = "Unknown retarget profile property: " + std::string(key.str());
            return false;
        }
    }
    output.path = asset.path;
    output.source_skeleton_guid = table["source_skeleton"].value_or(std::string{});
    output.target_skeleton_guid = table["target_skeleton"].value_or(std::string{});
    for (const char* name : {"hips_translation_mode", "source_hips_path", "target_hips_path"})
    {
        if (table.contains(name) && !table[name].value<std::string>())
        {
            error = "Retarget profile string property has the wrong type: " + std::string(name);
            return false;
        }
    }
    if (table.contains("hips_translation_scale") &&
        !table["hips_translation_scale"].value<double>())
    {
        error = "Retarget hips_translation_scale must be a number";
        return false;
    }
    const auto mode = table["hips_translation_mode"].value_or(std::string{"target_reference"});
    if (mode != "target_reference" && mode != "scaled_source_delta")
    {
        error = "Retarget hips_translation_mode must be target_reference or scaled_source_delta";
        return false;
    }
    output.hips_translation_mode = mode == "target_reference"
                                       ? schema::HipsTranslationMode::target_reference
                                       : schema::HipsTranslationMode::scaled_source_delta;
    output.source_hips_path = table["source_hips_path"].value_or(std::string{});
    output.target_hips_path = table["target_hips_path"].value_or(std::string{});
    output.hips_translation_scale = table["hips_translation_scale"].value_or(1.0);
    const auto* mappings = table["joint_mappings"].as_array();
    if (!mappings || mappings->empty() || mappings->size() > 1024)
    {
        error = "Retarget profile requires a bounded joint_mappings array";
        return false;
    }
    for (const auto& entry : *mappings)
    {
        const auto* item = entry.as_table();
        if (!item || item->size() != 2)
        {
            error = "Each retarget mapping requires source_joint_path and target_joint_path";
            return false;
        }
        auto mapping = std::make_unique<schema::RetargetJointMappingDataT>();
        mapping->source_joint_path = (*item)["source_joint_path"].value_or(std::string{});
        mapping->target_joint_path = (*item)["target_joint_path"].value_or(std::string{});
        output.joint_mappings.push_back(std::move(mapping));
    }
    if (!poses(table, "source_reference_pose", output.source_reference_pose) ||
        !poses(table, "target_reference_pose", output.target_reference_pose) ||
        !AnimationRetargetResourceCodec::validate(output))
    {
        error =
            "Invalid retarget profile GUIDs, mappings, reference TRS or hips policy: " + asset.path;
        return false;
    }
    return true;
}
} // namespace

schema::AssetKind RetargetImporter::kind() const noexcept
{
    return schema::AssetKind::animation_retarget_profile;
}

bool RetargetImporter::matches(const std::filesystem::path& source) const
{
    return io::source_extension(source) == ".retarget";
}

bool RetargetImporter::inspect_batch(const io::ImportContext&, std::string& fingerprint,
                                     std::string& error)
{
    return io::schema_fingerprint(
        {"runtime/animation_retarget.fbs", "runtime/animation_pose.fbs", "runtime/common.fbs"},
        fingerprint, error);
}

bool RetargetImporter::scan_dependencies(io::ImportAsset& asset, const io::ImportContext& context,
                                         std::string& error) const
{
    schema::AnimationRetargetProfileDataT profile;
    std::vector<Guid> dependencies;
    if (!read(asset, profile, error) ||
        !io::reference(profile.source_skeleton_guid, schema::AssetKind::skeleton, context,
                       dependencies, error) ||
        !io::reference(profile.target_skeleton_guid, schema::AssetKind::skeleton, context,
                       dependencies, error))
    {
        return false;
    }
    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    asset.build_dependencies = dependencies;
    asset.runtime_dependencies = std::move(dependencies);
    return true;
}

bool RetargetImporter::convert(const io::ImportAsset& asset, const io::ImportContext& context,
                               std::string& error) const
{
    schema::AnimationRetargetProfileDataT profile;
    std::unique_ptr<schema::SkeletonDataT> source, target;
    if (!read(asset, profile, error) ||
        !load_skeleton(profile.source_skeleton_guid, context, source, error) ||
        !load_skeleton(profile.target_skeleton_guid, context, target, error))
    {
        return false;
    }
    if (!retarget_resource_detail::binding_shape(profile, *source, *target))
    {
        error = "Retarget profile references unknown joint paths or incomplete reference poses: " +
                asset.path;
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishAnimationRetargetProfileDataBuffer(
        builder, schema::AnimationRetargetProfileData::Pack(builder, &profile));
    return io::write_bytes(context.output / asset.guid,
                           std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                           error);
}
} // namespace engine::plugins::animation_tools
