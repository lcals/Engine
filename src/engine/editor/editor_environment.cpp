#include "editor/editor_environment.hpp"
#include "runtime/utils/hash_utils.hpp"

#include "editor/asset_pipeline/project_lock.hpp"
#include "editor/project/project_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/resource/resource_system.hpp"

#include "runtime/utils/path_utils.hpp"

#include <nlohmann/json.hpp>

#include <SDL3/SDL_filesystem.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <utility>

namespace engine::editor
{
namespace
{
namespace fs = std::filesystem;

std::string application_name(const std::string& project_identity)
{
    return "engine-editor-" + hash::hex(hash::fnv1a(project_identity));
}
} // namespace

EditorEnvironment::EditorEnvironment(ProjectSystem& project, fs::path persistent_directory)
    : project_(project), persistent_directory_(std::move(persistent_directory))
{
}

EditorEnvironment::~EditorEnvironment() noexcept
{
    shutdown();
}

bool EditorEnvironment::initialize()
{
    if (initialized_ || !project_.is_initialized() || App::is_initialized())
    {
        return false;
    }
    std::error_code error;
    const auto root = fs::canonical(PathUtils::from_utf8(project_.project_root_), error);
    if (error)
    {
        return false;
    }
    project_identity_ = PathUtils::to_utf8(root);
    AppConfig config;
    config.application_name = application_name(project_identity_);
    config.persistent_data_path = persistent_directory_;
    // 编辑器直接使用导入缓存；不建立第二份包体或会话字节快照。
    config.assets_path = root / "cache";
    if (!App::initialize(config))
    {
        shutdown();
        return false;
    }
    builtin_path_ = App::assets_path();
    initialized_ = true;
    asset_pipeline::ProjectLock project_lock;
    std::string lock_error;
    if (!project_lock.acquire(builtin_path_ / ".asset-build.lock", lock_error))
    {
        LogSystem::write(LogLevel::error,
                         "Cannot register the editor resource target: " + lock_error);
        shutdown();
        return false;
    }
    fs::path candidate;
    fs::path lease_path;
    std::unique_ptr<asset_pipeline::ProjectLock> lease;
    {
        const auto directory = builtin_path_ / "editor-hosts";
        std::error_code error;
        fs::create_directories(directory, error);
        if (error || fs::is_symlink(fs::symlink_status(directory, error)) || error)
        {
            shutdown();
            return false;
        }
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        candidate = directory / (std::to_string(nonce) + ".json");
        const auto temporary = directory / (std::to_string(nonce) + ".tmp");
        lease_path = directory / (std::to_string(nonce) + ".lock");
        for (const auto& path : {candidate, temporary, lease_path})
        {
            const auto status = fs::symlink_status(path, error);
            if (error != std::errc::no_such_file_or_directory &&
                (error || status.type() != fs::file_type::not_found))
            {
                shutdown();
                return false;
            }
            error.clear();
        }
        lease = std::make_unique<asset_pipeline::ProjectLock>();
        std::string lock_error;
        if (!lease->acquire(lease_path, lock_error))
        {
            LogSystem::write(LogLevel::error, "Cannot acquire editor host lease: " + lock_error);
            shutdown();
            return false;
        }
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << nlohmann::json({{"projectRoot", project_identity_},
                                  {"targetPlatform", ENGINE_TARGET_PLATFORM},
                                  {"runtimeDir", PathUtils::to_utf8(App::persistent_data_path())}})
                      .dump();
        output.close();
        if (output)
        {
            fs::rename(temporary, candidate, error);
        }
        if (!output || error)
        {
            fs::remove(temporary, error);
            lease.reset();
            fs::remove(lease_path, error);
            shutdown();
            return false;
        }
    }
    session_marker_ = std::move(candidate);
    session_lease_ = std::move(lease);
    const auto status_file = App::persistent_data_path() / "editor-status.json";
    const auto status_temporary = App::persistent_data_path() / ".editor-initial-status.json";
    std::ofstream output(status_temporary, std::ios::binary | std::ios::trunc);
    output << nlohmann::json({{"session", PathUtils::to_utf8(session_marker_.stem())},
                              {"projectRoot", project_identity_},
                              {"targetPlatform", ENGINE_TARGET_PLATFORM},
                              {"projectState", "idle"},
                              {"running", false}})
                  .dump()
           << '\n';
    output.close();
    if (!output || !SDL_RenamePath(PathUtils::to_utf8(status_temporary).c_str(),
                                   PathUtils::to_utf8(status_file).c_str()))
    {
        std::error_code ignored;
        fs::remove(status_temporary, ignored);
        shutdown();
        return false;
    }
    return true;
}

void EditorEnvironment::shutdown() noexcept
{
    if (!session_marker_.empty())
    {
        std::error_code error;
        fs::remove(session_marker_, error);
        if (error)
        {
            LogSystem::write(LogLevel::warning,
                             "Cannot remove editor host registration: " + error.message());
        }
        auto lease_path = session_marker_;
        lease_path.replace_extension(".lock");
        session_lease_.reset();
        fs::remove(lease_path, error);
        session_marker_.clear();
    }
    if (initialized_)
    {
        App::shutdown();
    }
    initialized_ = false;
    project_identity_.clear();
    resource_revision_.clear();
    builtin_path_.clear();
}

bool EditorEnvironment::is_initialized() const noexcept
{
    return initialized_ && project_.is_initialized() && App::is_initialized() &&
           App::assets_path() == builtin_path_;
}

bool EditorEnvironment::refresh_resources(GameplaySession& gameplay)
{
    if (!is_initialized())
    {
        return false;
    }
    ResourceCatalog catalog;
    if (!ResourceSystem::read_catalog(builtin_path_, catalog))
    {
        return false;
    }
    return (!session_marker_.empty() && catalog.revision() == resource_revision_) ||
           apply_resources(catalog.data(), gameplay);
}

bool EditorEnvironment::apply_resources(const schema::AssetCatalogDataT& data,
                                        GameplaySession& gameplay)
{
    if (!is_initialized() || session_marker_.empty() || !session_lease_ ||
        data.target_platform != ENGINE_TARGET_PLATFORM ||
        !gameplay.set_resources(data, {{"content", builtin_path_}}))
    {
        return false;
    }
    resource_revision_ = data.revision;
    return true;
}

} // namespace engine::editor
