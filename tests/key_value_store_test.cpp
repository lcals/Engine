#include "runtime/storage/key_value_store.hpp"
#include "runtime/storage/storage_file.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace
{

bool expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::fprintf(stderr, "Key value store test: %.*s\n", static_cast<int>(message.size()),
                     message.data());
    }
    return condition;
}

struct TestDirectory
{
    std::filesystem::path path;

    ~TestDirectory()
    {
        if (!path.empty())
        {
            std::error_code code;
            (void)std::filesystem::remove_all(path, code);
        }
    }
};

bool create_directory(TestDirectory& directory)
{
    std::error_code code;
    const auto root = std::filesystem::temp_directory_path(code);
    if (code)
    {
        return false;
    }
    const auto identity = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned int index = 0; index < 100; ++index)
    {
        const auto path =
            root / ("engine-kv-test-" + std::to_string(identity) + "-" + std::to_string(index));
        if (std::filesystem::create_directory(path, code))
        {
            directory.path = path;
            return true;
        }
        if (code)
        {
            return false;
        }
    }
    return false;
}

bool path_exists(const std::filesystem::path& path)
{
    std::error_code code;
    return std::filesystem::exists(path, code) && !code;
}

bool write_file(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    return static_cast<bool>(file);
}

bool read_file(const std::filesystem::path& path, std::string& text)
{
    text.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !file.bad();
}

bool exclusive_creation(const std::filesystem::path& root)
{
    const auto path = root / std::filesystem::path(u8"独占-🙂.tmp");
    std::error_code error;
    auto* file = engine::storage_detail::open_exclusive_file(path, error);
    if (!expect(file != nullptr && !error, "exclusively create a Unicode temporary path"))
    {
        if (file != nullptr)
        {
            (void)std::fclose(file);
        }
        return false;
    }
    constexpr std::string_view content = "keep the already created file";
    const bool written = std::fwrite(content.data(), 1, content.size(), file) == content.size();
    const bool closed = std::fclose(file) == 0;
    if (!expect(written && closed, "write and close the exclusively created file"))
    {
        return false;
    }
    file = engine::storage_detail::open_exclusive_file(path, error);
    if (file != nullptr)
    {
        (void)std::fclose(file);
    }
    std::string actual;
    if (!expect(file == nullptr && error && read_file(path, actual) && actual == content,
                "exclusive creation rejects an existing file without truncating its contents"))
    {
        return false;
    }

    const auto link = root / "exclusive-link.tmp";
    std::filesystem::create_symlink(path, link, error);
    if (error)
    {
        std::puts("Key value store test: exclusive symlink check skipped (not permitted)");
        return true;
    }
    file = engine::storage_detail::open_exclusive_file(link, error);
    if (file != nullptr)
    {
        (void)std::fclose(file);
    }
    return expect(file == nullptr && error && read_file(path, actual) && actual == content,
                  "exclusive creation rejects a symlink without touching its target");
}

bool persistence(const std::filesystem::path& root)
{
    const auto directory = root / std::filesystem::path(u8"用户数据");
    const auto target = directory / "kv.json";
    engine::KeyValueStore writer;
    std::string value = "stale output";
    if (!expect(!writer.is_loaded() && !writer.get_string("coins", value) && value.empty() &&
                    !writer.last_error().empty() && !writer.has_key("coins") &&
                    !writer.set_string("coins", "10") && !writer.delete_key("coins") &&
                    !writer.save(),
                "all data operations require an explicitly loaded store") ||
        !expect(writer.load(directory) && writer.is_loaded() && writer.last_error().empty() &&
                    !path_exists(target),
                "a missing directory or file loads as an empty store without writing") ||
        !expect(writer.set_string("coins", "42") &&
                    writer.set_string("角色", "你好🙂\n\"value\"") &&
                    writer.set_string("empty", "") && !path_exists(target),
                "Unicode and empty string changes stay in memory until Save"))
    {
        return false;
    }
    engine::KeyValueStore observer;
    value = "stale output";
    if (!expect(observer.load(directory) && !observer.get_string("coins", value) && value.empty() &&
                    observer.last_error().empty() && !observer.has_key("coins") &&
                    observer.last_error().empty() && observer.delete_key("missing"),
                "another instance cannot see unsaved values and missing keys are not errors") ||
        !expect(writer.save() && observer.load(directory) && observer.get_string("coins", value) &&
                    value == "42" && observer.get_string("角色", value) &&
                    value == "你好🙂\n\"value\"" && observer.get_string("empty", value) &&
                    value.empty(),
                "Save publishes a complete Unicode snapshot readable by another instance") ||
        !expect(!path_exists(directory / "kv.json.tmp"),
                "successful publication consumes its temporary") ||
        !expect(writer.set_string("coins", "99") && observer.load(directory) &&
                    observer.get_string("coins", value) && value == "42",
                "subsequent in-memory edits do not implicitly replace the disk snapshot") ||
        !expect(writer.delete_key("coins") && writer.delete_key("coins") && writer.save() &&
                    observer.load(directory) && !observer.has_key("coins") &&
                    observer.has_key("角色"),
                "deletion persists without dropping unrelated values"))
    {
        return false;
    }
    {
        engine::KeyValueStore discarded;
        if (!discarded.load(directory) || !discarded.set_string("shutdown", "not saved"))
        {
            return false;
        }
    }
    return expect(observer.load(directory) && !observer.has_key("shutdown"),
                  "destruction does not save pending changes");
}

bool invalid_inputs(const std::filesystem::path& root)
{
    engine::KeyValueStore store;
    const auto directory = root / "limits";
    const std::string key(256, 'k');
    const std::string invalid_utf8("\xc0\xaf", 2);
    const std::string oversized(4U * 1024U * 1024U, 'v');
    std::string value;
    return expect(store.load(directory) && store.set_string("kept", "original") &&
                      store.set_string(key, "boundary") && !store.set_string("", "bad") &&
                      !store.set_string(std::string(257, 'k'), "bad") &&
                      !store.set_string(invalid_utf8, "bad") &&
                      !store.set_string("kept", invalid_utf8) &&
                      !store.set_string("kept", oversized) && store.get_string("kept", value) &&
                      value == "original" && store.get_string(key, value) && value == "boundary" &&
                      store.save(),
                  "invalid UTF-8, keys and encoded file overflow preserve accepted values");
}

bool failed_reload(const std::filesystem::path& root)
{
    const auto directory = root / "corrupt";
    std::error_code code;
    std::filesystem::create_directory(directory, code);
    if (code)
    {
        return false;
    }
    constexpr std::string_view valid = R"({"version":1,"values":{"kept":"disk"}})";
    constexpr std::array invalid = {
        std::string_view("{broken JSON"),
        std::string_view(R"({"version":2,"values":{"kept":"future"}})"),
        std::string_view(R"({"version":1,"values":{"kept":17}})"),
    };
    for (const auto text : invalid)
    {
        engine::KeyValueStore store;
        std::string disk;
        std::string value = "stale";
        if (!write_file(directory / "kv.json", valid) || !store.load(directory) ||
            !store.set_string("kept", "pending") || !write_file(directory / "kv.json", text))
        {
            return false;
        }
        if (!expect(!store.load(directory) && !store.is_loaded() && !store.last_error().empty(),
                    "a failed reload disables the previously loaded cache"))
        {
            return false;
        }
        const auto error = store.last_error();
        if (!expect(!store.set_string("kept", "overwrite") && !store.has_key("kept") &&
                        !store.delete_key("kept") && !store.save() &&
                        !store.get_string("kept", value) && value.empty() &&
                        store.last_error() == error && read_file(directory / "kv.json", disk) &&
                        disk == text,
                    "a failed reload disables the old cache and cannot overwrite rejected files"))
        {
            return false;
        }
    }
    return true;
}

bool failed_save(const std::filesystem::path& root)
{
    const auto directory = root / "write-failure";
    engine::KeyValueStore store;
    std::string before;
    std::string after;
    std::string value;
    if (!store.load(directory) || !store.set_string("kept", "disk") || !store.save() ||
        !read_file(directory / "kv.json", before) || !store.set_string("kept", "pending"))
    {
        return false;
    }
    std::error_code code;
    if (!std::filesystem::create_directory(directory / "kv.json.tmp", code) || code)
    {
        return false;
    }
    if (!expect(!store.save() && !store.last_error().empty() && store.is_loaded() &&
                    read_file(directory / "kv.json", after) && after == before &&
                    store.get_string("kept", value) && value == "pending",
                "a failed write preserves both the previous disk file and pending memory"))
    {
        return false;
    }
    if (!std::filesystem::remove(directory / "kv.json.tmp", code) || code)
    {
        return false;
    }
    engine::KeyValueStore observer;
    return expect(write_file(directory / "kv.json.tmp", "abandoned temporary snapshot") &&
                      store.save() && observer.load(directory) &&
                      observer.get_string("kept", value) && value == "pending",
                  "retry replaces an abandoned regular temporary and saves the pending snapshot");
}

bool symbolic_links(const std::filesystem::path& root)
{
    const auto directory = root / "link-target";
    engine::KeyValueStore store;
    if (!store.load(directory) || !store.set_string("kept", "disk") || !store.save())
    {
        return false;
    }
    std::error_code code;
    std::filesystem::create_directory_symlink(directory, root / "linked-directory", code);
    if (code)
    {
        std::puts("Key value store test: symbolic link checks skipped (not permitted)");
        return true;
    }
    engine::KeyValueStore rejected;
    if (!expect(!rejected.load(root / "linked-directory") && !rejected.save(),
                "a symbolic link directory cannot be loaded or saved"))
    {
        return false;
    }
    const auto file_directory = root / "linked-file";
    std::filesystem::create_directory(file_directory, code);
    if (code)
    {
        return false;
    }
    std::filesystem::create_symlink(directory / "kv.json", file_directory / "kv.json", code);
    if (code || !expect(!rejected.load(file_directory) && !rejected.save(),
                        "a symbolic link KV file cannot be loaded or saved"))
    {
        return false;
    }
    std::string before;
    std::string after;
    if (!read_file(directory / "kv.json", before) || !store.set_string("kept", "pending"))
    {
        return false;
    }
    std::filesystem::create_symlink(directory / "kv.json", directory / "kv.json.tmp", code);
    return !code &&
           expect(!store.save() && read_file(directory / "kv.json", after) && after == before &&
                      !rejected.load(directory),
                  "a symbolic link temporary cannot redirect writes or be accepted on load");
}

} // namespace

int main()
{
    static_assert(!std::is_copy_constructible_v<engine::KeyValueStore>);
    static_assert(!std::is_move_constructible_v<engine::KeyValueStore>);
    TestDirectory directory;
    return expect(create_directory(directory), "create a private test directory") &&
                   exclusive_creation(directory.path) && persistence(directory.path) &&
                   invalid_inputs(directory.path) && failed_reload(directory.path) &&
                   failed_save(directory.path) && symbolic_links(directory.path)
               ? 0
               : 1;
}
