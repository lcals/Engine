#pragma once

#include "runtime/scene/component.hpp"
#include "runtime/scene/material.hpp"
#include "runtime/schema/mesh_generated.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace engine
{

class Resource;

struct MeshRendererRuntime
{
    std::shared_ptr<const Resource> mesh_resource_;
    std::shared_ptr<const Resource> lightmap_resource_;
    std::vector<MaterialState> materials_;
    std::uint64_t next_material_identity_ = 1;
    std::unique_ptr<schema::MeshDataT> dynamic_mesh_;
    std::vector<schema::MeshVertex> deformed_vertices_;
    std::vector<Vec4> deformed_tangents_;
    std::vector<std::array<float, 16>> skinning_palette_;
    std::vector<std::array<float, 16>> skinning_normal_palette_;
    std::uint32_t skin_joint_count_ = 0;
    std::uint64_t mesh_revision_ = 0;
    std::uint64_t deformation_revision_ = 0;
};

class MeshRenderer final : public Component
{
public:
    using Data = schema::MeshRendererDataT;
    using Runtime = MeshRendererRuntime;
    MeshRenderer() noexcept = default;
    explicit MeshRenderer(Component component) noexcept : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    [[nodiscard]] bool initialize();
    [[nodiscard]] Runtime* runtime() const noexcept;
    static constexpr ComponentType component_type = ComponentType::MeshRendererData;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
    [[nodiscard]] bool build_data(schema::ComponentDataT& output) const;

    [[nodiscard]] std::string_view mesh_path() const noexcept;
    [[nodiscard]] bool set_mesh(std::string_view path);
    // 完整校验后提交独立瞬时网格，失败不改变已有几何和材质槽。
    [[nodiscard]] bool set_mesh_data(const schema::MeshDataT& data);
    [[nodiscard]] const schema::MeshDataT* mesh_data() const noexcept;
    [[nodiscard]] std::shared_ptr<const Resource> mesh_resource() const noexcept;
    [[nodiscard]] std::uint64_t mesh_revision() const noexcept;
    // 进程内稳定且不重复的组件实例身份，供瞬态缓存区分组件重建。
    // 瞬态绘制开关，不进入持久数据，不停用组件行为或动画。
    [[nodiscard]] bool cast_shadows() const noexcept;
    void set_cast_shadows(bool enabled) noexcept;
    [[nodiscard]] bool bake_static() const noexcept;
    [[nodiscard]] bool set_bake_static(bool enabled) noexcept;
    [[nodiscard]] std::string_view lightmap_path() const noexcept;
    [[nodiscard]] bool set_lightmap(std::string_view path);
    // 借用已校验姿态和网格版本的线性照明资源；绑定失效或不存在时为空。
    [[nodiscard]] std::shared_ptr<const Resource> lightmap_resource() const noexcept;
    // 瞬态顶点覆写，保留源资源身份、拓扑和材质；失败不改变已有变形。
    [[nodiscard]] bool set_deformation(std::span<const schema::MeshVertex> vertices,
                                       std::span<const Vec4> tangents);
    // 源骨骼索引顺序、mesh 局部空间、列主序；与直接顶点覆写互斥。
    [[nodiscard]] bool
    set_skinning_palette(std::span<const std::array<float, 16>> transforms,
                         std::span<const std::array<float, 16>> normal_transforms);
    [[nodiscard]] std::span<const std::array<float, 16>> skinning_palette() const noexcept;
    [[nodiscard]] std::span<const std::array<float, 16>> skinning_normal_palette() const noexcept;
    void clear_deformation() noexcept;
    [[nodiscard]] std::span<const schema::MeshVertex> deformed_vertices() const noexcept;
    [[nodiscard]] std::span<const Vec4> deformed_tangents() const noexcept;
    [[nodiscard]] std::uint64_t deformation_revision() const noexcept;
    // 按槽返回身份句柄；槽删除后重建不会复活旧句柄。
    [[nodiscard]] Material material(std::size_t slot = 0) const noexcept;
    [[nodiscard]] std::size_t material_count() const noexcept;
    [[nodiscard]] bool set_material_count(std::size_t count);

private:
    friend class Material;
};

} // namespace engine
