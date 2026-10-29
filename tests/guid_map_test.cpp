#include "runtime/core/guid_map.hpp"

#include <cstdio>
#include <string>

namespace
{
bool expect(bool value, const char* message)
{
    if (!value)
        std::fprintf(stderr, "GUID mapping: %s\n", message);
    return value;
}
} // namespace

int main()
{
    constexpr auto guid = "550e8400-e29b-41d4-a716-446655440000";
    constexpr auto other = "550e8400-e29b-41d4-a716-446655440001";
    engine::GuidMap map;
    std::string output;
    if (!expect(map.add("project/scenes/main.scene", guid), "register canonical identity") ||
        !expect(!map.add("project/scenes/copy.scene", guid) &&
                    !map.add("project/scenes/main.scene", other) && map.size() == 1,
                "reject duplicate GUID and path without changing index") ||
        !expect(map.path_to_guid("project/scenes/main.scene", output) && output == guid,
                "forward lookup") ||
        !expect(map.guid_to_path(output, output) && output == "project/scenes/main.scene",
                "reverse lookup supports aliased output") ||
        !expect(map.path_to_guid(output, output) && output == guid,
                "forward lookup supports aliased output"))
        return 1;
    for (const auto* path : {"", "project/", "project/../x", "project/./x", "project//x",
                             "project/x/", "project/x\\y", "project/x:y", "/project/x", "x"})
    {
        if (!expect(!map.add(path, other), "reject unsafe or unscoped path"))
            return 1;
    }
    for (const auto* value :
         {"", "550e8400e29b41d4a716446655440000", "550E8400-e29b-41d4-a716-446655440000",
          "00000000-0000-0000-0000-000000000000", "550e8400-e29b-41d4-a716-446655440000.js"})
    {
        if (!expect(!engine::GuidMap::valid_guid(value), "reject incomplete or decorated GUID"))
            return 1;
    }
    if (!expect(!map.guid_to_path(other, output) && output.empty(), "failed lookup clears output"))
        return 1;
    auto snapshot = map;
    map.clear();
    if (!expect(map.size() == 0 && snapshot.path_to_guid("project/scenes/main.scene", output) &&
                    output == guid,
                "snapshots own independent indexes"))
        return 1;
    return 0;
}
