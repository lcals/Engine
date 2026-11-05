#include "editor/asset_pipeline/import_tool.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>

using engine::editor::asset_pipeline::ImportToolStatus;
using engine::editor::asset_pipeline::run_import_tool;

int main(int argc, char** argv)
{
    if (argc == 2)
    {
        const std::string_view mode(argv[1]);
        if (mode == "echo")
        {
            std::fputs("tool output", stdout);
            return 0;
        }
        if (mode == "fail")
        {
            return 7;
        }
        if (mode == "wait")
        {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            return 0;
        }
        return 1;
    }
    std::string output;
    std::string error;
    if (run_import_tool({argv[0], "echo"}, {}, 64, output, error) != ImportToolStatus::success ||
        output != "tool output" || !error.empty())
    {
        return 1;
    }
    if (run_import_tool({argv[0], "echo"}, {}, 4, output, error) != ImportToolStatus::failed ||
        output.size() > 4 || error.empty())
    {
        return 2;
    }
    if (run_import_tool({argv[0], "fail"}, {}, 64, output, error) != ImportToolStatus::failed ||
        error.empty())
    {
        return 3;
    }
    const auto start = std::chrono::steady_clock::now();
    if (run_import_tool(
            {argv[0], "wait"}, [&]
            { return std::chrono::steady_clock::now() - start > std::chrono::milliseconds(100); },
            64, output, error) != ImportToolStatus::cancelled ||
        std::chrono::steady_clock::now() - start > std::chrono::seconds(2))
    {
        return 4;
    }
    return 0;
}
