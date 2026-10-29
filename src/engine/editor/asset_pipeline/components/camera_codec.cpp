#include "editor/asset_pipeline/components/camera_codec.hpp"

#include "runtime/core/guid_map.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace engine::asset_pipeline
{

schema::ComponentPayload CameraCodec::type() const noexcept
{
    return schema::ComponentPayload::CameraData;
}

std::string_view CameraCodec::name() const noexcept
{
    return "camera";
}

bool CameraCodec::validate(const schema::ComponentDataT& component, std::string& error) const
{

    const auto* value = component.payload.AsCameraData();
    if (value == nullptr || !std::isfinite(value->fov_degrees) ||
        !std::isfinite(value->near_clip) || !std::isfinite(value->far_clip) ||
        value->fov_degrees <= 0 || value->fov_degrees >= 180 || value->near_clip <= 0 ||
        value->far_clip <= value->near_clip || value->projection < schema::Projection::MIN ||
        value->projection > schema::Projection::MAX || !std::isfinite(value->orthographic_size) ||
        value->orthographic_size <= 0)
    {
        error = "invalid scene camera";
        return false;
    }

    return true;
}

bool CameraCodec::read(const toml::table& table, schema::ComponentDataT& component,
                       std::string& error) const
{

    schema::CameraDataT value;
    if ((table.contains("fov_degrees") &&
         !float_number(table.get("fov_degrees"), value.fov_degrees)) ||
        (table.contains("near_clip") && !float_number(table.get("near_clip"), value.near_clip)) ||
        (table.contains("far_clip") && !float_number(table.get("far_clip"), value.far_clip)) ||
        (table.contains("orthographic_size") &&
         !float_number(table.get("orthographic_size"), value.orthographic_size)))
    {
        error = "camera fields must be finite numbers";
        return false;
    }
    if (table.contains("projection"))
    {
        const auto projection = table["projection"].value<std::string_view>();
        if (projection != "perspective" && projection != "orthographic")
        {
            error = "camera projection must be perspective or orthographic";
            return false;
        }
        value.projection = projection == "perspective" ? schema::Projection::perspective
                                                       : schema::Projection::orthographic;
    }
    component.payload.Set(std::move(value));

    return true;
}

void CameraCodec::write(const schema::ComponentDataT& component, toml::table& value) const
{

    const auto* camera = component.payload.AsCameraData();
    value.insert("type", "camera");
    value.insert("fov_degrees", camera->fov_degrees);
    value.insert("near_clip", camera->near_clip);
    value.insert("far_clip", camera->far_clip);
    value.insert("projection", schema::EnumNameProjection(camera->projection));
    value.insert("orthographic_size", camera->orthographic_size);
}

} // namespace engine::asset_pipeline
