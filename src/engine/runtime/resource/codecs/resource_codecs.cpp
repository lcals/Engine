#include "runtime/resource/resource_codec.hpp"

#include "runtime/resource/codecs/audio_resource_codec.hpp"
#include "runtime/resource/codecs/binary_resource_codec.hpp"
#include "runtime/resource/codecs/material_resource_codec.hpp"
#include "runtime/resource/codecs/mesh_resource_codec.hpp"
#include "runtime/resource/codecs/scene_resource_codec.hpp"
#include "runtime/resource/codecs/shader_resource_codec.hpp"
#include "runtime/resource/codecs/texture_resource_codec.hpp"

#include <algorithm>
#include <array>

namespace engine
{
namespace
{

const BinaryResourceCodec binary;
const AudioResourceCodec audio;
const MaterialResourceCodec material;
const MeshResourceCodec mesh;
const SceneResourceCodec scene;
const ShaderResourceCodec shader;
const TextureResourceCodec texture;

struct CodecEntry
{
    schema::AssetKind kind;
    const ResourceCodec* codec;
};

const std::array registrations = {
    CodecEntry{schema::AssetKind::scene, &scene},
    CodecEntry{schema::AssetKind::mesh, &mesh},
    CodecEntry{schema::AssetKind::shader, &shader},
    CodecEntry{schema::AssetKind::material, &material},
    CodecEntry{schema::AssetKind::texture, &texture},
    CodecEntry{schema::AssetKind::script, &binary},
    CodecEntry{schema::AssetKind::source, &binary},
    CodecEntry{schema::AssetKind::audio, &audio},
};

} // namespace

const ResourceCodec* find_resource_codec(schema::AssetKind kind) noexcept
{
    const auto found = std::find_if(registrations.begin(), registrations.end(),
                                    [kind](const auto& item) { return item.kind == kind; });
    return found != registrations.end() ? found->codec : nullptr;
}

} // namespace engine
