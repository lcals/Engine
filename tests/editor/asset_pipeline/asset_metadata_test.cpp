#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/asset_metadata.hpp"
#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/asset_pipeline/project_lock.hpp"
#include "file_text.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
namespace fs = std::filesystem;
bool expect(bool condition, const char* description, const std::string& error)
{
    if (!condition)
    {
        std::fprintf(stderr, "%s: %s\n", description, error.c_str());
    }
    return condition;
}
struct Project
{
    fs::path root;
    ~Project()
    {
        std::error_code error;
        fs::remove_all(root, error);
    }
};

bool source_creation_cases(const fs::path& root)
{
    using namespace engine::editor::asset_pipeline;
    std::error_code code;
    fs::create_directories(root / "assets", code);
    if (code)
    {
        return false;
    }
    fs::create_directories(root / "cache", code);
    if (code)
    {
        return false;
    }
    std::string error;
    fs::path destination;
    std::string guid;
    std::string checked_guid;
    std::string content;
    {
        ProjectLock lock;
        if (!expect(lock.acquire(root / "cache/.asset-build.lock", error), "lock source creation",
                    error) ||
            !expect(
                project_file_path(root, "assets", "nested/created.terrain", ".terrain", destination,
                                  error) &&
                    create_source_with_meta(destination, "terrain source", "terrain", guid, error),
                "create source and adjacent metadata", error))
        {
            return false;
        }
    }
    // ensure_asset_guid 自行取得资源锁，验证前释放创建操作持有的锁。
    if (!expect(test_support::read_text(destination, content) && content == "terrain source" &&
                    ensure_asset_guid(root, destination, "terrain", checked_guid, error) &&
                    checked_guid == guid,
                "publish source with matching importer and GUID", error))
    {
        return false;
    }
    ProjectLock lock;
    if (!expect(lock.acquire(root / "cache/.asset-build.lock", error), "lock source failure cases",
                error))
    {
        return false;
    }
    const auto source_only = root / "assets/source.terrain";
    auto source_meta = source_only;
    source_meta += ".meta";
    if (!write_text(source_only, "original source", error) ||
        !expect(!create_source_with_meta(source_only, "replacement", "terrain", guid, error) &&
                    guid.empty() && !fs::exists(source_meta, code) && !code &&
                    test_support::read_text(source_only, content) && content == "original source",
                "source conflict preserves original without creating metadata", error))
    {
        return false;
    }
    const auto meta_only_source = root / "assets/meta-only.destruction";
    auto meta_only = meta_only_source;
    meta_only += ".meta";
    if (!write_text(meta_only, "original metadata", error) ||
        !expect(
            !create_source_with_meta(meta_only_source, "replacement", "destruction", guid, error) &&
                guid.empty() && !fs::exists(meta_only_source, code) && !code &&
                test_support::read_text(meta_only, content) && content == "original metadata",
            "metadata conflict preserves original without creating a source", error))
    {
        return false;
    }
    const auto blocked = root / "assets/blocked";
    if (!write_text(blocked, "parent is a file", error) ||
        !expect(!project_file_path(root, "assets", "blocked/child.terrain", ".terrain", destination,
                                   error) &&
                    destination.empty(),
                "reject non-directory parent", error) ||
        !expect(!project_file_path(root, "assets", "../outside.terrain", ".terrain", destination,
                                   error) &&
                    destination.empty(),
                "reject project path traversal", error))
    {
        return false;
    }

#if !defined(_WIN32)
    const auto denied = root / "assets/denied";
    fs::create_directory(denied, code);
    if (code)
    {
        return false;
    }
    const auto permissions = fs::status(denied, code).permissions();
    if (code)
    {
        return false;
    }
    fs::permissions(denied, fs::perms::owner_read | fs::perms::owner_exec, code);
    if (code)
    {
        return false;
    }
    // 有提权的测试进程可能绕过目录权限；仅在实际不可写时验证写入失败路径。
    const auto probe = denied / "probe";
    const bool writable = write_text(probe, "probe", error);
    bool created = false;
    if (!writable)
    {
        guid = "previous identity";
        created =
            create_source_with_meta(denied / "failed.terrain", "source", "terrain", guid, error);
    }
    fs::permissions(denied, permissions, code);
    if (code)
    {
        return false;
    }
    fs::remove(probe, code);
    if (code ||
        (!writable && !expect(!created && guid.empty() && fs::is_empty(denied, code) && !code,
                              "failed write leaves no source, metadata or temporary files", error)))
    {
        return false;
    }
#endif
    return true;
}
} // namespace

int main()
{
    using namespace engine;
    using namespace engine::editor::asset_pipeline;
    std::error_code code;
    Project project{fs::temp_directory_path(code) /
                    ("engine-asset-meta-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    for (const char* part : {"assets", "cache", "settings"})
    {
        fs::create_directories(project.root / part, code);
        if (code)
        {
            return 1;
        }
    }
    if (!source_creation_cases(project.root / "source-creation"))
    {
        return 1;
    }
    const auto source = project.root / "assets/a.txt";
    std::string error;
    std::string first;
    std::string second;
    if (!expect(write_text(project.root / "settings/editor-import.toml",
                           "[common]\nimport_threads = 8\n", error),
                "configure editor imports", error) ||
        !expect(write_text(source, "one", error), "create source", error) ||
        !expect(ensure_asset_guid(project.root, source, "source", first, error),
                "create source identity", error) ||
        !expect(ensure_asset_guid(project.root, source, "source", second, error) && first == second,
                "retain existing identity", error) ||
        !expect(!ensure_asset_guid(project.root, source, "mesh", second, error) && second.empty(),
                "reject importer mismatch", error))
    {
        return 1;
    }
    const auto duplicate = project.root / "assets/b.txt";
    std::string metadata;
    if (!write_text(duplicate, "two", error) ||
        !test_support::read_text(source.string() + ".meta", metadata) ||
        !write_text(duplicate.string() + ".meta", metadata, error) ||
        !expect(!ensure_asset_guid(project.root, duplicate, "source", second, error) &&
                    second.empty(),
                "reject duplicate GUID without repairing it", error))
    {
        return 1;
    }
    fs::remove(duplicate.string() + ".meta", code);
    if (!ensure_asset_guid(project.root, duplicate, "source", second, error) || first == second)
    {
        return 1;
    }
    engine::ThreadSystem threads;
    engine::TaskSystem tasks(threads, {"Editor", 4, engine::TaskPriority::low});
    engine::editor::AssetPipelineSystem pipeline(tasks);
    if (!expect(threads.initialize({}) && tasks.initialize({}) && pipeline.initialize({}) &&
                    pipeline.build_assets(project.root),
                "import shared metadata", pipeline.last_error()))
    {
        return 1;
    }
    fs::remove(source.string() + ".meta", code);
    if (!expect(!ensure_asset_guid(project.root, source, "source", second, error) && second.empty(),
                "known source cannot silently receive a new identity", error))
    {
        return 1;
    }
    const auto outside = project.root / "outside.txt";
    if (!write_text(outside, "outside", error) ||
        !expect(!ensure_asset_guid(project.root, outside, "source", second, error),
                "reject source outside assets", error))
    {
        return 1;
    }
    pipeline.shutdown();
    return 0;
}
