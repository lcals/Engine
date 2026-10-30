#pragma once

#include "runtime/scene/component.hpp"
#include "runtime/scene/material.hpp"
#include "runtime/schema/mesh_generated.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
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
    // 进程内稳定且不重复的组件实例身份，供瞬态缓存区分组件重建。
    [[nodiscard]] std::uint64_t mesh_instance_id() const noexcept;
    [[nodiscard]] bool visible() const noexcept;
    // 瞬态绘制开关，不进入持久数据，不停用组件行为或动画。
    void set_visible(bool visible) noexcept;
    [[nodiscard]] bool cast_shadows() const noexcept;
    void set_cast_shadows(bool enabled) noexcept;
    // 瞬态顶点覆写，保留源资源身份、拓扑和材质；失败不改变已有变形。
    [[nodiscard]] bool set_deformation(std::span<const schema::MeshVertex> vertices,
                                       std::span<const Vec4> tangents);
    void clear_deformation() noexcept;
    [[nodiscard]] std::span<const schema::MeshVertex> deformed_vertices() const noexcept;
    [[nodiscard]] std::span<const Vec4> deformed_tangents() const noexcept;
    [[nodiscard]] std::uint64_t deformation_revision() const noexcept;
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
    std::vector<schema::MeshVertex> deformed_vertices_;
    std::vector<Vec4> deformed_tangents_;
    const std::uint64_t mesh_instance_id_;
    bool visible_ = true;
    std::uint64_t mesh_revision_ = 0;
    std::uint64_t deformation_revision_ = 0;
};

} // namespace engine
