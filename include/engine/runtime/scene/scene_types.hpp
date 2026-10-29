#pragma once

#include <cstdint>

namespace engine
{

struct Vec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Transform
{
    Vec3 position{};
    Vec3 rotation{}; // 欧拉角，单位为度；局部旋转次序 X、Y、Z。
    Vec3 scale{1.0, 1.0, 1.0};
};

struct Color
{
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
};

struct CameraSettings
{
    float fov_degrees = 60.0F;
    float near_clip = 0.1F;
    float far_clip = 1000.0F;
};

struct LightSettings
{
    Color color{};
    float intensity = 3.0F;
};

struct Material
{
    Color base_color{0.7F, 0.3F, 0.15F, 1.0F};
    float metallic = 0.0F;
    float roughness = 0.4F;
    bool unlit = false;
};

enum class ComponentType : std::uint8_t
{
    camera,
    light,
    mesh_renderer
};

enum class MeshKind : std::uint8_t
{
    cube
};

} // namespace engine
