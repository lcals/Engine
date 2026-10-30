#include "editor/asset_pipeline/components/scene_component_codec.hpp"

#include "editor/asset_pipeline/components/camera_codec.hpp"
#include "editor/asset_pipeline/components/environment_codec.hpp"
#include "editor/asset_pipeline/components/light_codec.hpp"
#include "editor/asset_pipeline/components/mesh_renderer_codec.hpp"
#include "editor/asset_pipeline/components/post_process_codec.hpp"
#include "editor/asset_pipeline/components/transform_codec.hpp"

#include "editor/asset_pipeline/components/audio_listener_codec.hpp"
#include "editor/asset_pipeline/components/audio_source_codec.hpp"
#include "editor/asset_pipeline/components/collider_codec.hpp"
#include "editor/asset_pipeline/components/rigid_body_codec.hpp"

#include "editor/asset_pipeline/components/animator_codec.hpp"

#include "editor/asset_pipeline/components/character_motor_codec.hpp"

#include "editor/asset_pipeline/components/text_codec.hpp"
#include "editor/asset_pipeline/components/ui_codec.hpp"

#include <array>
#include <vector>

namespace engine::editor::asset_pipeline
{
namespace
{

std::vector<std::unique_ptr<SceneComponentCodec>>& extensions()
{
    static std::vector<std::unique_ptr<SceneComponentCodec>> entries;
    return entries;
}

const std::array<const SceneComponentCodec*, 14>& codecs()
{
    static const UiCodec ui;
    static const TextCodec text;
    static const AnimatorCodec animator;
    static const CharacterMotorCodec motor;
    static const TransformCodec transform;
    static const CameraCodec camera;
    static const LightCodec light;
    static const MeshRendererCodec mesh;
    static const ColliderCodec collider;
    static const RigidBodyCodec rigid_body;
    static const AudioSourceCodec audio_source;
    static const AudioListenerCodec audio_listener;
    static const EnvironmentCodec environment;
    static const PostProcessCodec post_process;

    static const std::array<const SceneComponentCodec*, 14> entries{
        &ui,   &text,     &motor,      &animator,     &transform,      &camera,      &light,
        &mesh, &collider, &rigid_body, &audio_source, &audio_listener, &environment, &post_process};
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
    for (const auto& codec : extensions())
    {
        if (codec->type() == type)
        {
            return codec.get();
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
    for (const auto& codec : extensions())
    {
        if (codec->name() == name)
        {
            return codec.get();
        }
    }
    return nullptr;
}

bool register_scene_component_codec(std::unique_ptr<SceneComponentCodec>& codec)
{
    if (!codec || codec->type() == schema::ComponentPayload::NONE || codec->name().empty() ||
        find_scene_component_codec(codec->type()) || find_scene_component_codec(codec->name()))
    {
        return false;
    }
    extensions().push_back(std::move(codec));
    return true;
}

} // namespace engine::editor::asset_pipeline
