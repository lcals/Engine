#pragma once

#include "runtime/scene/component.hpp"
#include "runtime/scene/material.hpp"
#include "runtime/schema/mesh_generated.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace engine
{

class Resource;

class MeshRenderer final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::MeshRendererData;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;
    [[nodiscard]] bool build_data(schema::ComponentDataT& output) const override;

    [[nodiscard]] std::string_view mesh_path() const noexcept;
    [[nodiscard]] bool set_mesh(std::string_view path);
    // 完整校验后提交独立瞬时网格，失败不改变已有几何和材质槽。
    [[nodiscard]] bool set_mesh_data(const schema::MeshDataT& data);
    [[nodiscard]] const schema::MeshDataT* mesh_data() const noexcept;
    [[nodiscard]] std::uint64_t mesh_revision() const noexcept;
    [[nodiscard]] Material& material() noexcept;
    [[nodiscard]] const Material& material() const noexcept;
    // 按槽借用；越界返回空，槽数量为 1..16，缩容不能丢弃被子网格使用的槽。
    [[nodiscard]] Material* material(std::size_t slot) noexcept;
    [[nodiscard]] const Material* material(std::size_t slot) const noexcept;
    [[nodiscard]] std::size_t material_count() const noexcept;
    [[nodiscard]] bool set_material_count(std::size_t count);

private:
    friend class Node;
    explicit MeshRenderer(Node& node);
    [[nodiscard]] bool initialize() override;

    std::shared_ptr<const Resource> mesh_resource_;
    Material material_;
    std::vector<std::unique_ptr<Material>> additional_materials_;
    std::unique_ptr<schema::MeshDataT> dynamic_mesh_;
    std::uint64_t mesh_revision_ = 0;
};

} // namespace engine
