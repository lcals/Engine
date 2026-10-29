#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

namespace engine
{

using Guid = std::string;

class GuidMap final
{
public:
    [[nodiscard]] static bool valid_guid(std::string_view guid) noexcept;
    // 逻辑路径和GUID均唯一；失败不修改已登记映射。
    [[nodiscard]] bool add(std::string_view path, std::string_view guid);
    // 失败清空输出；不访问文件系统。
    [[nodiscard]] bool path_to_guid(std::string_view path, Guid& output) const;
    [[nodiscard]] bool guid_to_path(std::string_view guid, std::string& output) const;
    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::map<std::string, Guid, std::less<>> paths_;
    std::map<Guid, std::string, std::less<>> guids_;
};

} // namespace engine
