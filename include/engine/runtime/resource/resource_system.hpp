#pragma once

#include "runtime/schema/scene_generated.h"
#include "runtime/system/system.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace engine
{

class ResourceSystem final : public System
{
public:
    ResourceSystem() noexcept;
    ~ResourceSystem() noexcept override;

    // 同步读取普通文件或 Android 的 apk:/assets/ 路径；上限 64 MiB，失败清空输出。
    // 不要求系统初始化；APK 读取要求宿主的 SDL Android 环境已就绪。
    [[nodiscard]] static bool read_binary(const std::filesystem::path& path,
                                          std::vector<std::byte>& output);
    // 保留原始字节及 NUL，不转码；空文件成功。
    [[nodiscard]] static bool read_text(const std::filesystem::path& path, std::string& output);

    // 注册表为单宿主、主线程资源；成功时深拷贝。仅接受 builtin/ 与 project/ 路径。
    // 同一完整路径在 Shader 和材质之间唯一；内置材质不能引用项目 Shader。
    [[nodiscard]] static bool register_shader(const schema::ShaderDataT& data);
    [[nodiscard]] static bool register_material(const schema::MaterialDataT& data);
    // 返回只读借用，不得修改或转移其嵌套数据；注册不替换已有资源。
    // 全部清理使所有借用失效，项目清理仅使 project/ 借用失效。
    [[nodiscard]] static const schema::ShaderDataT* find_shader(std::string_view path) noexcept;
    [[nodiscard]] static const schema::MaterialDataT* find_material(std::string_view path) noexcept;
    // 所有使用这些定义的场景和 GPU 资源必须先释放。
    static void clear_project_definitions() noexcept;
    static void clear_definitions() noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;
};

} // namespace engine
