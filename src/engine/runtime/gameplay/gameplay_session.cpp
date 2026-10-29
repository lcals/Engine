#include "runtime/gameplay/gameplay_session.hpp"

#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/world/world_system.hpp"

#if ENGINE_HAS_SCRIPT_RUNTIME
#include "runtime/app/app.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/script/script_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <system_error>
#include <utility>
#include <vector>
#endif

namespace engine
{
#if ENGINE_HAS_SCRIPT_RUNTIME
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr std::size_t maximum_text_size = 4 * 1024 * 1024;

[[nodiscard]] bool read_text(const fs::path& path, std::string& text)
{
    if (ResourceSystem::read_text(path, text) && text.size() <= maximum_text_size)
    {
        return true;
    }
    text.clear();
    return false;
}

[[nodiscard]] bool relative_path(std::string_view input, std::string& normalized)
{
    normalized.clear();
    if (input.empty() || input.find_first_of("\\:") != std::string_view::npos ||
        input.find('\0') != std::string_view::npos)
    {
        return false;
    }
    const fs::path path = PathUtils::from_utf8(input);
    if (path.is_absolute() || path.has_root_path())
    {
        return false;
    }
    const auto clean = path.lexically_normal();
    for (const auto& part : clean)
    {
        if (part == "..")
        {
            return false;
        }
    }
    normalized = PathUtils::to_utf8(clean);
    return !normalized.empty() && normalized != "." && normalized.back() != '/';
}

[[nodiscard]] bool safe_revision(std::string_view value)
{
    if (value.empty() || value.size() > 128 || value == "." || value == ".." || value == "builtin")
    {
        return false;
    }
    for (const char character : value)
    {
        if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '-' || character == '_' ||
              character == '.'))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool contained_file(const fs::path& root, std::string_view relative, fs::path& path,
                                  bool directory = false)
{
    std::error_code error;
    const auto canonical_root = fs::canonical(root, error);
    if (error || !fs::is_directory(canonical_root, error) || error)
    {
        return false;
    }
    const auto candidate = fs::canonical(canonical_root / PathUtils::from_utf8(relative), error);
    if (error)
    {
        return false;
    }
    auto component = candidate.begin();
    for (const auto& root_component : canonical_root)
    {
        if (component == candidate.end() || *component != root_component)
        {
            return false;
        }
        ++component;
    }
    const bool correct_type =
        directory ? fs::is_directory(candidate, error) : fs::is_regular_file(candidate, error);
    if (component == candidate.end() || !correct_type || error)
    {
        return false;
    }
    path = candidate;
    return true;
}

#if defined(SDL_PLATFORM_ANDROID)
[[nodiscard]] bool builtin_file(const fs::path& root, std::string_view relative, fs::path& path)
{
    const auto root_text = PathUtils::to_utf8(root);
    constexpr std::string_view prefix = "apk:/assets";
    if (root_text.starts_with("apk:"))
    {
        std::string normalized;
        if (root_text != prefix)
        {
            if (!root_text.starts_with("apk:/assets/") ||
                !relative_path(std::string_view(root_text).substr(prefix.size() + 1), normalized) ||
                normalized != std::string_view(root_text).substr(prefix.size() + 1))
            {
                return false;
            }
        }
        if (!relative_path(relative, normalized) || normalized != relative)
        {
            return false;
        }
        path = PathUtils::from_utf8(root_text + "/" + normalized);
        return true;
    }
    return contained_file(root, relative, path);
}

#endif

[[nodiscard]] bool matches_file(const fs::path& path, std::uint64_t size, std::string_view hash)
{
    std::uint64_t value = 0;
    std::uint64_t actual_size = 0;
    if (hash.size() != 16 || !VirtualFileSystem::hash_file(path, value, actual_size) ||
        actual_size != size)
    {
        return false;
    }
    constexpr char hex[] = "0123456789abcdef";
    std::array<char, 16> actual_hash{};
    for (std::size_t index = actual_hash.size(); index > 0; --index)
    {
        actual_hash[index - 1] = hex[value & 15];
        value >>= 4;
    }
    return hash == std::string_view(actual_hash.data(), actual_hash.size());
}

[[nodiscard]] bool script_path(std::string_view path)
{
    return path.ends_with(".js") || path.ends_with(".mjs");
}

#if defined(SDL_PLATFORM_ANDROID)
[[nodiscard]] bool prepare_directory(const fs::path& root, std::string_view relative,
                                     fs::path& directory)
{
    std::error_code error;
    fs::create_directories(root / PathUtils::from_utf8(relative), error);
    return !error && contained_file(root, relative, directory, true);
}

[[nodiscard]] bool cache_file(const fs::path& directory, std::string_view name,
                              std::string_view bytes)
{
    const fs::path target = directory / PathUtils::from_utf8(name);
    std::error_code error;
    const auto status = fs::symlink_status(target, error);
    std::string previous;
    if (!error && fs::is_regular_file(status) && ResourceSystem::read_text(target, previous) &&
        previous == bytes)
    {
        return true;
    }

    // 临时文件位于已校验目录；先移除残留链接，发布时替换目标本身而不跟随目标链接。
    const fs::path temporary = directory / PathUtils::from_utf8(std::string(name) + ".tmp");
    fs::remove(temporary, error);
    if (error)
    {
        return false;
    }
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file ||
        !SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(), PathUtils::to_utf8(target).c_str()))
    {
        fs::remove(temporary, error);
        return false;
    }
    return true;
}

[[nodiscard]] bool prepare_builtin_manifest(std::string& entry, std::set<std::string>& deleted,
                                            std::string& message)
{
    if (!App::is_initialized())
    {
        message = "Android scripts require initialized App paths";
        return false;
    }
    std::string text;
    if (!read_text(PathUtils::from_utf8("apk:/assets/project-manifest.json"), text))
    {
        message = "Cannot read APK project manifest";
        return false;
    }
    const auto document = Json::parse(text, nullptr, false);
    if (!document.is_object() || !document.contains("revision") ||
        !document["revision"].is_string() || !document.contains("entry") ||
        !document["entry"].is_string() || !document.contains("files") ||
        !document["files"].is_array() || !document.contains("deleted") ||
        !document["deleted"].is_array())
    {
        message = "Invalid APK project manifest";
        return false;
    }
    const auto manifest_entry = document["entry"].get<std::string>();
    std::string normalized;
    if (!safe_revision(document["revision"].get<std::string>()) ||
        !relative_path(manifest_entry, normalized) || normalized != manifest_entry ||
        !script_path(manifest_entry))
    {
        message = "Unsafe APK project revision or entry path";
        return false;
    }
    std::set<std::string> seen;
    for (const auto& item : document["files"])
    {
        if (!item.is_object() || !item.contains("path") || !item["path"].is_string() ||
            !item.contains("size") || !item["size"].is_number_unsigned() ||
            !item.contains("hash") || !item["hash"].is_string())
        {
            message = "Invalid APK project file description";
            return false;
        }
        const auto path = item["path"].get<std::string>();
        fs::path file;
        if (!relative_path(path, normalized) || normalized != path || !seen.insert(path).second ||
            !builtin_file(App::builtin_assets_path(), path, file))
        {
            message = "APK project file failed path, size or hash check: " + path;
            return false;
        }
    }
    for (const auto& item : document["deleted"])
    {
        if (!item.is_string())
        {
            message = "Invalid deleted APK project path";
            return false;
        }
        const auto path = item.get<std::string>();
        if (!relative_path(path, normalized) || normalized != path || seen.contains(path) ||
            !deleted.insert(path).second)
        {
            message = "Unsafe or conflicting deleted APK project path: " + path;
            return false;
        }
    }
    if (seen.contains("catalog.bin"))
    {
        ResourceCatalog catalog;
        if (!ResourceSystem::read_catalog(App::builtin_assets_path(), catalog))
        {
            message = "APK GUID resource catalog is invalid";
            return false;
        }
        bool has_entry = false;
        std::set<std::string> expected{"catalog.bin"};
        for (const auto& asset : catalog.data().entries)
        {
            expected.insert(asset->location->path);
            has_entry = has_entry || (asset->kind == schema::AssetKind::script &&
                                      asset->module_path == manifest_entry);
        }
        if (!has_entry || expected != seen)
        {
            message = "APK manifest does not match its complete GUID resource catalog";
            return false;
        }
    }
    else if (!seen.contains(manifest_entry))
    {
        message = "APK project entry is missing from the complete manifest";
        return false;
    }
    fs::path directory;
    if (!prepare_directory(App::persistent_data_path(), "assets", directory) ||
        !cache_file(directory, "builtin.json", text))
    {
        message = "Cannot export APK project manifest to persistent assets/builtin.json";
        return false;
    }
    entry = manifest_entry;
    return true;
}
#endif

struct Snapshot
{
    ResourceCatalog catalog;
    MountList mounts;
    std::string revision;
    std::string entry = "scripts/game.js";
};

} // namespace

struct GameplaySession::State
{
    GameplaySession& owner;
    Snapshot builtin;
    std::unique_ptr<Snapshot> available;
    std::string requested_entry;
    std::string last_manifest;
    std::string last_control;
    fs::file_time_type last_control_time{};
    bool control_seen = false;
    std::string last_attempt;
    std::string last_status;
    bool gameplay_requested = false;
    mutable ScriptStatus reported;

    explicit State(GameplaySession& session) : owner(session)
    {
    }

    bool fail(std::string message)
    {
        if (owner.last_error_ != message)
        {
            LogSystem::write(LogLevel::error, message);
            owner.last_error_ = std::move(message);
        }
        return false;
    }

    void publish()
    {
        if (!App::is_initialized())
        {
            return;
        }
        const auto& status = *owner.script_status();
        const Json document = {{"running", status.running},
                               {"hotReload", status.hot_reload},
                               {"activeRevision", status.active_revision},
                               {"availableRevision", status.available_revision},
                               {"lastError", status.last_error}};
        const std::string text = document.dump(-1, ' ', false, Json::error_handler_t::replace);
        if (text == last_status)
        {
            return;
        }
        const fs::path target = App::persistent_data_path() / "script-status.json";
        const fs::path temporary = App::persistent_data_path() / "script-status.json.tmp";
        std::error_code error;
        fs::remove(temporary, error);
        if (error)
        {
            (void)fail("Cannot remove temporary script status: " + error.message());
            return;
        }
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << text;
        file.close();
        if (!file)
        {
            (void)fail("Cannot write script status");
            return;
        }
        if (!SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(),
                            PathUtils::to_utf8(target).c_str()))
        {
            (void)fail(std::string("Cannot publish script status: ") + SDL_GetError());
            return;
        }
        last_status = text;
    }

    void poll_control()
    {
        if (!App::is_initialized())
        {
            return;
        }
        const fs::path file = App::persistent_data_path() / "script-control.json";
        std::error_code error;
        if (!fs::exists(file, error) && !error)
        {
            last_control.clear();
            control_seen = false;
            return;
        }
        std::string text;
        if (error || !read_text(file, text))
        {
            (void)fail("Cannot read script control");
            return;
        }
        const auto written = fs::last_write_time(file, error);
        if (error)
        {
            (void)fail("Cannot inspect script control timestamp");
            return;
        }
        if (control_seen && text == last_control && written == last_control_time)
        {
            return;
        }
        last_control = text;
        last_control_time = written;
        control_seen = true;
        const auto document = Json::parse(text, nullptr, false);
        if (!document.is_object() || !document.contains("hotReload") ||
            !document["hotReload"].is_boolean())
        {
            (void)fail("Script control requires a boolean hotReload");
            return;
        }
        const bool enabled = document["hotReload"].get<bool>();
        if (enabled && !owner.script_->status().hot_reload)
        {
            last_attempt.clear();
        }
        owner.script_->set_hot_reload(enabled);
    }

    [[nodiscard]] bool poll_available()
    {
        const fs::path root = App::persistent_data_path() / "assets";
        const fs::path manifest = root / "available.json";
        std::error_code error;
        if (!fs::exists(manifest, error) && !error)
        {
            // 已接受的完整版本不因发布文件暂时缺失而退回内置版本。
            return true;
        }
        std::string text;
        if (error || !read_text(manifest, text))
        {
            return fail("Cannot read asset version manifest");
        }
        if (text == last_manifest)
        {
            return true;
        }
        const auto document = Json::parse(text, nullptr, false);
        if (!document.is_object() || !document.contains("revision") ||
            !document["revision"].is_string() || !document.contains("entry") ||
            !document["entry"].is_string() || !document.contains("files") ||
            !document["files"].is_array() || !document.contains("deleted") ||
            !document["deleted"].is_array())
        {
            return fail("Invalid asset version manifest");
        }
        auto candidate = std::make_unique<Snapshot>();
        candidate->revision = document["revision"].get<std::string>();
        const std::string entry = document["entry"].get<std::string>();
        if (!safe_revision(candidate->revision) || !relative_path(entry, candidate->entry) ||
            entry != candidate->entry || !script_path(entry))
        {
            return fail("Unsafe asset revision or entry path");
        }
        if (available && candidate->revision == available->revision)
        {
            return fail("A published revision is immutable; publish a new revision");
        }
        fs::path version_root;
        if (!contained_file(App::persistent_data_path(), "assets/versions/" + candidate->revision,
                            version_root, true))
        {
            return fail("Missing or escaping asset version directory");
        }
        std::set<std::string> seen;
        for (const auto& item : document["files"])
        {
            if (!item.is_object() || !item.contains("path") || !item["path"].is_string() ||
                !item.contains("size") || !item["size"].is_number_unsigned() ||
                !item.contains("hash") || !item["hash"].is_string())
            {
                return fail("Invalid version file description");
            }
            const auto path = item["path"].get<std::string>();
            std::string normalized;
            fs::path file;
            if (!relative_path(path, normalized) || normalized != path ||
                !seen.insert(path).second || !contained_file(version_root, path, file))
            {
                return fail("Asset version file failed path, size or hash check: " + path);
            }
            if (!matches_file(file, item["size"].get<std::uint64_t>(),
                              item["hash"].get<std::string>()))
            {
                return fail("Version asset failed size or hash check: " + path);
            }
        }
        std::set<std::string> deleted;
        for (const auto& item : document["deleted"])
        {
            if (!item.is_string())
            {
                return fail("Invalid deleted asset path");
            }
            const auto path = item.get<std::string>();
            std::string normalized;
            if (!relative_path(path, normalized) || normalized != path || seen.contains(path) ||
                !deleted.insert(path).second)
            {
                return fail("Unsafe or conflicting deleted asset path: " + path);
            }
        }
        if (!seen.contains("catalog.bin") ||
            !ResourceSystem::read_catalog(version_root, candidate->catalog))
        {
            return fail("Published resources require a valid runtime catalog");
        }
        candidate->mounts = {{"content", version_root}};
        bool has_entry = false;
        std::set<std::string> expected{"catalog.bin"};
        for (const auto& asset : candidate->catalog.data().entries)
        {
            if (!asset->location || asset->location->mount != "content")
            {
                return fail("Deployment catalog contains an unsupported mount");
            }
            expected.insert(asset->location->path);
            has_entry = has_entry || (asset->kind == schema::AssetKind::script &&
                                      asset->module_path == candidate->entry);
        }
        if (!has_entry || seen != expected)
        {
            return fail("Version manifest does not match the complete runtime catalog");
        }
        last_manifest = std::move(text);
        available = std::move(candidate);
        return true;
    }

    bool apply(const Snapshot& snapshot)
    {
        auto* resources = ResourceSystem::instance();
        if (resources == nullptr ||
            snapshot.catalog.data().target_platform != resources->catalog().data().target_platform)
        {
            return fail("Runtime resource catalog targets a different platform");
        }
        const auto& entry = requested_entry.empty() ? snapshot.entry : requested_entry;
        if (!owner.script_->prepare_gameplay(snapshot.catalog, snapshot.mounts, entry,
                                             snapshot.revision))
        {
            return fail(owner.script_->status().last_error);
        }
        owner.script_->stop_gameplay();
        owner.world_.destroy_scene();
        owner.renderer_.release_scene();
        if (!resources->update_catalog(snapshot.catalog.data(), snapshot.mounts))
        {
            gameplay_requested = false;
            return fail(resources->last_error());
        }
        if (!owner.script_->start_prepared_gameplay() || !owner.renderer_.prepare_scene())
        {
            const auto reason = owner.script_->status().last_error.empty()
                                    ? "Cannot prepare scene rendering resources"
                                    : owner.script_->status().last_error;
            owner.stop(reason);
            return false;
        }
        owner.last_error_.clear();
        gameplay_requested = true;
        return true;
    }
};
#else
struct GameplaySession::State
{
};
#endif

GameplaySession::GameplaySession(WorldSystem& world, RenderSystem& renderer,
                                 ScriptSystem* script) noexcept
    : world_(world), renderer_(renderer), script_(script)
{
}

GameplaySession::~GameplaySession() noexcept
{
    stop();
}

bool GameplaySession::set_resources(const schema::AssetCatalogDataT& data, const MountList& mounts,
                                    std::string_view entry)
{
#if ENGINE_HAS_SCRIPT_RUNTIME
    auto* resources = ResourceSystem::instance();
    if (resources == nullptr || script_ == nullptr || script_->status().running)
    {
        last_error_ = "Stop gameplay before replacing its resource directory";
        return false;
    }
    auto candidate = std::make_unique<State>(*this);
    if (!candidate->builtin.catalog.assign(data))
    {
        last_error_ = "Invalid prepared resource directory";
        return false;
    }
    candidate->builtin.mounts = mounts;
    candidate->builtin.revision = data.revision;
    candidate->builtin.entry = entry.empty() ? "scripts/game.js" : std::string(entry);
    if (state_)
    {
        candidate->last_manifest = state_->last_manifest;
    }
    else if (App::is_initialized())
    {
        // 第一次直接提交时忽略上次编辑器会话已发布的旧内容。
        std::error_code error;
        const auto manifest = App::persistent_data_path() / "assets" / "available.json";
        if (fs::is_regular_file(manifest, error) && !error)
        {
            if (!read_text(manifest, candidate->last_manifest))
            {
                last_error_ = "Cannot inspect existing deployment manifest";
                return false;
            }
        }
    }
    if (!resources->update_catalog(data, mounts))
    {
        last_error_ = "Cannot submit prepared resource directory: " + resources->last_error();
        return false;
    }
    if (state_ && state_->available && state_->available->catalog.revision() == data.revision)
    {
        candidate->available = std::move(state_->available);
    }
    state_ = std::move(candidate);
    last_error_.clear();
    return true;
#else
    static_cast<void>(data);
    static_cast<void>(mounts);
    static_cast<void>(entry);
    last_error_ = "Script runtime is not enabled";
    return false;
#endif
}

bool GameplaySession::start(std::string_view entry)
{
    last_error_.clear();
#if ENGINE_HAS_SCRIPT_RUNTIME
    if (!world_.is_initialized() || !renderer_.is_initialized() || script_ == nullptr ||
        !script_->is_initialized() || !App::is_initialized())
    {
        last_error_ = "The runtime is not ready for gameplay";
        return false;
    }
    if (script_->status().running)
    {
        return true;
    }
    if (!state_)
    {
        auto candidate = std::make_unique<State>(*this);
        candidate->builtin.mounts = {{"content", App::builtin_assets_path()}};
        candidate->builtin.revision = "builtin";
#if defined(SDL_PLATFORM_ANDROID)
        std::set<std::string> deleted;
        if (!prepare_builtin_manifest(candidate->builtin.entry, deleted, last_error_))
        {
            return false;
        }
#endif
        if (!ResourceSystem::read_catalog(App::builtin_assets_path(), candidate->builtin.catalog))
        {
            last_error_ = "Cannot read the built-in runtime catalog";
            return false;
        }
        state_ = std::move(candidate);
    }
    state_->requested_entry = entry;
    state_->poll_control();
    if (!state_->poll_available())
    {
        state_->publish();
        return false;
    }
    const auto& selected = state_->available ? *state_->available : state_->builtin;
    state_->last_attempt = selected.revision;
    const bool result = state_->apply(selected);
    state_->publish();
    return result;
#else
    static_cast<void>(entry);
    last_error_ = "Script runtime is not enabled";
    return false;
#endif
}

bool GameplaySession::tick()
{
#if ENGINE_HAS_SCRIPT_RUNTIME
    if (!state_ || script_ == nullptr || !App::is_initialized())
    {
        return true;
    }
    state_->poll_control();
    const bool complete = state_->poll_available();
    if (state_->gameplay_requested && !script_->status().running)
    {
        state_->gameplay_requested = false;
        world_.destroy_scene();
        renderer_.release_scene();
    }
    if (complete && state_->gameplay_requested && script_->status().hot_reload &&
        state_->available && state_->available->revision != script_->status().active_revision &&
        state_->available->revision != state_->last_attempt)
    {
        state_->last_attempt = state_->available->revision;
        (void)state_->apply(*state_->available);
    }
    state_->publish();
#endif
    return true;
}

void GameplaySession::stop(std::string_view reason) noexcept
{
    if (!reason.empty())
    {
        last_error_ = reason;
    }
#if ENGINE_HAS_SCRIPT_RUNTIME
    if (state_)
    {
        state_->gameplay_requested = false;
    }
    if (script_ != nullptr)
    {
        script_->stop_gameplay();
    }
#endif
    world_.destroy_scene();
    renderer_.release_scene();
#if ENGINE_HAS_SCRIPT_RUNTIME
    if (state_)
    {
        state_->publish();
    }
#endif
}

const ScriptStatus* GameplaySession::script_status() const noexcept
{
#if ENGINE_HAS_SCRIPT_RUNTIME
    if (script_ == nullptr)
    {
        return nullptr;
    }
    if (!state_)
    {
        return &script_->status();
    }
    state_->reported = script_->status();
    state_->reported.available_revision = state_->available ? state_->available->revision : "";
    if (!last_error_.empty())
    {
        state_->reported.last_error = last_error_;
    }
    return &state_->reported;
#else
    return nullptr;
#endif
}

std::string_view GameplaySession::last_error() const noexcept
{
    return last_error_;
}

} // namespace engine
