#pragma once

#include "runtime/resource/resource.hpp"

#include <string>
#include <string_view>

namespace engine
{

// 宿主选择加载实现；核心消费者不依赖插件的具体类型。
class ResourceLoader
{
public:
    virtual ~ResourceLoader() noexcept = default;

    [[nodiscard]] virtual bool load(std::string_view guid, ResourcePtr& output) = 0;
    // 只查询当前代际的已有引用，不读取文件；未驻留时返回空。
    [[nodiscard]] virtual ResourcePtr find(std::string_view guid) const noexcept = 0;
    [[nodiscard]] virtual const std::string& last_error() const noexcept = 0;
};

} // namespace engine
