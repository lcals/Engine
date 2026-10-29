#include "editor/editor_environment.hpp"

#include "editor/project/project_system.hpp"
#include "runtime/app/app.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <utility>

namespace engine
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

bool EditorEnvironment::initialize(bool snapshot_builtin_assets)
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
    if (snapshot_builtin_assets)
    {
        static std::atomic<std::uint64_t> sequence = 0;
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto candidate =
            root / "cache" / "editor-builtin" /
            (std::to_string(nonce) + "-" + std::to_string(sequence.fetch_add(1)));
        fs::create_directories(candidate.parent_path(), error);
        if (error || !fs::create_directory(candidate, error))
        {
            project_identity_.clear();
            return false;
        }
        snapshot_directory_ = candidate;
        config.builtin_assets_path = snapshot_directory_ / "assets";
    }
    else
    {
        config.builtin_assets_path = root / "assets";
    }
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
    if (!snapshot_directory_.empty())
    {
        std::error_code error;
        fs::remove_all(snapshot_directory_, error);
        if (error)
        {
            LogSystem::write(LogLevel::warning,
                             "Cannot remove editor built-in snapshot: " + error.message());
        }
        snapshot_directory_.clear();
    }
    if (initialized_)
    {
        App::shutdown();
    }
    initialized_ = false;
    snapshot_prepared_ = false;
    project_identity_.clear();
    builtin_path_.clear();
}

bool EditorEnvironment::is_initialized() const noexcept
{
    return initialized_ && project_.is_initialized() && App::is_initialized() &&
           App::builtin_assets_path() == builtin_path_;
}

bool EditorEnvironment::prepare_builtin_assets()
{
    if (!is_initialized() || snapshot_directory_.empty() || snapshot_prepared_)
    {
        return false;
    }
    const auto source = PathUtils::from_utf8(project_identity_) / "cache" / "assets";
    std::error_code error;
    fs::copy(source, builtin_path_, fs::copy_options::recursive, error);
    if (error)
    {
        std::error_code ignored;
        fs::remove_all(builtin_path_, ignored);
        return false;
    }
    snapshot_prepared_ = true;
    return true;
}

} // namespace engine
