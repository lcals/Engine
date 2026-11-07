#include "plugins/editor/asset_import/fbx_authoring.hpp"

#include "runtime/resource/codecs/skeleton_resource_codec.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include <string_view>

namespace engine::plugins::asset_import
{
namespace
{
bool fields(const toml::table& entry, std::initializer_list<std::string_view> allowed,
            std::string& error)
{
    for (const auto& [key, unused] : entry)
    {
        (void)unused;
        if (std::find(allowed.begin(), allowed.end(), key.str()) == allowed.end())
        {
            error = "Unknown FBX animation authoring property: " + std::string(key.str());
            return false;
        }
    }
    return true;
}

bool text(const toml::table& entry, const char* field, std::string& output)
{
    const auto value = entry[field].value<std::string>();
    if (!value || value->empty() || value->size() > 1024 || value->find('\0') != std::string::npos)
    {
        return false;
    }
    output = *value;
    return true;
}

bool number(const toml::table& entry, const char* field, float& output)
{
    if (!entry.contains(field))
    {
        return true;
    }
    const auto value = entry[field].value<double>();
    if (!value || !std::isfinite(*value) || std::abs(*value) > std::numeric_limits<float>::max())
    {
        return false;
    }
    output = static_cast<float>(*value);
    return true;
}

template <std::size_t Size>
bool numbers(const toml::table& entry, const char* field, std::array<float, Size>& output)
{
    if (!entry.contains(field))
    {
        return true;
    }
    const auto* values = entry[field].as_array();
    if (!values || values->size() != Size)
    {
        return false;
    }
    for (std::size_t index = 0; index < Size; ++index)
    {
        const auto value = (*values)[index].value<double>();
        if (!value || !std::isfinite(*value) ||
            std::abs(*value) > std::numeric_limits<float>::max())
        {
            return false;
        }
        output[index] = static_cast<float>(*value);
    }
    return true;
}

schema::Vec3 vector(const std::array<float, 3>& value)
{
    return {value[0], value[1], value[2]};
}

bool order(const toml::table& entry, std::uint16_t& output)
{
    if (!entry.contains("order"))
    {
        return true;
    }
    const auto value = entry["order"].value<std::int64_t>();
    if (!value || *value < 0 || *value > std::numeric_limits<std::uint16_t>::max())
    {
        return false;
    }
    output = static_cast<std::uint16_t>(*value);
    return true;
}

bool socket(const toml::table& entry, FbxDocument& document, std::string& error)
{
    auto value = std::make_unique<schema::SkeletonSocketDataT>();
    std::array<float, 3> translation{}, scale{1, 1, 1};
    std::array<float, 4> rotation{0, 0, 0, 1};
    if (!fields(entry, {"name", "joint_path", "translation", "rotation", "scale"}, error) ||
        !text(entry, "name", value->name) || !text(entry, "joint_path", value->joint_path) ||
        !numbers(entry, "translation", translation) || !numbers(entry, "rotation", rotation) ||
        !numbers(entry, "scale", scale))
    {
        return false;
    }
    value->offset = schema::JointLocalPose(
        vector(translation), {rotation[0], rotation[1], rotation[2], rotation[3]}, vector(scale));
    document.skeleton_authoring.sockets.push_back(std::move(value));
    return true;
}

bool aim(const toml::table& entry, FbxDocument& document, std::string& error)
{
    auto value = std::make_unique<schema::SkeletonAimIkDataT>();
    std::array<float, 3> forward{0, 0, 1}, up{0, 1, 0}, offset{};
    if (!fields(entry, {"name", "joint_path", "order", "forward_joint", "up_joint", "offset_joint"},
                error) ||
        !text(entry, "name", value->name) || !text(entry, "joint_path", value->joint_path) ||
        !order(entry, value->order) || !numbers(entry, "forward_joint", forward) ||
        !numbers(entry, "up_joint", up) || !numbers(entry, "offset_joint", offset))
    {
        return false;
    }
    value->forward_joint = vector(forward);
    value->up_joint = vector(up);
    value->offset_joint = vector(offset);
    document.skeleton_authoring.aim_ik.push_back(std::move(value));
    return true;
}

bool two_bone(const toml::table& entry, FbxDocument& document, std::string& error)
{
    auto value = std::make_unique<schema::SkeletonTwoBoneIkDataT>();
    std::array<float, 3> axis{0, 0, 1};
    if (!fields(entry,
                {"name", "start_path", "middle_path", "end_path", "order", "mid_axis_joint",
                 "soften", "twist_radians"},
                error) ||
        !text(entry, "name", value->name) || !text(entry, "start_path", value->start_path) ||
        !text(entry, "middle_path", value->middle_path) ||
        !text(entry, "end_path", value->end_path) || !order(entry, value->order) ||
        !numbers(entry, "mid_axis_joint", axis) || !number(entry, "soften", value->soften) ||
        !number(entry, "twist_radians", value->twist_radians))
    {
        return false;
    }
    value->mid_axis_joint = vector(axis);
    document.skeleton_authoring.two_bone_ik.push_back(std::move(value));
    return true;
}

bool event(const toml::table& entry, FbxDocument& document, std::string& error)
{
    auto value = std::make_unique<schema::AnimationEventMarkerDataT>();
    std::string animation;
    const auto id = entry["marker_id"].value<std::int64_t>();
    if (!fields(entry,
                {"animation", "marker_id", "time", "name", "payload_bool", "payload_number",
                 "payload_string"},
                error) ||
        !text(entry, "animation", animation) || !text(entry, "name", value->name) || !id ||
        *id <= 0 || *id > std::numeric_limits<std::uint32_t>::max() || !entry.contains("time") ||
        !number(entry, "time", value->time) || value->time < 0.0F)
    {
        return false;
    }
    value->marker_id = static_cast<std::uint32_t>(*id);
    unsigned payload_count = 0;
    if (entry.contains("payload_bool"))
    {
        const auto payload = entry["payload_bool"].value<bool>();
        if (!payload)
        {
            return false;
        }
        schema::AnimationEventBooleanT data;
        data.value = *payload;
        value->payload.Set(std::move(data));
        ++payload_count;
    }
    if (entry.contains("payload_number"))
    {
        const auto payload = entry["payload_number"].value<double>();
        if (!payload || !std::isfinite(*payload))
        {
            return false;
        }
        schema::AnimationEventNumberT data;
        data.value = *payload;
        value->payload.Set(std::move(data));
        ++payload_count;
    }
    if (entry.contains("payload_string"))
    {
        const auto payload = entry["payload_string"].value<std::string>();
        if (!payload)
        {
            return false;
        }
        schema::AnimationEventStringT data;
        data.value = *payload;
        value->payload.Set(std::move(data));
        ++payload_count;
    }
    if (payload_count > 1)
    {
        return false;
    }
    document.animation_authoring[animation].events.push_back(std::move(value));
    return true;
}
} // namespace

bool read_animation_authoring(const toml::table& meta, FbxDocument& document, std::string& error)
{
    document.skeleton_authoring = {};
    document.animation_authoring.clear();
    document.authoring_settings.clear();
    toml::table normalized;
    struct Section
    {
        const char* name;
        bool (*read)(const toml::table&, FbxDocument&, std::string&);
        std::size_t limit;
    };
    constexpr Section sections[] = {{"skeleton_sockets", socket, 1024},
                                    {"skeleton_aim_ik", aim, 1024},
                                    {"skeleton_two_bone_ik", two_bone, 1024},
                                    {"animation_events", event, 4096}};
    for (const auto& section : sections)
    {
        if (!meta.contains(section.name))
        {
            continue;
        }
        const auto* entries = meta[section.name].as_array();
        if (!entries || entries->size() > section.limit)
        {
            error = std::string("FBX ") + section.name + " requires a bounded table array";
            return false;
        }
        normalized.insert(section.name, *entries);
        for (const auto& entry : *entries)
        {
            const auto* table = entry.as_table();
            if (!table || !section.read(*table, document, error))
            {
                if (error.empty())
                {
                    error = std::string("Invalid FBX ") + section.name + " entry";
                }
                return false;
            }
        }
    }
    std::ostringstream formatted;
    formatted << toml::toml_formatter(normalized);
    document.authoring_settings = formatted.str();
    return true;
}

bool validate_animation_authoring(const FbxDocument& document, std::string& error)
{
    const auto& rig = document.skeleton_authoring;
    const bool has_rig = !rig.sockets.empty() || !rig.aim_ik.empty() || !rig.two_bone_ik.empty();
    if (has_rig)
    {
        if (!document.external_skeleton.empty() || document.joints.empty())
        {
            error = "FBX rig definitions require the source-owned generated skeleton";
            return false;
        }
        schema::SkeletonDataT skeleton;
        skeleton.schema_version = 2;
        skeleton.path = "authoring/skeleton";
        if (!convert_skeleton(document, skeleton, error) ||
            !SkeletonResourceCodec::validate(skeleton))
        {
            if (error.empty())
            {
                error = "Invalid FBX socket or IK definition for this skeleton";
            }
            return false;
        }
    }
    for (const auto& [key, authored] : document.animation_authoring)
    {
        const auto found = document.animations.find(key);
        if (found == document.animations.end())
        {
            error = "FBX animation_events references an unknown animation key: " + key;
            return false;
        }
        const double duration = found->second->time_end - found->second->time_begin;
        std::set<std::uint32_t> ids;
        for (const auto& marker : authored.events)
        {
            if (marker->time > static_cast<float>(duration) ||
                !ids.insert(marker->marker_id).second)
            {
                error = "FBX event time exceeds the clip or its marker_id is duplicated: " + key;
                return false;
            }
        }
    }
    return true;
}
} // namespace engine::plugins::asset_import
