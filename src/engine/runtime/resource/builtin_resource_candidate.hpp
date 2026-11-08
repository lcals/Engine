#pragma once

#include "runtime/resource/resource.hpp"

namespace engine::resource_detail
{
// 宿主显式构造内存候选；不读取文件，不登记或加载资源。
[[nodiscard]] bool builtin_candidate(std::string_view guid, std::unique_ptr<Resource>& output);
} // namespace engine::resource_detail
