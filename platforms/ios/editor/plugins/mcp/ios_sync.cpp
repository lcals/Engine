#include "plugins/editor/mcp/project_mcp_tools.hpp"

#include "editor/file_text.hpp"
#include "ios/editor/plugins/mcp/ios_container.hpp"
#include "plugins/editor/asset_sync/asset_sync_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <fstream>
#include <system_error>

namespace engine::plugins::mcp
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

std::string nonce()
{
    static std::atomic_uint64_t counter{0};
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
           std::to_string(counter.fetch_add(1));
}

bool write_text(const fs::path& path, std::string_view text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    return !output.fail();
}

bool manifest_valid(const Json& value)
{
    return value.is_object() && value.contains("revision") && value["revision"].is_string() &&
           value.contains("entry") && value["entry"].is_string() && value.contains("files") &&
           value["files"].is_array() && value.contains("deleted") && value["deleted"].is_array();
}

void set_reply(const McpTarget& target, const ios_detail::IosContainer& container, Json result,
               std::string& reply)
{
    result["targetPlatform"] = "ios";
    result["deviceUdid"] = target.ios_udid;
    result["bundleIdentifier"] = target.ios_bundle_identifier;
    result["applicationName"] = target.ios_application_name;
    result["iosTarget"] = target.ios_simulator ? "simulator" : "device";
    if (!container.runtime_dir().empty())
    {
        result["runtimeDir"] = container.runtime_dir();
    }
    reply = result.dump(-1, ' ', false, Json::error_handler_t::replace);
}
} // namespace

bool ProjectMcpTools::sync_ios(const fs::path& source, std::string& reply)
{
    ios_detail::IosContainer container(target_);
    const auto token = nonce();
    const auto staging = "assets/.staging-" + token;
    const auto temporary_manifest = "assets/.available-" + token + ".json";
    fs::path mirror;
    bool mirror_owned = false;
    bool staging_owned = false;
    bool manifest_owned = false;
    const auto cleanup = [&]()
    {
        bool success = true;
        std::string ignored;
        if (staging_owned)
        {
            success = container.remove(staging, ignored) && success;
        }
        if (manifest_owned)
        {
            success = container.remove(temporary_manifest, ignored) && success;
        }
        if (mirror_owned)
        {
            std::error_code code;
            fs::remove_all(mirror, code);
            success = !code && success;
        }
        return success;
    };
    const auto fail = [&](std::string error)
    {
        if (!cleanup())
        {
            error += "; owned iOS staging cleanup was incomplete";
        }
        set_reply(target_, container, {{"error", std::move(error)}}, reply);
        return false;
    };
    ResourceCatalog catalog;
    if (!ResourceSystem::read_catalog(source, catalog) || catalog.data().target_platform != "ios")
    {
        return fail("iOS sync requires a complete runtime catalog built for ios");
    }
    std::string error;
    if (!container.open(error) || !container.lock(error))
    {
        return fail(error);
    }
    std::error_code code;
    const auto temporary_root = fs::temp_directory_path(code);
    const auto canonical_root = code ? fs::path{} : fs::canonical(temporary_root, code);
    mirror = canonical_root / ("engine-ios-sync-" + token);
    if (code || !fs::create_directory(mirror, code) || code)
    {
        return fail("Cannot create an owned temporary iOS resource mirror");
    }
    mirror_owned = true;
    fs::create_directory(mirror / "assets", code);
    std::string baseline;
    bool exists = false;
    if (code || !container.read("assets/available.json", baseline, exists, error))
    {
        return fail(error.empty() ? "Cannot prepare the iOS resource baseline" : error);
    }
    const auto published = exists ? Json::parse(baseline, nullptr, false) : Json{};
    if (exists &&
        (!manifest_valid(published) || !write_text(mirror / "assets/available.json", baseline)))
    {
        return fail("The published iOS resource manifest is invalid or inaccessible");
    }
    // catalog、依赖载体、完整版本号及删除清单只由现有同步系统生成。
    if (!sync_.sync(source, mirror))
    {
        return fail(sync_.last_error());
    }
    const auto revision = sync_.revision();
    const auto manifest_path = mirror / "assets/available.json";
    std::string manifest_text;
    if (!engine::editor::file_detail::read_text(manifest_path, manifest_text))
    {
        return fail("Cannot read the generated iOS resource manifest");
    }
    const auto manifest = Json::parse(manifest_text, nullptr, false);
    if (!manifest_valid(manifest))
    {
        return fail("The generated iOS resource manifest is invalid");
    }
    const auto version = "assets/versions/" + revision;
    bool reuse = false;
    if (!container.make_directory("assets/versions", error) ||
        !container.directory(version, reuse, error))
    {
        return fail(error);
    }
    if (!reuse)
    {
        bool collision = false;
        if (!container.directory(staging, collision, error) || collision ||
            !container.make_directory(staging, error))
        {
            return fail(error.empty() ? "The iOS staging directory already exists" : error);
        }
        staging_owned = true;
    }
    for (const auto& item : manifest["files"])
    {
        if (!item.is_object() || !item.contains("path") || !item["path"].is_string() ||
            !item.contains("hash") || !item["hash"].is_string() || !item.contains("size") ||
            !item["size"].is_number_unsigned())
        {
            return fail("The generated iOS file manifest has invalid fields");
        }
        const auto path = item["path"].get<std::string>();
        const auto destination = (reuse ? version : staging) + "/" + path;
        if ((!reuse &&
             !container.upload(mirror / "assets/versions" / revision / PathUtils::from_utf8(path),
                               destination, error)) ||
            !container.verify(destination, item["size"].get<std::uint64_t>(),
                              item["hash"].get<std::string>(), error))
        {
            return fail(error);
        }
    }
    if (reuse && published == manifest)
    {
        Json result = {{"availableRevision", revision},
                       {"unchanged", true},
                       {"activation", "pending engine decision"}};
        if (!cleanup())
        {
            result["warning"] = "Owned iOS temporary mirror cleanup was incomplete";
        }
        set_reply(target_, container, std::move(result), reply);
        return true;
    }
    if (!reuse)
    {
        if (!container.rename(staging, version, error))
        {
            return fail(error);
        }
        staging_owned = false;
    }
    std::string received;
    if (!container.read(temporary_manifest, received, exists, error) || exists)
    {
        return fail(error.empty() ? "The iOS manifest staging path already exists" : error);
    }
    manifest_owned = true;
    if (!container.upload(manifest_path, temporary_manifest, error) ||
        !container.read(temporary_manifest, received, exists, error) || !exists ||
        received != manifest_text)
    {
        return fail(error.empty() ? "The iOS staged manifest verification failed" : error);
    }
    if (!container.rename(temporary_manifest, "assets/available.json", error))
    {
        return fail("iOS manifest publication was not confirmed: " + error);
    }
    manifest_owned = false;
    Json result = {{"availableRevision", revision}, {"activation", "pending engine decision"}};
    if (!cleanup())
    {
        result["warning"] = "Version published, but owned iOS staging cleanup was incomplete";
    }
    set_reply(target_, container, std::move(result), reply);
    return true;
}

bool ProjectMcpTools::set_ios_hot_reload(bool enabled, std::string& reply)
{
    ios_detail::IosContainer container(target_);
    std::string error;
    if (!container.open(error))
    {
        set_reply(target_, container, {{"error", error}}, reply);
        return false;
    }
    const auto token = nonce();
    const auto local = project_root_ / "cache" / (".ios-control-" + token + ".json");
    const auto remote = ".script-control-" + token + ".json";
    const auto text = Json({{"hotReload", enabled}}).dump() + "\n";
    std::string received;
    bool exists = false;
    bool success = write_text(local, text);
    if (success)
    {
        success = container.upload(local, remote, error) &&
                  container.read(remote, received, exists, error) && exists && received == text;
    }
    if (success)
    {
        success = container.rename(remote, "script-control.json", error);
        if (!success)
        {
            error = "iOS hot reload control publication was not confirmed: " + error;
        }
    }
    std::error_code code;
    fs::remove(local, code);
    std::string cleanup_error;
    const bool cleaned = container.remove(remote, cleanup_error);
    if (!success)
    {
        if (error.empty())
        {
            error = "Cannot prepare or verify the iOS hot reload control";
        }
        if (!cleaned)
        {
            error += "; owned iOS control staging cleanup was incomplete";
        }
        set_reply(target_, container, {{"error", error}}, reply);
        return false;
    }
    Json result = {{"requestedHotReload", enabled}, {"activation", "pending engine decision"}};
    if (!cleaned || code)
    {
        result["warning"] =
            "Control published, but owned iOS control staging cleanup was incomplete";
    }
    set_reply(target_, container, std::move(result), reply);
    return true;
}

bool ProjectMcpTools::read_ios_status(std::string& reply)
{
    ios_detail::IosContainer container(target_);
    std::string error;
    const auto fail = [&]()
    {
        set_reply(target_, container, {{"error", error}}, reply);
        return false;
    };
    if (!container.open(error))
    {
        return fail();
    }
    Json result = {{"running", nullptr},
                   {"state", "unreported"},
                   {"activeRevision", ""},
                   {"availableRevision", ""}};
    std::string text;
    bool exists = false;
    if (!container.read("script-status.json", text, exists, error))
    {
        return fail();
    }
    if (exists)
    {
        result = Json::parse(text, nullptr, false);
        if (!result.is_object() || !result.contains("running") || !result["running"].is_boolean())
        {
            error = "The iOS runtime status is invalid";
            return fail();
        }
        result["state"] =
            result["running"].get<bool>() ? "lastReportedRunning" : "lastReportedStopped";
    }
    if (!container.read("assets/available.json", text, exists, error))
    {
        return fail();
    }
    result["availableRevision"] = "";
    if (exists)
    {
        const auto available = Json::parse(text, nullptr, false);
        if (!manifest_valid(available))
        {
            error = "The iOS available resource manifest is invalid";
            return fail();
        }
        result["availableRevision"] = available["revision"];
    }
    result["statusSource"] = "app-container files: availableRevision is published; runtime fields "
                             "are the last engine report";
    set_reply(target_, container, std::move(result), reply);
    return true;
}
} // namespace engine::plugins::mcp
