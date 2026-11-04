#include "runtime/resource/codecs/font_resource_codec.hpp"
#include "runtime/resource/codecs/light_probe_resource_codec.hpp"
#include "runtime/resource/codecs/ui_resource_codec.hpp"
#include "runtime/resource/resource_codec.hpp"

#include "runtime/resource/codecs/audio_resource_codec.hpp"
#include "runtime/resource/codecs/binary_resource_codec.hpp"
#include "runtime/resource/codecs/material_resource_codec.hpp"
#include "runtime/resource/codecs/mesh_resource_codec.hpp"
#include "runtime/resource/codecs/prefab_resource_codec.hpp"
#include "runtime/resource/codecs/scene_resource_codec.hpp"
#include "runtime/resource/codecs/shader_resource_codec.hpp"
#include "runtime/resource/codecs/texture_resource_codec.hpp"

#include "runtime/resource/codecs/model_resource_codec.hpp"

#include "runtime/resource/codecs/skeleton_resource_codec.hpp"

#include "runtime/resource/codecs/animation_resource_codec.hpp"
#include "runtime/resource/codecs/animation_retarget_resource_codec.hpp"

#include <algorithm>
#include <array>

namespace engine
{
namespace
{

const ModelResourceCodec model;
const SkeletonResourceCodec skeleton;
const AnimationResourceCodec animation;
const AnimationRetargetResourceCodec animation_retarget;
const BinaryResourceCodec binary;
const AudioResourceCodec audio;
const MaterialResourceCodec material;
const MeshResourceCodec mesh;
const SceneResourceCodec scene;
const PrefabResourceCodec prefab;
const ShaderResourceCodec shader;
const TextureResourceCodec texture;
const FontResourceCodec font;
const UiResourceCodec ui;
const LightProbeResourceCodec light_probe;

struct CodecEntry
{
    schema::AssetKind kind;
    const ResourceCodec* codec;
};

const std::array registrations = {
    CodecEntry{schema::AssetKind::scene, &scene},
    CodecEntry{schema::AssetKind::prefab, &prefab},
    CodecEntry{schema::AssetKind::mesh, &mesh},
    CodecEntry{schema::AssetKind::shader, &shader},
    CodecEntry{schema::AssetKind::material, &material},
    CodecEntry{schema::AssetKind::texture, &texture},
    CodecEntry{schema::AssetKind::script, &binary},
    CodecEntry{schema::AssetKind::source, &binary},
    CodecEntry{schema::AssetKind::model, &model},
    CodecEntry{schema::AssetKind::skeleton, &skeleton},
    CodecEntry{schema::AssetKind::animation, &animation},
    CodecEntry{schema::AssetKind::animation_retarget_profile, &animation_retarget},
    CodecEntry{schema::AssetKind::audio, &audio},
    CodecEntry{schema::AssetKind::font, &font},
    CodecEntry{schema::AssetKind::ui, &ui},
    CodecEntry{schema::AssetKind::effect, &binary},
    CodecEntry{schema::AssetKind::terrain, &binary},
    CodecEntry{schema::AssetKind::destruction, &binary},
    CodecEntry{schema::AssetKind::json, &binary},
    CodecEntry{schema::AssetKind::light_probe, &light_probe},
};

} // namespace

const ResourceCodec* find_resource_codec(schema::AssetKind kind) noexcept
{
    const auto found = std::find_if(registrations.begin(), registrations.end(),
                                    [kind](const auto& item) { return item.kind == kind; });
    return found != registrations.end() ? found->codec : nullptr;
}

} // namespace engine
