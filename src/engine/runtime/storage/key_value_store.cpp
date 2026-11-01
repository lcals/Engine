#include "runtime/storage/key_value_store.hpp"

#include "runtime/storage/storage_file.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <system_error>
#include <utility>

namespace engine
{
namespace
{

using Json = nlohmann::json;
using Values = std::map<std::string, std::string, std::less<>>;
constexpr std::size_t maximum_file_size = 4U * 1024U * 1024U;
constexpr std::size_t maximum_key_size = 256;
constexpr const char* file_name = "kv.json";
constexpr const char* temporary_name = "kv.json.tmp";

bool fail(std::string& error, std::string_view message)
{
    error = message;
    return false;
}

bool not_loaded(std::string& error)
{
    if (error.empty())
    {
        error = "KV store has not been loaded";
    }
    return false;
}

bool valid_utf8(std::string_view text)
{
    for (std::size_t index = 0; index < text.size();)
    {
        const auto first = static_cast<unsigned char>(text[index++]);
        if (first < 0x80)
        {
            continue;
        }
        std::uint32_t value = 0;
        std::uint32_t minimum = 0;
        unsigned int count = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            value = first & 0x1fU;
            minimum = 0x80;
            count = 1;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            value = first & 0x0fU;
            minimum = 0x800;
            count = 2;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            value = first & 0x07U;
            minimum = 0x10000;
            count = 3;
        }
        else
        {
            return false;
        }
        if (count > text.size() - index)
        {
            return false;
        }
        for (unsigned int remaining = 0; remaining < count; ++remaining)
        {
            const auto next = static_cast<unsigned char>(text[index++]);
            if ((next & 0xc0U) != 0x80U)
            {
                return false;
            }
            value = (value << 6U) | (next & 0x3fU);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
        {
            return false;
        }
    }
    return true;
}

bool valid_key(std::string_view key, std::string& error)
{
    if (key.empty() || key.size() > maximum_key_size || !valid_utf8(key))
    {
        return fail(error, "KV key must contain 1 to 256 bytes of valid UTF-8");
    }
    return true;
}

bool inspect_directory(const std::filesystem::path& directory, std::string& error)
{
    std::error_code code;
    const auto status = std::filesystem::symlink_status(directory, code);
    if (code == std::errc::no_such_file_or_directory ||
        (!code && status.type() == std::filesystem::file_type::not_found))
    {
        return true;
    }
    if (code || std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
    {
        return fail(error, "KV directory must be accessible and cannot be a symbolic link");
    }
    return true;
}

bool inspect_file(const std::filesystem::path& path, bool& exists, std::string& error)
{
    exists = false;
    std::error_code code;
    const auto status = std::filesystem::symlink_status(path, code);
    if (code == std::errc::no_such_file_or_directory ||
        (!code && status.type() == std::filesystem::file_type::not_found))
    {
        return true;
    }
    if (code || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
    {
        return fail(error, "KV files must be accessible regular files, not symbolic links");
    }
    exists = true;
    return true;
}

bool encode(const Values& values, std::string& text, std::string& error)
{
    Json document = Json::object();
    document["version"] = 1;
    auto& entries = document["values"] = Json::object();
    for (const auto& [key, value] : values)
    {
        if (!valid_key(key, error) || value.size() > maximum_file_size || !valid_utf8(value))
        {
            return fail(error, "KV contents require valid UTF-8 within the 4 MiB file limit");
        }
        entries[key] = value;
    }
    // Inputs are validated; replacement also prevents malformed UTF-8 from aborting a
    // JSON_NOEXCEPTION build if that invariant is accidentally broken in the future.
    text = document.dump(-1, ' ', false, Json::error_handler_t::replace);
    if (text.size() > maximum_file_size)
    {
        return fail(error, "KV contents exceed the 4 MiB file limit");
    }
    return true;
}

bool read_file(const std::filesystem::path& path, std::string& text, std::string& error)
{
    std::error_code code;
    const auto size = std::filesystem::file_size(path, code);
    if (code || size > maximum_file_size)
    {
        return fail(error, "Cannot read KV file or its size exceeds 4 MiB");
    }
    const auto encoded = PathUtils::to_utf8(path);
    auto* file = SDL_IOFromFile(encoded.c_str(), "rb");
    if (file == nullptr)
    {
        return fail(error, "Cannot open KV file for reading");
    }
    text.resize(static_cast<std::size_t>(size));
    const bool read = text.empty() || SDL_ReadIO(file, text.data(), text.size()) == text.size();
    char extra = 0;
    const bool complete =
        read && SDL_ReadIO(file, &extra, 1) == 0 && SDL_GetIOStatus(file) == SDL_IO_STATUS_EOF;
    const bool closed = SDL_CloseIO(file);
    if (!complete || !closed)
    {
        return fail(error, "Cannot read a complete and stable KV file");
    }
    return true;
}

void remove_temporary(const std::filesystem::path& path)
{
    std::error_code code;
    (void)std::filesystem::remove(path, code);
}

} // namespace

bool KeyValueStore::load(const std::filesystem::path& directory)
{
    loaded_ = false;
    directory_.clear();
    values_.clear();
    last_error_.clear();
    if (directory.empty())
    {
        return fail(last_error_, "KV directory cannot be empty");
    }
    std::error_code code;
    auto candidate_directory = std::filesystem::absolute(directory, code);
    if (code)
    {
        return fail(last_error_, "Cannot resolve KV directory");
    }
    candidate_directory = candidate_directory.lexically_normal();
    bool exists = false;
    bool temporary_exists = false;
    if (!inspect_directory(candidate_directory, last_error_) ||
        !inspect_file(candidate_directory / file_name, exists, last_error_) ||
        !inspect_file(candidate_directory / temporary_name, temporary_exists, last_error_))
    {
        return false;
    }
    Values candidate;
    if (exists)
    {
        std::string text;
        if (!read_file(candidate_directory / file_name, text, last_error_))
        {
            return false;
        }
        const auto document = Json::parse(text, nullptr, false);
        if (!document.is_object())
        {
            return fail(last_error_, "KV file must contain a versioned JSON object");
        }
        const auto version = document.find("version");
        const auto entries = document.find("values");
        if (version == document.end() || !version->is_number_integer() || *version != 1 ||
            entries == document.end() || !entries->is_object())
        {
            return fail(last_error_, "Unsupported KV version or invalid values object");
        }
        for (auto entry = entries->begin(); entry != entries->end(); ++entry)
        {
            if (!valid_key(entry.key(), last_error_) || !entry->is_string())
            {
                return fail(last_error_, "KV values must map valid UTF-8 keys to strings");
            }
            const auto& value = entry->get_ref<const std::string&>();
            if (!valid_utf8(value))
            {
                return fail(last_error_, "KV values must contain valid UTF-8");
            }
            candidate.emplace(entry.key(), value);
        }
    }
    directory_ = std::move(candidate_directory);
    values_ = std::move(candidate);
    loaded_ = true;
    return true;
}

bool KeyValueStore::get_string(std::string_view key, std::string& value)
{
    if (!loaded_)
    {
        value.clear();
        return not_loaded(last_error_);
    }
    last_error_.clear();
    if (!valid_key(key, last_error_))
    {
        value.clear();
        return false;
    }
    const auto found = values_.find(key);
    if (found == values_.end())
    {
        value.clear();
        return false;
    }
    value = found->second;
    return true;
}

bool KeyValueStore::set_string(std::string_view key, std::string_view value)
{
    if (!loaded_)
    {
        return not_loaded(last_error_);
    }
    last_error_.clear();
    if (!valid_key(key, last_error_))
    {
        return false;
    }
    if (value.size() > maximum_file_size || !valid_utf8(value))
    {
        return fail(last_error_, "KV value requires valid UTF-8 within the 4 MiB file limit");
    }
    auto candidate = values_;
    candidate.insert_or_assign(std::string(key), std::string(value));
    std::string text;
    if (!encode(candidate, text, last_error_))
    {
        return false;
    }
    values_ = std::move(candidate);
    return true;
}

bool KeyValueStore::has_key(std::string_view key)
{
    if (!loaded_)
    {
        return not_loaded(last_error_);
    }
    last_error_.clear();
    return valid_key(key, last_error_) && values_.contains(key);
}

bool KeyValueStore::delete_key(std::string_view key)
{
    if (!loaded_)
    {
        return not_loaded(last_error_);
    }
    last_error_.clear();
    if (!valid_key(key, last_error_))
    {
        return false;
    }
    if (const auto found = values_.find(key); found != values_.end())
    {
        values_.erase(found);
    }
    return true;
}

bool KeyValueStore::save()
{
    if (!loaded_)
    {
        return not_loaded(last_error_);
    }
    last_error_.clear();
    std::string text;
    if (!encode(values_, text, last_error_) || !inspect_directory(directory_, last_error_))
    {
        return false;
    }
    const auto target = directory_ / file_name;
    const auto temporary = directory_ / temporary_name;
    bool exists = false;
    bool temporary_exists = false;
    if (!inspect_file(target, exists, last_error_) ||
        !inspect_file(temporary, temporary_exists, last_error_))
    {
        return false;
    }
    std::error_code code;
    if (temporary_exists)
    {
        (void)std::filesystem::remove(temporary, code);
        if (code)
        {
            return fail(last_error_, "Cannot remove abandoned KV temporary file");
        }
    }
    std::filesystem::create_directories(directory_, code);
    if (code || !inspect_directory(directory_, last_error_))
    {
        return fail(last_error_, "Cannot create a regular KV directory");
    }
    const auto temporary_text = PathUtils::to_utf8(temporary);
    auto* file = storage_detail::open_exclusive_file(temporary, code);
    if (file == nullptr)
    {
        last_error_ = "Cannot exclusively create KV temporary file: " + code.message();
        return false;
    }
    const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed)
    {
        remove_temporary(temporary);
        return fail(last_error_, "Cannot completely write and close KV temporary file");
    }
    if (!inspect_directory(directory_, last_error_) || !inspect_file(target, exists, last_error_) ||
        !inspect_file(temporary, temporary_exists, last_error_))
    {
        remove_temporary(temporary);
        return false;
    }
    if (!temporary_exists)
    {
        return fail(last_error_, "KV temporary file disappeared before publication");
    }
    const auto target_text = PathUtils::to_utf8(target);
    if (!SDL_RenamePath(temporary_text.c_str(), target_text.c_str()))
    {
        const std::string detail = SDL_GetError();
        remove_temporary(temporary);
        last_error_ = "Cannot publish KV file: " + detail;
        return false;
    }
    return true;
}

bool KeyValueStore::is_loaded() const noexcept
{
    return loaded_;
}

const std::string& KeyValueStore::last_error() const noexcept
{
    return last_error_;
}

} // namespace engine
