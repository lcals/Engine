#pragma once

#include "runtime/scene/scene_types.hpp"

#include <string_view>

namespace engine
{

class MeshRenderer;

class Material final
{
public:
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&&) = delete;
    Material& operator=(Material&&) = delete;

    [[nodiscard]] std::string_view type_path() const noexcept;
    [[nodiscard]] std::string_view shader_path() const noexcept;
    // 材质资源路径；换型重新合并 Shader 默认值与材质覆盖值，同型保持当前值。
    [[nodiscard]] bool set_type(std::string_view path);
    // Shader 资源路径；切换后采用新 Shader 默认值，同路径保留已有参数。
    [[nodiscard]] bool set_shader(std::string_view path);
    [[nodiscard]] bool set_float(std::string_view name, float value) noexcept;
    [[nodiscard]] bool set_v2(std::string_view name, const Vec2& value) noexcept;
    [[nodiscard]] bool set_v3(std::string_view name, const Vec3& value) noexcept;
    [[nodiscard]] bool set_v4(std::string_view name, const Vec4& value) noexcept;
    // 查询失败时输出零值。
    [[nodiscard]] bool get_float(std::string_view name, float& output) const noexcept;
    [[nodiscard]] bool get_v2(std::string_view name, Vec2& output) const noexcept;
    [[nodiscard]] bool get_v3(std::string_view name, Vec3& output) const noexcept;
    [[nodiscard]] bool get_v4(std::string_view name, Vec4& output) const noexcept;

private:
    friend class MeshRenderer;
    explicit Material(schema::MaterialDataT& data) noexcept;

    [[nodiscard]] schema::MaterialParameterT* find_parameter(std::string_view name,
                                                             schema::MaterialValue type) noexcept;
    [[nodiscard]] const schema::MaterialParameterT*
    find_parameter(std::string_view name, schema::MaterialValue type) const noexcept;

    schema::MaterialDataT& data_;
};

} // namespace engine
