#pragma once

#include "runtime/resource/codecs/animation_resource_codec.hpp"
#include "runtime/resource/codecs/animation_retarget_resource_codec.hpp"
#include "runtime/resource/codecs/audio_resource_codec.hpp"
#include "runtime/resource/codecs/font_resource_codec.hpp"
#include "runtime/resource/codecs/json_resource_codec.hpp"
#include "runtime/resource/codecs/light_probe_resource_codec.hpp"
#include "runtime/resource/codecs/material_resource_codec.hpp"
#include "runtime/resource/codecs/mesh_resource_codec.hpp"
#include "runtime/resource/codecs/model_resource_codec.hpp"
#include "runtime/resource/codecs/movie_resource_codec.hpp"
#include "runtime/resource/codecs/prefab_resource_codec.hpp"
#include "runtime/resource/codecs/scene_resource_codec.hpp"
#include "runtime/resource/codecs/script_resource_codec.hpp"
#include "runtime/resource/codecs/shader_resource_codec.hpp"
#include "runtime/resource/codecs/skeleton_resource_codec.hpp"
#include "runtime/resource/codecs/source_resource_codec.hpp"
#include "runtime/resource/codecs/texture_resource_codec.hpp"
#include "runtime/resource/codecs/ui_resource_codec.hpp"
#include "runtime/resource/resource_system.hpp"
#if ENGINE_HAS_EFFECTS_PLUGIN
#include "plugins/runtime/effects/codecs/effect_resource_codec.hpp"
#endif
#if ENGINE_HAS_TERRAIN_PLUGIN
#include "plugins/runtime/terrain/codecs/terrain_resource_codec.hpp"
#endif
#if ENGINE_HAS_DESTRUCTION_PLUGIN
#include "plugins/runtime/destruction/codecs/destruction_resource_codec.hpp"
#endif

namespace
{
bool register_runtime_resource_codecs(engine::ResourceSystem& resources)
{
    if (!resources.add<engine::SourceResourceCodec>() ||
        !resources.add<engine::ScriptResourceCodec>() ||
        !resources.add<engine::JsonResourceCodec>() ||
        !resources.add<engine::SceneResourceCodec>() ||
        !resources.add<engine::PrefabResourceCodec>() ||
        !resources.add<engine::MeshResourceCodec>() ||
        !resources.add<engine::ShaderResourceCodec>() ||
        !resources.add<engine::MaterialResourceCodec>() ||
        !resources.add<engine::TextureResourceCodec>() ||
        !resources.add<engine::AudioResourceCodec>() ||
        !resources.add<engine::MovieResourceCodec>() ||
        !resources.add<engine::FontResourceCodec>() || !resources.add<engine::UiResourceCodec>() ||
        !resources.add<engine::LightProbeResourceCodec>() ||
        !resources.add<engine::ModelResourceCodec>() ||
        !resources.add<engine::SkeletonResourceCodec>() ||
        !resources.add<engine::AnimationResourceCodec>() ||
        !resources.add<engine::AnimationRetargetResourceCodec>())
    {
        return false;
    }
#if ENGINE_HAS_EFFECTS_PLUGIN
    if (!resources.add<engine::plugins::effects::EffectResourceCodec>())
    {
        return false;
    }
#endif
#if ENGINE_HAS_TERRAIN_PLUGIN
    if (!resources.add<engine::plugins::terrain::TerrainResourceCodec>())
    {
        return false;
    }
#endif
#if ENGINE_HAS_DESTRUCTION_PLUGIN
    if (!resources.add<engine::plugins::destruction::DestructionResourceCodec>())
    {
        return false;
    }
#endif
    return true;
}
} // namespace
