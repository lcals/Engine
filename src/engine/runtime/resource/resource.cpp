#include "runtime/resource/resource.hpp"

namespace engine
{

Resource::~Resource() noexcept = default;

const schema::AssetEntryT& Resource::info() const noexcept
{
    return info_;
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

ByteView Resource::bytes() const noexcept
{
    return bytes_;
}

} // namespace engine
