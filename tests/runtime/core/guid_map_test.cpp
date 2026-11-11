#include "runtime/core/guid_map.hpp"
#include "utf8_fixture.hpp"

#include <cstdio>
#include <string>

namespace
{
bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "GUID mapping: %s\n", message);
    }
    return value;
}
} // namespace

int main()
{
    constexpr auto guid = "550e8400-e29b-41d4-a716-446655440000";
    constexpr auto other = "550e8400-e29b-41d4-a716-446655440001";
    engine::GuidMap map;
    std::string output;
    if (!expect(map.add("scenes/main.scene", guid), "register canonical identity") ||
        !expect(!map.add("scenes/copy.scene", guid) && !map.add("scenes/main.scene", other) &&
                    map.size() == 1,
                "reject duplicate GUID and path without changing index") ||
        !expect(map.path_to_guid("scenes/main.scene", output) && output == guid,
                "forward lookup") ||
        !expect(map.guid_to_path(output, output) && output == "scenes/main.scene",
                "reverse lookup supports aliased output") ||
        !expect(map.path_to_guid(output, output) && output == guid,
                "forward lookup supports aliased output"))
    {
        return 1;
    }
    const auto unicode_path = test_support::utf8_fixture("resource_path");
    for (const auto* path :
         {"x", "scenes/main.scene", "project/scenes/main.scene", "builtin/textures/custom.png",
          "builtin://textures/white-srgb", "editor://icons/play", unicode_path.c_str()})
    {
        if (!expect(engine::GuidMap::valid_path(path),
                    "ordinary folders and explicit builtin/editor namespaces are valid"))
        {
            return 1;
        }
    }
    for (const auto* path : {"",
                             "../x",
                             "./x",
                             "a//x",
                             "/x",
                             "x/",
                             "x\\y",
                             "x:y",
                             "project/",
                             "builtin://",
                             "editor://",
                             "project://x",
                             "file://x",
                             "builtin:///x",
                             "builtin://../x",
                             "editor://./x",
                             "editor://icons//play",
                             "builtin://x:y",
                             "x\nname",
                             "x\x7f"})
    {
        if (!expect(!engine::GuidMap::valid_path(path) && !map.add(path, other),
                    "reject unsafe paths and unknown or incomplete namespaces"))
        {
            return 1;
        }
    }
    const std::string nul_path("folder/\0name", 12);
    if (!expect(!engine::GuidMap::valid_path(nul_path) && !map.add(nul_path, other),
                "reject an embedded NUL in a logical path"))
    {
        return 1;
    }
    for (const auto* value :
         {"", "550e8400e29b41d4a716446655440000", "550E8400-e29b-41d4-a716-446655440000",
          "00000000-0000-0000-0000-000000000000", "550e8400-e29b-41d4-a716-446655440000.js"})
    {
        if (!expect(!engine::GuidMap::valid_guid(value), "reject incomplete or decorated GUID"))
        {
            return 1;
        }
    }
    if (!expect(!map.guid_to_path(other, output) && output.empty(), "failed lookup clears output"))
    {
        return 1;
    }
    auto snapshot = map;
    map.clear();
    if (!expect(map.size() == 0 && snapshot.path_to_guid("scenes/main.scene", output) &&
                    output == guid,
                "snapshots own independent indexes"))
    {
        return 1;
    }
    return 0;
}
