#pragma once

#include "runtime/scene/scene_types.hpp"

#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace engine
{

namespace schema
{
struct ShaderDataT;
}

class MeshRenderer;
class Resource;

class Material final
{
public:
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&&) = delete;
    Material& operator=(Material&&) = delete;

    [[nodiscard]] std::string_view type_path() const noexcept;
    [[nodiscard]] std::string_view shader_path() const noexcept;
    // 借用材质当前持有的 Shader 定义；切换 Shader 或销毁材质后失效。
    [[nodiscard]] const schema::ShaderDataT* shader_data() const noexcept;
    // 材质资源路径；换型重新合并 Shader 默认值与材质覆盖值，同型保持当前值。
    [[nodiscard]] bool set_type(std::string_view path);
    // Shader 资源路径；切换后采用新 Shader 默认值，同路径保留已有参数。
    [[nodiscard]] bool set_shader(std::string_view path);
    [[nodiscard]] bool set_float(std::string_view name, float value) noexcept;
    [[nodiscard]] bool set_v2(std::string_view name, const Vec2& value) noexcept;
    [[nodiscard]] bool set_v3(std::string_view name, const Vec3& value) noexcept;
    [[nodiscard]] bool set_v4(std::string_view name, const Vec4& value) noexcept;
    [[nodiscard]] bool set_texture(std::string_view name, std::string_view path);
    // 输出借用当前资源路径；失败输出空视图。
    [[nodiscard]] bool get_texture(std::string_view name, std::string_view& output) const noexcept;
    // 借用共享引用，未知名称返回空引用；修改材质后重新查询。
    [[nodiscard]] const std::shared_ptr<const Resource>&
    texture(std::string_view name) const noexcept;
    // 查询失败时输出零值。
    [[nodiscard]] bool get_float(std::string_view name, float& output) const noexcept;
    [[nodiscard]] bool get_v2(std::string_view name, Vec2& output) const noexcept;
    [[nodiscard]] bool get_v3(std::string_view name, Vec3& output) const noexcept;
    [[nodiscard]] bool get_v4(std::string_view name, Vec4& output) const noexcept;

private:
    friend class MeshRenderer;
    Material() noexcept;
    void swap(Material& other) noexcept;

    [[nodiscard]] schema::MaterialParameterT* find_parameter(std::string_view name,
                                                             schema::MaterialValue type) noexcept;
    [[nodiscard]] const schema::MaterialParameterT*
    find_parameter(std::string_view name, schema::MaterialValue type) const noexcept;

    std::shared_ptr<const Resource> material_resource_;
    std::shared_ptr<const Resource> shader_resource_;
    schema::MaterialDataT data_;
    std::map<std::string, std::shared_ptr<const Resource>, std::less<>> textures_;
};

} // namespace engine
