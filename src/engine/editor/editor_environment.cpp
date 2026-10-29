#include "editor/editor_environment.hpp"

#include "editor/asset_pipeline/project_lock.hpp"
#include "editor/project/project_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/resource/resource_system.hpp"

#include "runtime/utils/path_utils.hpp"

#include <nlohmann/json.hpp>

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
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char value : project_identity)
    {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string suffix(16, '0');
    for (std::size_t index = suffix.size(); index != 0; --index)
    {
        suffix[index - 1] = digits[hash & 15U];
        hash >>= 4U;
    }
    return "engine-editor-" + suffix;
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
    config.builtin_assets_path = root / "cache";
    if (!App::initialize(config))
    {
        shutdown();
        return false;
    }
    builtin_path_ = App::builtin_assets_path();
    initialized_ = true;
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
           App::builtin_assets_path() == builtin_path_;
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
    if (!is_initialized())
    {
        return false;
    }
    if (!gameplay.set_resources(data, {{"content", builtin_path_}}))
    {
        return false;
    }
    if (session_marker_.empty())
    {
        const auto directory = builtin_path_ / "editor-hosts";
        std::error_code error;
        fs::create_directories(directory, error);
        if (error || fs::is_symlink(fs::symlink_status(directory, error)) || error)
        {
            return false;
        }
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto candidate = directory / (std::to_string(nonce) + ".json");
        const auto temporary = directory / (std::to_string(nonce) + ".tmp");
        const auto lease_path = directory / (std::to_string(nonce) + ".lock");
        for (const auto& path : {candidate, temporary, lease_path})
        {
            const auto status = fs::symlink_status(path, error);
            if (error != std::errc::no_such_file_or_directory &&
                (error || status.type() != fs::file_type::not_found))
            {
                return false;
            }
            error.clear();
        }
        auto lease = std::make_unique<asset_pipeline::ProjectLock>();
        std::string lock_error;
        if (!lease->acquire(lease_path, lock_error))
        {
            LogSystem::write(LogLevel::error, "Cannot acquire editor host lease: " + lock_error);
            return false;
        }
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << nlohmann::json({{"projectRoot", project_identity_},
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
            return false;
        }
        session_marker_ = candidate;
        session_lease_ = std::move(lease);
    }
    resource_revision_ = data.revision;
    return true;
}

} // namespace engine::editor
