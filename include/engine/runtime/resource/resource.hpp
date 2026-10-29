#pragma once

#include "runtime/schema/asset_catalog_generated.h"
#include "runtime/schema/material_generated.h"
#include "runtime/schema/mesh_generated.h"
#include "runtime/schema/scene_generated.h"
#include "runtime/schema/shader_generated.h"
#include "runtime/schema/texture_generated.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace engine
{

class Resource;
class ResourceSystem;
using ResourcePtr = std::shared_ptr<const Resource>;
using ByteView = std::span<const std::byte>;

class Resource final
{
public:
    ~Resource() noexcept;
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
    Resource(Resource&&) = delete;
    Resource& operator=(Resource&&) = delete;

    [[nodiscard]] const schema::AssetEntryT& info() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] const schema::SceneDataT* scene() const noexcept;
    [[nodiscard]] const schema::MeshDataT* mesh() const noexcept;
    [[nodiscard]] const schema::ShaderDataT* shader() const noexcept;
    [[nodiscard]] const schema::MaterialDataT* material() const noexcept;
    [[nodiscard]] const schema::TextureDataT* texture() const noexcept;
    [[nodiscard]] ByteView bytes() const noexcept;

private:
    friend class ResourceSystem;
    Resource() noexcept = default;

    std::uint64_t generation_ = 0;
    schema::AssetEntryT info_;
    std::unique_ptr<schema::SceneDataT> scene_;
    std::unique_ptr<schema::MeshDataT> mesh_;
    std::unique_ptr<schema::ShaderDataT> shader_;
    std::unique_ptr<schema::MaterialDataT> material_;
    std::unique_ptr<schema::TextureDataT> texture_;
    std::vector<std::byte> bytes_;
};

} // namespace engine
