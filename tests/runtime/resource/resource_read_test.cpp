#include "runtime/engine_config.hpp"
#include "runtime/resource/codecs/source_resource_codec.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
bool read_file_text(const std::filesystem::path& path, std::string& output)
{
    output.clear();
    engine::ByteBuffer data;
    if (!engine::VirtualFileSystem::read_file(path, data))
    {
        return false;
    }
    if (!data.empty())
    {
        output.assign(reinterpret_cast<const char*>(data.data()), data.size());
    }
    return true;
}

bool expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::fprintf(stderr, "Resource read test: %.*s\n", static_cast<int>(message.size()),
                     message.data());
    }
    return condition;
}

struct TestDirectory
{
    std::filesystem::path path;

    ~TestDirectory()
    {
        std::error_code error;
        if (!path.empty())
        {
            std::filesystem::remove_all(path, error);
        }
    }
};

bool create_directory(TestDirectory& directory)
{
    std::error_code error;
    const auto temporary = std::filesystem::temp_directory_path(error);
    if (error)
    {
        return false;
    }
    const auto identity = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned int index = 0; index < 100; ++index)
    {
        const auto path = temporary / ("engine-resource-read-test-" + std::to_string(identity) +
                                       "-" + std::to_string(index));
        if (std::filesystem::create_directory(path, error))
        {
            directory.path = path;
            return true;
        }
        if (error)
        {
            return false;
        }
    }
    return false;
}

bool write_file(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!contents.empty())
    {
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
    file.close();
    return static_cast<bool>(file);
}

bool binary_and_text(const std::filesystem::path& root)
{
    const auto path = root / std::filesystem::path(u8"资源-文件.bin");
    const std::array<unsigned char, 8> data{0, 1, 127, 128, 255, '\r', '\n', 0};
    const std::string content(reinterpret_cast<const char*>(data.data()), data.size());
    if (!write_file(path, content))
    {
        return expect(false, "create the binary fixture");
    }
    std::vector<std::byte> binary{std::byte{0x42}};
    std::string text = "old contents";
    bool success = expect(engine::VirtualFileSystem::read_file(path, binary),
                          "read binary with a Unicode filename without engine initialization");
    success &= expect(binary.size() == data.size(), "binary read preserves exact size");
    if (binary.size() == data.size())
    {
        for (std::size_t index = 0; index < data.size(); ++index)
        {
            success &= expect(binary[index] == static_cast<std::byte>(data[index]),
                              "binary read preserves all byte values including NUL");
        }
    }
    success &= expect(read_file_text(path, text) && text == content,
                      "text read does not truncate NUL or modify line endings");

    const auto text_path = root / std::filesystem::path(u8"脚本.ts");
    const std::u8string utf8 = u8"// 中文脚本\r\nDebug.Log('你好');\n";
    const std::string script(utf8.begin(), utf8.end());
    if (!write_file(text_path, script))
    {
        return expect(false, "create the UTF-8 fixture");
    }
    success &= expect(read_file_text(text_path, text) && text == script,
                      "text read preserves UTF-8 source contents");
    return success;
}

bool atomic_replace_with_open_reader(const std::filesystem::path& root)
{
    const auto path = root / std::filesystem::path(u8"原子-替换-🐟.bin");
    const auto replacement = root / std::filesystem::path(u8"临时-替换-🐟.bin");
    constexpr std::string_view previous_contents = "previous file snapshot\n";
    constexpr std::string_view replacement_contents = "replacement file snapshot\n";
    if (!write_file(path, previous_contents) || !write_file(replacement, replacement_contents))
    {
        return expect(false, "create the atomic replacement fixtures");
    }
    const auto utf8_path = engine::PathUtils::to_utf8(path);
    const auto utf8_replacement = engine::PathUtils::to_utf8(replacement);
    SDL_IOStream* previous = SDL_IOFromFile(utf8_path.c_str(), "rb");
    if (previous == nullptr)
    {
        return expect(false, "keep a Unicode file open through the SDL reader");
    }
    bool success = true;
#if defined(_WIN32)
    SDL_IOStream* writer = SDL_IOFromFile(utf8_path.c_str(), "r+b");
    success &= expect(writer == nullptr, "the old SDL reader still excludes in-place writers");
    if (writer != nullptr)
    {
        success &= expect(SDL_CloseIO(writer), "close an unexpected writable SDL stream");
    }
#endif
    success &= expect(SDL_RenamePath(utf8_replacement.c_str(), utf8_path.c_str()),
                      "atomically replace a file while its old SDL reader remains open");
    std::array<char, previous_contents.size()> bytes{};
    const auto size = SDL_ReadIO(previous, bytes.data(), bytes.size());
    success &=
        expect(size == bytes.size() && std::string_view(bytes.data(), size) == previous_contents,
               "the old SDL reader retains the previous file snapshot");
    std::string text;
    success &= expect(read_file_text(path, text) && text == replacement_contents,
                      "a new VFS reader sees the complete replacement contents");
    success &= expect(SDL_CloseIO(previous), "close the old SDL reader");
    return success;
}

bool empty_and_failures(const std::filesystem::path& root)
{
    const auto empty = root / "empty.bin";
    if (!write_file(empty, {}))
    {
        return expect(false, "create an empty file");
    }
    std::vector<std::byte> binary{std::byte{0x42}};
    std::string text = "old contents";
    bool success = expect(engine::VirtualFileSystem::read_file(empty, binary) && binary.empty(),
                          "empty binary file succeeds");
    success &= expect(read_file_text(empty, text) && text.empty(), "empty text file succeeds");

    auto embedded_nul = empty;
    embedded_nul += std::string("\0ignored", 8);
    const std::array<std::filesystem::path, 4> invalid_paths{
        std::filesystem::path{}, root / "missing.bin", root, embedded_nul};
    for (const auto& invalid : invalid_paths)
    {
        binary.assign(8, std::byte{0x42});
        text = "must be cleared";
        success &= expect(!engine::VirtualFileSystem::read_file(invalid, binary) && binary.empty(),
                          "invalid binary input fails and clears the old output");
        success &= expect(!read_file_text(invalid, text) && text.empty(),
                          "invalid text input fails and clears the old output");
    }
    return success;
}

bool oversized_file(const std::filesystem::path& root)
{
    const auto path = root / "oversized.bin";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.seekp(64 * 1024 * 1024);
        file.put('\0');
        file.close();
        if (!file)
        {
            return expect(false, "create the oversized sparse fixture");
        }
    }
    std::vector<std::byte> binary{std::byte{1}};
    std::string text = "previous data";
    bool success = expect(!engine::VirtualFileSystem::read_file(path, binary) && binary.empty(),
                          "oversized binary input fails without partial output");
    success &= expect(!read_file_text(path, text) && text.empty(),
                      "oversized text input fails without partial output");
    return success;
}

bool apk_path_failures(const std::filesystem::path& root)
{
    const std::array<std::string_view, 15> invalid_paths{"apk:",
                                                         "apk:/",
                                                         "apk:/asset/game.js",
                                                         "apk:/assets",
                                                         "apk:/assets/folder/",
                                                         "apk://assets/game.js",
                                                         "apk:/assets//game.js",
                                                         "apk:/assets/",
                                                         "apk:/assets/./game.js",
                                                         "apk:/assets/../game.js",
                                                         "apk:/assets/../../game.js",
                                                         "apk:/assets/game.js/.",
                                                         "apk:/assets/game.js/..",
                                                         "apk:/assets/folder\\game.js",
                                                         "apk:/assets/C:game.js"};
    bool success = true;
    std::vector<std::byte> binary;
    std::string text;
    for (const auto invalid : invalid_paths)
    {
        binary.assign(8, std::byte{0x42});
        text = "must be cleared";
        success &= expect(!engine::VirtualFileSystem::read_file(invalid, binary) && binary.empty(),
                          "malformed or escaping APK binary path fails and clears output");
        success &= expect(!read_file_text(invalid, text) && text.empty(),
                          "malformed or escaping APK text path fails and clears output");
    }
    auto embedded_nul = std::filesystem::path("apk:/assets/game.js");
    embedded_nul += std::string("\0ignored", 8);
    binary.assign(8, std::byte{0x42});
    text = "must be cleared";
    success &= expect(!engine::VirtualFileSystem::read_file(embedded_nul, binary) && binary.empty(),
                      "APK binary path with NUL fails and clears output");
    success &= expect(!read_file_text(embedded_nul, text) && text.empty(),
                      "APK text path with NUL fails and clears output");

#if !defined(__ANDROID__)
    // POSIX 允许带冒号的真实目录；确认 APK 前缀不会读取这个伪装的桌面文件。
    std::error_code error;
    const auto previous_directory = std::filesystem::current_path(error);
    if (error)
    {
        return expect(false, "capture the current working directory");
    }
#if !defined(_WIN32)
    const auto poison = root / "apk:" / "assets" / "game.js";
    std::filesystem::create_directories(poison.parent_path(), error);
    if (error || !write_file(poison, "must not be read as an APK asset"))
    {
        return expect(false, "create the local APK-prefix collision fixture");
    }
#endif
    std::filesystem::current_path(root, error);
    if (error)
    {
        return expect(false, "enter the APK-prefix collision fixture directory");
    }
    binary.assign(8, std::byte{0x42});
    text = "must be cleared";
    success &= expect(!engine::VirtualFileSystem::read_file("apk:/assets/game.js", binary) &&
                          binary.empty(),
                      "a valid APK binary path cannot read a same-name desktop file");
    success &= expect(!read_file_text("apk:/assets/game.js", text) && text.empty(),
                      "a valid APK text path cannot read a same-name desktop file");
    std::filesystem::current_path(previous_directory, error);
    success &= expect(!error, "restore the original working directory");
#else
    (void)root;
#endif
    return success;
}

bool single_resource_operations(const std::filesystem::path& root)
{
    using namespace engine;
    ThreadSystem threads({2});
    TaskSystem files(threads, {"ResourceFileTest", 1});
    TaskSystem decoding(threads, {"ResourceDecodeTest", 1});
    ResourceSystem resources;
    if (!threads.initialize({}) || !files.initialize({}) || !decoding.initialize({}) ||
        !resources.add<SourceResourceCodec>() || !resources.initialize({}))
    {
        return false;
    }
    ResourceReadConfig config;
    config.max_in_flight = 1;
    config.files.max_in_flight = 1;
    if (!expect(!resources.configure_async(files, files, config),
                "file and resource decoding require separate task domains") ||
        !resources.configure_async(files, decoding, config))
    {
        return false;
    }
    constexpr std::string_view guid = "10000000-0000-4000-8000-000000000005";
    auto entry = std::make_unique<schema::AssetEntryT>();
    entry->guid = guid;
    entry->path = "inputs/source.bin";
    entry->kind = schema::AssetKind::source;
    const ByteBuffer payload{std::byte{0}, std::byte{19}, std::byte{127}, std::byte{255}};
    ByteBuffer entity;
    if (!ResourceSystem::encode_resource(guid, entry->kind, payload, entity, entry->header_size) ||
        !write_file(root / "single-resource",
                    {reinterpret_cast<const char*>(entity.data()), entity.size()}))
    {
        return false;
    }
    std::uint64_t hash = 0;
    if (!VirtualFileSystem::hash_file(root / "single-resource", hash, entry->content_size))
    {
        return false;
    }
    std::array<char, 17> hash_text{};
    std::snprintf(hash_text.data(), hash_text.size(), "%016llx",
                  static_cast<unsigned long long>(hash));
    entry->content_hash = hash_text.data();
    entry->location = std::make_unique<schema::FileLocationT>();
    entry->location->mount = "content";
    entry->location->path = "single-resource";
    entry->location->length = entry->content_size;
    auto catalog = resources.catalog().data();
    catalog.revision = "single-read-v1";
    catalog.entries.push_back(std::move(entry));
    const MountList mounts{{"content", root}};
    if (!resources.update_catalog(catalog, mounts))
    {
        return false;
    }
    auto pump = [&](ResourceRead& operation)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (operation.status() == ResourceReadStatus::reading &&
               std::chrono::steady_clock::now() < deadline)
        {
            if (!resources.tick(0))
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return operation.status() != ResourceReadStatus::reading;
    };
    std::unique_ptr<Resource> synchronous;
    std::unique_ptr<ResourceRead> operation;
    std::unique_ptr<ResourceRead> pending;
    if (!expect(resources.read(guid, synchronous) && synchronous->info().guid == guid &&
                    !resources.read(guid, synchronous) && synchronous,
                "synchronous reads transfer ownership and refuse to overwrite it") ||
        !expect(resources.begin_read(guid, operation) &&
                    operation->status() == ResourceReadStatus::reading &&
                    !resources.begin_read(guid, pending) && !pending &&
                    resources.error() == ResourceError::busy,
                "single-item asynchronous reads are bounded and status does not advance") ||
        !expect(pump(*operation) && operation->status() == ResourceReadStatus::ready,
                "system tick publishes a decoded single resource"))
    {
        return false;
    }
    if (!expect(!resources.configure_async(files, decoding, config),
                "reconfiguration cannot discard an unclaimed ready result") ||
        !resources.begin_read(guid, pending))
    {
        return false;
    }
    const auto previous_generation = resources.generation();
    catalog.revision = "single-read-v2";
    if (!resources.update_catalog(catalog, mounts) || !resources.tick(0))
    {
        return false;
    }
    std::unique_ptr<Resource> asynchronous;
    if (!expect(operation->take(asynchronous) &&
                    asynchronous->generation() == previous_generation &&
                    operation->status() == ResourceReadStatus::delivered &&
                    !operation->take(asynchronous) && asynchronous,
                "a ready result transfers once and retains its completed catalog generation") ||
        !expect(pending->status() == ResourceReadStatus::failed &&
                    pending->error() == ResourceError::generation_changed && !pending->cancel(),
                "an unfinished old-generation read fails without reviving its result"))
    {
        return false;
    }
    operation.reset();
    pending.reset();
    resources.shutdown();
    if (!resources.initialize({}) || !resources.update_catalog(catalog, mounts) ||
        !resources.configure_async(files, decoding, config) ||
        !resources.begin_read(guid, operation) || !pump(*operation) ||
        operation->status() != ResourceReadStatus::ready)
    {
        return false;
    }
    resources.shutdown();
    return expect(
        operation->status() == ResourceReadStatus::cancelled && operation->cancel() &&
            operation->error() == ResourceError::cancelled && asynchronous->info().guid == guid &&
            resource_detail::data<SourceResource>(synchronous.get())->size() == payload.size(),
        "closing cancels unclaimed operations while transferred data remains independent");
}

bool asynchronous_ranges(const std::filesystem::path& root)
{
    using namespace engine;
    ThreadSystem threads({2});
    TaskSystem tasks(threads, {"FileTest", 2});
    std::unique_ptr<FileRead> survivor;
    {
        VirtualFileSystem files;
        if (!write_file(root / "async.pack", "prefixDATAtrailer") || !threads.initialize({}) ||
            !tasks.initialize({}) || !files.configure({{"content", root}}) ||
            !files.configure_async(tasks, {1, 4}))
        {
            return false;
        }
        schema::FileLocationT location;
        location.mount = "content";
        location.path = "async.pack";
        location.offset = 6;
        location.length = 4;
        auto pump = [&](FileRead& operation)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (operation.status() == FileReadStatus::reading &&
                   std::chrono::steady_clock::now() < deadline)
            {
                files.poll_async(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return operation.status();
        };
        std::unique_ptr<FileRead> operation;
        std::unique_ptr<FileRead> rejected;
        ByteBuffer bytes;
        if (!expect(files.begin_read(location, 0, 4, operation) &&
                        operation->status() == FileReadStatus::reading && !operation->take(bytes) &&
                        bytes.empty() && !files.begin_read(location, 0, 4, rejected) && !rejected &&
                        files.error() == FileError::busy,
                    "submission is nonblocking; queries do not publish I/O or bypass capacity") ||
            !expect(pump(*operation) == FileReadStatus::ready && operation->take(bytes) &&
                        std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) ==
                            "DATA" &&
                        operation->status() == FileReadStatus::delivered &&
                        !operation->take(bytes) && bytes.empty() && !operation->cancel(),
                    "only the pack member is read and ownership transfers exactly once"))
        {
            return false;
        }
        operation.reset();
        location.path = "missing.pack";
        if (!expect(files.begin_read(location, 0, 4, operation) &&
                        pump(*operation) == FileReadStatus::failed &&
                        operation->error() == FileError::io_failed &&
                        !operation->last_error().empty() && !operation->take(bytes) &&
                        bytes.empty() && !operation->cancel(),
                    "preparation failure is isolated and retains its error"))
        {
            return false;
        }
        operation.reset();
        location.path = "async.pack";
        if (!expect(files.begin_read(location, 0, 4, operation) &&
                        pump(*operation) == FileReadStatus::ready && operation->cancel() &&
                        operation->cancel() && operation->error() == FileError::cancelled &&
                        !operation->take(bytes) && bytes.empty(),
                    "failed reads release capacity; ready results can be discarded"))
        {
            return false;
        }
        operation.reset();
        location.length = 5;
        if (!expect(!files.begin_read(location, 0, 5, operation) && !operation &&
                        files.error() == FileError::budget_exceeded,
                    "an oversized request fails rather than waiting forever"))
        {
            return false;
        }
        location.length = 4;
        if (!expect(!files.begin_read(location, 3, 2, operation) &&
                        files.error() == FileError::invalid_range,
                    "range overflow is rejected before I/O") ||
            !expect(files.begin_read(location, 0, 4, survivor) && survivor->cancel() &&
                        survivor->status() == FileReadStatus::cancelled,
                    "cancelling an in-flight read immediately invalidates publication"))
        {
            return false;
        }
        files.shutdown_async();
        files.shutdown_async();
        if (!expect(files.poll_async(1) == 0 && survivor->cancel(),
                    "shutdown drains I/O without reviving cancelled operations"))
        {
            return false;
        }
        survivor.reset();
        if (!files.configure_async(tasks, {1, 4}) || !files.begin_read(location, 0, 4, survivor) ||
            pump(*survivor) != FileReadStatus::ready)
        {
            return false;
        }
    }
    return expect(survivor->status() == FileReadStatus::cancelled &&
                      survivor->error() == FileError::cancelled && survivor->cancel(),
                  "unclaimed ready operations may outlive the file service safely");
}

} // namespace

int main()
{
    TestDirectory directory;
    if (!create_directory(directory))
    {
        return 1;
    }
    int failures = 0;
    failures += !binary_and_text(directory.path);
    failures += !atomic_replace_with_open_reader(directory.path);
    failures += !empty_and_failures(directory.path);
    failures += !oversized_file(directory.path);
    failures += !apk_path_failures(directory.path);
    failures +=
        !(asynchronous_ranges(directory.path) && single_resource_operations(directory.path));
    return failures == 0 ? 0 : 1;
}
