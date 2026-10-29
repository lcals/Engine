#include "editor/asset_pipeline/components/scene_component_codec.hpp"

#include "editor/asset_pipeline/components/camera_codec.hpp"
#include "editor/asset_pipeline/components/light_codec.hpp"
#include "editor/asset_pipeline/components/mesh_renderer_codec.hpp"
#include "editor/asset_pipeline/components/transform_codec.hpp"

#include "editor/asset_pipeline/components/audio_listener_codec.hpp"
#include "editor/asset_pipeline/components/audio_source_codec.hpp"
#include "editor/asset_pipeline/components/collider_codec.hpp"
#include "editor/asset_pipeline/components/rigid_body_codec.hpp"

#include <array>

namespace engine::editor::asset_pipeline
{
namespace
{

const std::array<const SceneComponentCodec*, 8>& codecs()
{
    static const TransformCodec transform;
    static const CameraCodec camera;
    static const LightCodec light;
    static const MeshRendererCodec mesh;
    static const ColliderCodec collider;
    static const RigidBodyCodec rigid_body;
    static const AudioSourceCodec audio_source;
    static const AudioListenerCodec audio_listener;

    static const std::array<const SceneComponentCodec*, 8> entries{
        &transform, &camera, &light, &mesh, &collider, &rigid_body, &audio_source, &audio_listener};
    return entries;
}

} // namespace

const SceneComponentCodec* find_scene_component_codec(schema::ComponentPayload type) noexcept
{
    for (const auto* codec : codecs())
    {
        if (codec->type() == type)
        {
            return codec;
        }
    }
    return nullptr;
}

const SceneComponentCodec* find_scene_component_codec(std::string_view name) noexcept
{
    for (const auto* codec : codecs())
    {
        if (codec->name() == name)
        {
            return codec;
        }
    }
    return nullptr;
}

} // namespace engine::editor::asset_pipeline
