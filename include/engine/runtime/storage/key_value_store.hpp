#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace engine
{

class KeyValueStore final
{
public:
    KeyValueStore() = default;
    ~KeyValueStore() = default;
    KeyValueStore(const KeyValueStore&) = delete;
    KeyValueStore& operator=(const KeyValueStore&) = delete;
    KeyValueStore(KeyValueStore&&) = delete;
    KeyValueStore& operator=(KeyValueStore&&) = delete;

    // 目录由调用方提供；缺少文件视为空存储，失败后禁止修改及保存。
    [[nodiscard]] bool load(const std::filesystem::path& directory);
    // 缺 key 返回 false，清空 value，last_error 保持为空。
    [[nodiscard]] bool get_string(std::string_view key, std::string& value);
    [[nodiscard]] bool set_string(std::string_view key, std::string_view value);
    [[nodiscard]] bool has_key(std::string_view key);
    [[nodiscard]] bool delete_key(std::string_view key);
    [[nodiscard]] bool save();
    [[nodiscard]] bool is_loaded() const noexcept;
    [[nodiscard]] const std::string& last_error() const noexcept;

private:
    std::filesystem::path directory_;
    std::map<std::string, std::string, std::less<>> values_;
    std::string last_error_;
    bool loaded_ = false;
};

} // namespace engine
