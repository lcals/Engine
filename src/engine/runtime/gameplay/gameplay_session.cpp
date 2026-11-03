#include "runtime/gameplay/gameplay_session.hpp"
#include "runtime/font/font_system.hpp"
#include "runtime/ui/ui_system.hpp"

#include "runtime/animation/animation_system.hpp"
#include "runtime/audio/audio_system.hpp"
#include "runtime/physics/physics_system.hpp"

#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/world/world_system.hpp"

#include "runtime/app/app.hpp"
#include "runtime/gameplay/script_runtime.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/performance/profiling.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

namespace engine
{
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

struct Snapshot
{
    ResourceCatalog catalog;
    MountList mounts;
    std::string revision;
    std::string entry = "scripts/game.js";
};

struct ControlRead
{
    std::string text;
    fs::file_time_type written{};
    bool exists = false;
    bool valid = false;
    bool hot_reload = false;
    std::string error;
};

ControlRead read_control(const fs::path& root)
{
    ControlRead output;
    const auto file = root / "script-control.json";
    std::error_code error;
    output.exists = fs::exists(file, error);
    if (!output.exists && !error)
    {
        return output;
    }
    if (error || !read_text(file, output.text))
    {
        output.error = "Cannot read script control";
        return output;
    }
    output.written = fs::last_write_time(file, error);
    if (error)
    {
        output.error = "Cannot inspect script control timestamp";
        return output;
    }
    const auto document = Json::parse(output.text, nullptr, false);
    if (!document.is_object() || !document.contains("hotReload") ||
        !document["hotReload"].is_boolean())
    {
        output.error = "Script control requires a boolean hotReload";
        return output;
    }
    output.valid = true;
    output.hot_reload = document["hotReload"].get<bool>();
    return output;
}

bool write_status(const fs::path& root, const std::string& text, std::string& message)
{
    const auto target = root / "script-status.json";
    const auto temporary = root / "script-status.json.tmp";
    std::error_code error;
    fs::remove(temporary, error);
    if (error)
    {
        message = "Cannot remove temporary script status: " + error.message();
        return false;
    }
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file << text;
    file.close();
    if (!file)
    {
        message = "Cannot write script status";
        return false;
    }
    if (!SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(), PathUtils::to_utf8(target).c_str()))
    {
        message = std::string("Cannot publish script status: ") + SDL_GetError();
        return false;
    }
    return true;
}

struct DeploymentRead
{
    std::string last_manifest;
    std::string current_revision;
    std::unique_ptr<Snapshot> available;
    std::string error;

    bool fail(std::string message)
    {
        error = std::move(message);
        return false;
    }

    [[nodiscard]] bool read(const fs::path& persistent_root,
                            const std::atomic<bool>* cancelled = nullptr)
    {
        const fs::path root = persistent_root / "assets";
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
        if (candidate->revision == current_revision)
        {
            return fail("A published revision is immutable; publish a new revision");
        }
        fs::path version_root;
        if (!contained_file(persistent_root, "assets/versions/" + candidate->revision, version_root,
                            true))
        {
            return fail("Missing or escaping asset version directory");
        }
        std::set<std::string> seen;
        for (const auto& item : document["files"])
        {
            if (cancelled && cancelled->load(std::memory_order_relaxed))
            {
                return fail("Deployment verification cancelled");
            }
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
};

} // namespace

struct GameplaySession::State
{
    struct BackgroundIo
    {
        std::atomic<bool> cancelled{false};
        std::uint64_t generation = 0;
        ControlRead control;
        DeploymentRead deployment;
        std::string status_text;
        std::string write_error;
        bool status_written = false;
        bool ready = false; // 仅完成委托在所属线程写入。
    };

    GameplaySession& owner;
    fs::path persistent_root;
    std::shared_ptr<BackgroundIo> pending;
    std::uint64_t generation = 1;
    std::string desired_status;
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
    bool hot_reload = false;

    explicit State(GameplaySession& session)
        : owner(session),
          persistent_root(App::is_initialized() ? App::persistent_data_path() : fs::path{})
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
        const auto* status = owner.script_status();
        if (status == nullptr)
        {
            return;
        }
        const Json document = {
            {"running", status->running},
            {"runId", std::to_string(status->run_id)},
            {"hotReload", hot_reload},
            {"activeRevision", status->active_revision},
            {"availableRevision", available ? available->revision : ""},
            {"lastError", owner.last_error_.empty() ? status->last_error : owner.last_error_}};
        desired_status = document.dump(-1, ' ', false, Json::error_handler_t::replace);
        if (owner.background_ != nullptr || desired_status == last_status)
        {
            return;
        }
        std::string error;
        if (!write_status(persistent_root, desired_status, error))
        {
            (void)fail(std::move(error));
            return;
        }
        last_status = desired_status;
    }

    void accept_control(const ControlRead& control)
    {
        if (!control.error.empty())
        {
            (void)fail(control.error);
            return;
        }
        if (!control.exists)
        {
            last_control.clear();
            control_seen = false;
            return;
        }
        if (!control.valid ||
            (control_seen && control.text == last_control && control.written == last_control_time))
        {
            return;
        }
        last_control = control.text;
        last_control_time = control.written;
        control_seen = true;
        if (control.hot_reload && !hot_reload)
        {
            last_attempt.clear();
        }
        hot_reload = control.hot_reload;
    }

    void poll_control()
    {
        if (App::is_initialized())
        {
            accept_control(read_control(persistent_root));
        }
    }

    void invalidate_background() noexcept
    {
        ++generation;
        if (pending)
        {
            pending->cancelled.store(true, std::memory_order_relaxed);
        }
    }

    bool collect_background()
    {
        if (!pending || !pending->ready)
        {
            return true;
        }
        auto result = std::move(pending);
        if (result->generation != generation)
        {
            return true;
        }
        accept_control(result->control);
        if (result->status_written)
        {
            last_status = result->status_text;
        }
        if (!result->write_error.empty())
        {
            (void)fail(std::move(result->write_error));
        }
        if (!result->deployment.error.empty())
        {
            return fail(std::move(result->deployment.error));
        }
        accept_deployment(result->deployment);
        return true;
    }

    void schedule_background()
    {
        if (owner.background_ == nullptr || pending || persistent_root.empty())
        {
            return;
        }
        auto operation = std::make_shared<BackgroundIo>();
        operation->generation = generation;
        operation->deployment.last_manifest = last_manifest;
        operation->deployment.current_revision = available ? available->revision : "";
        if (desired_status != last_status)
        {
            operation->status_text = desired_status;
        }
        const bool submitted = owner.background_->submit(
            "Gameplay.deployment",
            [operation, root = persistent_root]()
            {
                ENGINE_PROFILE_ZONE("Gameplay.deployment");
                if (operation->cancelled.load(std::memory_order_relaxed))
                {
                    return false;
                }
                operation->control = read_control(root);
                const bool valid = operation->deployment.read(root, &operation->cancelled);
                if (!operation->status_text.empty() &&
                    !operation->cancelled.load(std::memory_order_relaxed))
                {
                    operation->status_written =
                        write_status(root, operation->status_text, operation->write_error);
                }
                return valid;
            },
            [operation](bool) { operation->ready = true; }, TaskPriority::low);
        if (submitted)
        {
            pending = std::move(operation);
        }
    }

    [[nodiscard]] bool poll_available()
    {
        DeploymentRead result;
        result.last_manifest = last_manifest;
        result.current_revision = available ? available->revision : "";
        if (!result.read(persistent_root))
        {
            return fail(std::move(result.error));
        }
        accept_deployment(result);
        return true;
    }

    void accept_deployment(DeploymentRead& result)
    {
        last_manifest = std::move(result.last_manifest);
        if (result.available)
        {
            available = std::move(result.available);
        }
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
        owner.release_scene();
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

GameplaySession::GameplaySession(WorldSystem& world, RenderSystem& renderer, ScriptRuntime* script,
                                 PhysicsSystem* physics, AudioSystem* audio,
                                 AnimationSystem* animation, FontSystem* fonts, UiSystem* ui,
                                 TaskSystem* background) noexcept
    : world_(world), renderer_(renderer), script_(script), physics_(physics), audio_(audio),
      animation_(animation), fonts_(fonts), ui_(ui), background_(background)
{
}

GameplaySession::~GameplaySession() noexcept
{
    stop();
    if (background_ && state_)
    {
        // 关闭边界可以等待；先收尾旧写入，再确保磁盘上的最终状态为 stopped。
        (void)background_->wait();
        if (!state_->desired_status.empty())
        {
            std::string error;
            if (!write_status(state_->persistent_root, state_->desired_status, error))
            {
                (void)state_->fail(std::move(error));
            }
        }
    }
}

bool GameplaySession::set_resources(const schema::AssetCatalogDataT& data, const MountList& mounts,
                                    std::string_view entry)
{
    auto* resources = ResourceSystem::instance();
    if (resources == nullptr)
    {
        last_error_ = "Gameplay resource services are not initialized";
        return false;
    }
    if (script_ != nullptr && script_->status().running)
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
        candidate->hot_reload = state_->hot_reload;
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
    if (state_)
    {
        state_->invalidate_background();
        candidate->generation = state_->generation;
        candidate->pending = std::move(state_->pending);
        candidate->last_status = state_->last_status;
    }
    state_ = std::move(candidate);
    last_error_.clear();
    return true;
}

bool GameplaySession::start(std::string_view entry)
{
    last_error_.clear();
    if (script_ == nullptr)
    {
        last_error_ = "No script runtime was provided by the host";
        return false;
    }
    if (!world_.is_initialized() || !renderer_.is_initialized() || !script_->is_initialized() ||
        !App::is_initialized())
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
        candidate->builtin.mounts = {{"content", App::assets_path()}};
        if (!ResourceSystem::read_catalog(App::assets_path(), candidate->builtin.catalog))
        {
            last_error_ = "Cannot read the built-in runtime catalog";
            return false;
        }
        candidate->builtin.revision = candidate->builtin.catalog.revision();
        state_ = std::move(candidate);
    }
    state_->invalidate_background();
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
    state_->schedule_background();
    return result;
}

bool GameplaySession::tick()
{
    if (!state_ || script_ == nullptr || !App::is_initialized())
    {
        return true;
    }
    if (!background_)
    {
        state_->poll_control();
    }
    const bool complete = background_ ? state_->collect_background() : state_->poll_available();
    if (state_->gameplay_requested && !script_->status().running)
    {
        state_->gameplay_requested = false;
        release_scene();
    }
    if (complete && state_->gameplay_requested && state_->hot_reload && state_->available &&
        state_->available->revision != script_->status().active_revision &&
        state_->available->revision != state_->last_attempt)
    {
        state_->last_attempt = state_->available->revision;
        (void)state_->apply(*state_->available);
    }
    state_->publish();
    state_->schedule_background();
    return true;
}

void GameplaySession::release_scene() noexcept
{
    if (ui_)
    {
        ui_->release_scene();
    }
    if (fonts_)
    {
        fonts_->release_scene();
    }
    if (animation_ != nullptr)
    {
        animation_->clear();
    }
    if (audio_ != nullptr)
    {
        audio_->clear();
    }
    if (physics_ != nullptr)
    {
        physics_->clear();
    }
    world_.destroy_scene();
    renderer_.release_scene();
}

void GameplaySession::stop(std::string_view reason) noexcept
{
    if (!reason.empty())
    {
        last_error_ = reason;
    }
    if (state_)
    {
        state_->gameplay_requested = false;
        state_->invalidate_background();
    }
    if (script_ != nullptr)
    {
        script_->stop_gameplay();
    }
    release_scene();
    if (state_)
    {
        state_->publish();
        state_->schedule_background();
    }
}

bool GameplaySession::execute_project_script(const ProjectScriptRequest& request,
                                             std::string& result_json, std::string& error)
{
    result_json.clear();
    error.clear();
    if (script_ == nullptr)
    {
        error = "Project script execution requires the script plugin";
        return false;
    }
    return script_->execute_project_script(request, result_json, error);
}

const ScriptStatus* GameplaySession::script_status() const noexcept
{
    if (script_ == nullptr)
    {
        return nullptr;
    }
    return &script_->status();
}

std::string_view GameplaySession::last_error() const noexcept
{
    return last_error_;
}

} // namespace engine
