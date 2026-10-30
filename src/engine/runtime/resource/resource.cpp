#include "runtime/resource/resource.hpp"

namespace engine
{

Resource::~Resource() noexcept = default;

const schema::AssetEntryT& Resource::info() const noexcept
{
    return info_;
}

std::uint64_t Resource::generation() const noexcept
{
    return generation_;
}

const schema::SceneDataT* Resource::scene() const noexcept
{
    return scene_.get();
}

const schema::MeshDataT* Resource::mesh() const noexcept
{
    return mesh_.get();
}

const schema::ShaderDataT* Resource::shader() const noexcept
{
    return shader_.get();
}

const schema::MaterialDataT* Resource::material() const noexcept
{
    return material_.get();
}

const schema::TextureDataT* Resource::texture() const noexcept
{
    return texture_.get();
}

const schema::AudioClipDataT* Resource::audio() const noexcept
{
    return audio_.get();
}

const schema::ModelDataT* Resource::model() const noexcept
{
    return model_.get();
}

const schema::SkeletonDataT* Resource::skeleton() const noexcept
{
    return skeleton_.get();
}

const schema::AnimationDataT* Resource::animation() const noexcept
{
    return animation_.get();
}

const schema::FontDataT* Resource::font() const noexcept
{
    return font_.get();
}

const schema::UiDocumentDataT* Resource::ui() const noexcept
{
    return ui_.get();
}

ByteView Resource::bytes() const noexcept
{
    return bytes_;
}

} // namespace engine
