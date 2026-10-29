#include "asset_sync_system.hpp"
#include "engine_config.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

bool write_file(const fs::path& path, std::string_view text)
{
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary);
    output << text;
    output.close();
    return !error && !output.fail();
}

Json read_json(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return Json::parse(input, nullptr, false);
}

bool check(bool condition, const char* description)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
    return condition;
}

bool exercise(const fs::path& root)
{
    const auto source = root / "source";
    const auto runtime = root / "runtime";
    const auto available = runtime / "assets" / "available.json";
    const auto entry = source / "scripts" / "game.js";
    engine::AssetSyncSystem sync;
    if (!check(!sync.sync(source, runtime), "uninitialized sync rejected") ||
        !check(sync.initialize({}), "initialize sync") ||
        !check(write_file(entry, "export default class Game {}\n"), "write gameplay entry") ||
        !check(write_file(source / "scripts" / "deleted.js", "export const value = 1;\n"),
               "write removable script") ||
        !check(sync.sync(source, runtime), "publish initial version"))
    {
        return false;
    }
    const auto first = read_json(available);
    const auto first_revision = sync.revision();
    std::error_code error;
    const auto first_timestamp = fs::last_write_time(available, error);
    if (!check(!error && first.is_object(), "read complete manifest") ||
        !check(sync.sync(source, runtime), "repeat identical sync") ||
        !check(sync.revision() == first_revision, "identical content retains revision") ||
        !check(fs::last_write_time(available, error) == first_timestamp,
               "identical content does not republish manifest") ||
        !check(!sync.sync(source, runtime, "../escape.js"), "entry traversal rejected") ||
        !check(!sync.sync(source, source), "source/runtime overlap rejected") ||
        !check(read_json(available) == first, "failed sync keeps old manifest"))
    {
        return false;
    }
    fs::remove(source / "scripts" / "deleted.js", error);
    if (!check(!error && write_file(entry, "export default class Game { updated = true; }\n"),
               "change entry and remove script") ||
        !check(sync.sync(source, runtime), "publish changed snapshot"))
    {
        return false;
    }
    const auto second = read_json(available);
    if (!check(sync.revision() != first_revision, "changed content gets new revision") ||
        !check(second["deleted"] == Json::array({"scripts/deleted.js"}), "deletion recorded") ||
        !check(
            fs::exists(runtime / "assets" / "versions" / first_revision / "scripts" / "deleted.js",
                       error),
            "old active snapshot remains intact") ||
        !check(write_file(source / "scripts" / "added.js", "export const added = 1;\n"),
               "add script") ||
        !check(sync.sync(source, runtime), "publish added script"))
    {
        return false;
    }
    const auto third = read_json(available);
    if (!check(third["deleted"] == second["deleted"], "deletions persist across later syncs"))
    {
        return false;
    }
    // 暂存文件和其他同步持有的锁都不能让半批文件成为可用版本。
    if (!write_file(runtime / "assets" / ".staging-interrupted" / "scripts" / "game.js", "bad"))
    {
        return false;
    }
    fs::create_directory(runtime / "assets" / ".sync-lock", error);
    if (!check(!error && !sync.sync(source, runtime),
               "concurrent or interrupted sync lock rejected") ||
        !check(read_json(available) == third, "interrupted transfer never changes manifest"))
    {
        return false;
    }
    fs::remove(runtime / "assets" / ".sync-lock", error);
    if (!check(!error && sync.sync(source, runtime), "retry after abandoned lock cleanup"))
    {
        return false;
    }
    // 主机支持符号链接时验证源与目标均不会逃出指定树。
    fs::create_symlink(entry, source / "scripts" / "escape.js", error);
    if (!error)
    {
        if (!check(!sync.sync(source, runtime), "source symlink rejected") ||
            !check(read_json(available) == third, "symlink failure keeps old manifest"))
        {
            return false;
        }
        fs::remove(source / "scripts" / "escape.js", error);
        fs::create_directory_symlink(runtime, root / "redirected-runtime", error);
        if (!check(!error && !sync.sync(source, root / "redirected-runtime"),
                   "runtime symlink rejected"))
        {
            return false;
        }
    }
    else
    {
        std::fputs("Symlink checks skipped: host does not permit creating test symlinks\n", stderr);
        error.clear();
    }
    const auto current_revision = third["revision"].get<std::string>();
    if (!check(
            write_file(runtime / "assets" / "versions" / current_revision / "scripts" / "game.js",
                       "corrupt"),
            "simulate corrupted immutable version") ||
        !check(!sync.sync(source, runtime), "corrupted immutable version is rejected") ||
        !check(read_json(available) == third, "corruption does not replace available pointer"))
    {
        return false;
    }
    for (const char* invalid :
         {R"({"files":[false],"deleted":[]})", R"({"files":[],"deleted":[null]})",
          R"({"files":[{"path":"../outside"}],"deleted":[]})"})
    {
        if (!check(write_file(available, invalid), "write invalid external manifest") ||
            !check(!sync.sync(source, runtime), "invalid manifest rejected without exceptions"))
        {
            return false;
        }
    }
    return true;
}
} // namespace

int main()
{
    std::error_code error;
    const auto temporary = fs::canonical(fs::temp_directory_path(error), error);
    if (error)
    {
        return 1;
    }
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = temporary / ("engine-asset-sync-test-" + std::to_string(nonce));
    const bool success = exercise(root);
    fs::remove_all(root, error);
    return success && !error ? 0 : 1;
}
