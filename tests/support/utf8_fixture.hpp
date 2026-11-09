#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace test_support
{
[[nodiscard]] inline std::string utf8_fixture(std::string_view name)
{
    constexpr std::string_view source_file = __FILE__;
    const std::u8string source_path(source_file.begin(), source_file.end());
    const auto path = std::filesystem::path(source_path).parent_path() / "fixtures/unicode.tsv";
    std::ifstream input(path, std::ios::binary);
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        const auto separator = line.find('\t');
        if (separator != std::string::npos && std::string_view(line).substr(0, separator) == name)
        {
            return line.substr(separator + 1);
        }
    }
    std::fprintf(stderr, "Missing UTF-8 test fixture: %.*s\n", static_cast<int>(name.size()),
                 name.data());
    std::exit(EXIT_FAILURE);
}
} // namespace test_support
