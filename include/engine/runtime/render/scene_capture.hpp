#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace engine
{

namespace schema
{
struct TextureDataT;
struct MeshDataT;
} // namespace schema

enum class SceneCaptureStatus : std::uint8_t
{
    idle,
    pending,
    complete,
    cancelled,
    failed
};

struct SceneCaptureLightmap
{
    std::uint64_t node_id = 0;
    std::shared_ptr<const schema::TextureDataT> image;
    std::shared_ptr<const schema::MeshDataT> mesh;
};

// 接受请求时复制描述；中间光照纹理只读共享，不依赖编辑器或临时资源目录。
struct SceneCaptureRequest
{
    std::array<float, 16> view{};
    std::array<float, 16> projection{};
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::vector<std::uint64_t> node_ids;
    std::vector<std::uint64_t> light_node_ids;
    std::vector<SceneCaptureLightmap> indirect;
};

struct SceneCaptureImage
{
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::vector<float> rgba;
};

} // namespace engine
