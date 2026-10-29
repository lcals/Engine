#include "runtime/resource/resource_system.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{

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
    bool success = expect(engine::ResourceSystem::read_binary(path, binary),
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
    success &= expect(engine::ResourceSystem::read_text(path, text) && text == content,
                      "text read does not truncate NUL or modify line endings");

    const auto text_path = root / std::filesystem::path(u8"脚本.ts");
    const std::u8string utf8 = u8"// 中文脚本\r\nDebug.Log('你好');\n";
    const std::string script(utf8.begin(), utf8.end());
    if (!write_file(text_path, script))
    {
        return expect(false, "create the UTF-8 fixture");
    }
    success &= expect(engine::ResourceSystem::read_text(text_path, text) && text == script,
                      "text read preserves UTF-8 source contents");
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
    bool success = expect(engine::ResourceSystem::read_binary(empty, binary) && binary.empty(),
                          "empty binary file succeeds");
    success &= expect(engine::ResourceSystem::read_text(empty, text) && text.empty(),
                      "empty text file succeeds");

    auto embedded_nul = empty;
    embedded_nul += std::string("\0ignored", 8);
    const std::array<std::filesystem::path, 4> invalid_paths{
        std::filesystem::path{}, root / "missing.bin", root, embedded_nul};
    for (const auto& invalid : invalid_paths)
    {
        binary.assign(8, std::byte{0x42});
        text = "must be cleared";
        success &= expect(!engine::ResourceSystem::read_binary(invalid, binary) && binary.empty(),
                          "invalid binary input fails and clears the old output");
        success &= expect(!engine::ResourceSystem::read_text(invalid, text) && text.empty(),
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
    bool success = expect(!engine::ResourceSystem::read_binary(path, binary) && binary.empty(),
                          "oversized binary input fails without partial output");
    success &= expect(!engine::ResourceSystem::read_text(path, text) && text.empty(),
                      "oversized text input fails without partial output");
    return success;
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
    failures += !empty_and_failures(directory.path);
    failures += !oversized_file(directory.path);
    return failures == 0 ? 0 : 1;
}
