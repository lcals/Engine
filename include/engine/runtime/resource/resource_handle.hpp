#pragma once

#include <cstdint>

namespace engine
{

// 一次获取的身份；按值复制不增加引用，释放后全部副本失效。
struct ResourceHandle
{
    std::uint64_t owner = 0;
    std::uint64_t id = 0;
    std::uint64_t generation = 0;
};

enum class ResourceError : std::uint8_t
{
    none,
    unavailable,
    not_found,
    not_loaded,
    not_ready,
    invalid_handle,
    type_mismatch,
    unsupported_type,
    busy,
    budget_exceeded,
    io_failed,
    invalid_data,
    dependency_failed,
    cancelled,
    generation_changed,
    identity_conflict
};

} // namespace engine
