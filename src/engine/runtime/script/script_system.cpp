#include "runtime/script/script_system.hpp"

#include "runtime/app/app.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>
#include <libplatform/libplatform.h>
#include <nlohmann/json.hpp>
#include <v8.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
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

constexpr std::string_view engine_module = R"js(
const native = globalThis.__engineNative;
export class Gameplay {
    OnInitialize() { return true; }
    Update() {}
    OnShutdown() {}
}
export class Debug {
    constructor() { throw new TypeError('Debug is static'); }
    static Log(message) { native.Log(message); }
    static LogWarning(message) { native.LogWarning(message); }
    static LogError(message) { native.LogError(message); }
}
export class App {
    constructor() { throw new TypeError('App is static'); }
    static get builtinAssetsPath() { return native.builtinAssetsPath; }
    static get persistentDataPath() { return native.persistentDataPath; }
    static get cachePath() { return native.cachePath; }
}
Object.freeze(Debug);
Object.freeze(App);
)js";

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

[[nodiscard]] bool builtin_file(const fs::path& root, std::string_view relative, fs::path& path)
{
#if defined(SDL_PLATFORM_ANDROID)
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
#endif
    return contained_file(root, relative, path);
}

[[nodiscard]] bool matches_bytes(std::string_view bytes, std::uint64_t size, std::string_view hash)
{
    if (hash.size() != 16 || bytes.size() != size)
    {
        return false;
    }
    std::uint64_t value = 14695981039346656037ULL;
    for (const char byte : bytes)
    {
        value ^= static_cast<unsigned char>(byte);
        value *= 1099511628211ULL;
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

[[nodiscard]] bool matches_file(const fs::path& path, std::uint64_t size, std::string_view hash)
{
    std::vector<std::byte> bytes;
    return ResourceSystem::read_binary(path, bytes) &&
           matches_bytes(
               std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), size,
               hash);
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
            !builtin_file(App::builtin_assets_path(), path, file) ||
            !matches_file(file, item["size"].get<std::uint64_t>(), item["hash"].get<std::string>()))
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
    if (!seen.contains(manifest_entry))
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

[[nodiscard]] bool prepare_icu_path(std::string& path)
{
#if defined(SDL_PLATFORM_ANDROID)
    if (!App::is_initialized())
    {
        return false;
    }
    std::vector<std::byte> bytes;
    fs::path directory;
    if (!ResourceSystem::read_binary(PathUtils::from_utf8("apk:/assets/v8/icudtl.dat"), bytes) ||
        bytes.empty() || !prepare_directory(App::cache_path(), "v8", directory) ||
        !cache_file(directory, "icudtl.dat",
                    std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())))
    {
        return false;
    }
    path = PathUtils::to_utf8(directory / "icudtl.dat");
#else
    const char* base = SDL_GetBasePath();
    if (base == nullptr)
    {
        return false;
    }
    path = PathUtils::to_utf8(PathUtils::from_utf8(base) / "icudtl.dat");
#endif
    return true;
}

[[nodiscard]] v8::MaybeLocal<v8::String> js_string(v8::Isolate* isolate, std::string_view text)
{
    if (text.size() > maximum_text_size)
    {
        return {};
    }
    return v8::String::NewFromUtf8(isolate, text.data(), v8::NewStringType::kNormal,
                                   static_cast<int>(text.size()));
}

std::string value_text(v8::Isolate* isolate, v8::Local<v8::Value> value)
{
    const v8::String::Utf8Value text(isolate, value);
    return *text == nullptr ? std::string{}
                            : std::string(*text, static_cast<std::size_t>(text.length()));
}

struct V8Process
{
    std::unique_ptr<v8::Platform> platform;
    bool ready = false;
    bool attempted = false;

    ~V8Process()
    {
        if (ready)
        {
            (void)v8::V8::Dispose();
            v8::V8::DisposePlatform();
        }
    }

    [[nodiscard]] bool initialize()
    {
        if (ready || attempted)
        {
            return ready;
        }
        std::string icu;
        if (!prepare_icu_path(icu))
        {
            return false;
        }
        if (!v8::V8::InitializeICUDefaultLocation(nullptr, icu.c_str()))
        {
            return false;
        }
        attempted = true;
        v8::V8::SetFlagsFromString("--harmony-temporal");
        platform = v8::platform::NewDefaultPlatform();
        if (!platform)
        {
            return false;
        }
        v8::V8::InitializePlatform(platform.get());
        ready = v8::V8::Initialize();
        if (!ready)
        {
            v8::V8::DisposePlatform();
            platform.reset();
        }
        return ready;
    }
};

std::shared_ptr<V8Process> v8_process()
{
    // 静态引用保留平台到进程退出；isolate 的引用覆盖静态析构顺序，不能先释放平台。
    static const auto process = std::make_shared<V8Process>();
    return process;
}

void log_callback(const v8::FunctionCallbackInfo<v8::Value>& arguments)
{
    auto* isolate = arguments.GetIsolate();
    if (arguments.Length() != 1 || !arguments[0]->IsString())
    {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Debug requires one string message")));
        return;
    }
    const auto level = static_cast<LogLevel>(arguments.Data().As<v8::Int32>()->Value());
    LogSystem::write(level, value_text(isolate, arguments[0]));
}

struct Snapshot
{
    std::string revision = "builtin";
    std::string entry = "scripts/game.js";
    fs::path builtin_root;
    std::map<std::string, std::string> scripts;
    std::set<std::string> deleted;
};

} // namespace

struct ScriptSystem::State
{
    struct Run
    {
        State& owner;
        Snapshot snapshot;
        v8::Global<v8::Context> context;
        std::map<std::string, v8::Global<v8::Module>> modules;
        v8::Global<v8::Object> gameplay;
        bool initialization_entered = false;

        Run(State& state, Snapshot version) : owner(state), snapshot(std::move(version))
        {
        }

        [[nodiscard]] v8::MaybeLocal<v8::Module> load_module(const std::string& path)
        {
            auto* isolate = owner.isolate;
            const auto existing = modules.find(path);
            if (existing != modules.end())
            {
                return existing->second.Get(isolate);
            }
            if (modules.size() >= 256)
            {
                owner.module_error("Module graph exceeds 256 files");
                return {};
            }
            std::string source;
            if (path == "engine")
            {
                source = engine_module;
            }
            else
            {
                if (!script_path(path) || snapshot.deleted.contains(path))
                {
                    owner.module_error("Module is deleted or is not JavaScript: " + path);
                    return {};
                }
                const auto cached = snapshot.scripts.find(path);
                if (cached != snapshot.scripts.end())
                {
                    source = cached->second;
                }
                else
                {
                    fs::path file;
                    if (!builtin_file(snapshot.builtin_root, path, file) ||
                        !read_text(file, source))
                    {
                        owner.module_error("Cannot read contained module: " + path);
                        return {};
                    }
                }
            }
            v8::Local<v8::String> text;
            v8::Local<v8::String> name;
            if (!js_string(isolate, source).ToLocal(&text) ||
                !js_string(isolate, path).ToLocal(&name))
            {
                return {};
            }
            const v8::ScriptOrigin origin(name, 0, 0, false, -1, {}, false, false, true);
            v8::ScriptCompiler::Source input(text, origin);
            v8::Local<v8::Module> module;
            if (!v8::ScriptCompiler::CompileModule(isolate, &input).ToLocal(&module))
            {
                return {};
            }
            modules.emplace(path, v8::Global<v8::Module>(isolate, module));
            return module;
        }

        static v8::MaybeLocal<v8::Module> resolve(v8::Local<v8::Context> context,
                                                  v8::Local<v8::String> specifier,
                                                  v8::Local<v8::FixedArray> attributes,
                                                  v8::Local<v8::Module> referrer)
        {
            auto* run = static_cast<Run*>(
                context->GetAlignedPointerFromEmbedderData(0, v8::kEmbedderDataTypeTagDefault));
            auto* isolate = run->owner.isolate;
            if (attributes->Length() != 0)
            {
                run->owner.module_error("Import attributes are not supported");
                return {};
            }
            const std::string request = value_text(isolate, specifier);
            if (request == "engine")
            {
                return run->load_module(request);
            }
            if (!request.starts_with("./") && !request.starts_with("../"))
            {
                run->owner.module_error("Only relative imports and engine are supported: " +
                                        request);
                return {};
            }
            for (const auto& [name, module] : run->modules)
            {
                if (module.Get(isolate) == referrer)
                {
                    std::string path;
                    if (!relative_path(PathUtils::to_utf8(PathUtils::from_utf8(name).parent_path() /
                                                          PathUtils::from_utf8(request)),
                                       path))
                    {
                        run->owner.module_error("Import escapes the asset tree: " + request);
                        return {};
                    }
                    return run->load_module(path);
                }
            }
            run->owner.module_error("Unknown module referrer");
            return {};
        }
    };

    ScriptStatus& status;
    std::shared_ptr<V8Process> process;
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator;
    v8::Isolate* isolate = nullptr;
    std::unique_ptr<Run> active;
    std::unique_ptr<Snapshot> available;
    std::string builtin_entry = "scripts/game.js";
    std::set<std::string> builtin_deleted;
    std::string requested_entry;
    std::string last_manifest;
    std::string last_control;
    fs::file_time_type last_control_time{};
    bool control_seen = false;
    std::string last_attempt;
    std::string last_status;
    bool gameplay_requested = false;

    State(ScriptStatus& state, std::shared_ptr<V8Process> platform)
        : status(state), process(std::move(platform))
    {
    }

    ~State()
    {
        stop();
        if (isolate != nullptr)
        {
            isolate->Dispose();
        }
    }

    bool fail(std::string message)
    {
        if (status.last_error != message)
        {
            LogSystem::write(LogLevel::error, message);
            status.last_error = std::move(message);
        }
        return false;
    }

    void module_error(const std::string& message)
    {
        v8::Local<v8::String> text;
        if (js_string(isolate, message).ToLocal(&text))
        {
            isolate->ThrowException(v8::Exception::Error(text));
        }
    }

    bool exception(v8::TryCatch& caught, const std::string& operation)
    {
        std::string message = operation;
        if (caught.HasCaught())
        {
            message += ": " + value_text(isolate, caught.Exception());
            const auto location = caught.Message();
            if (!location.IsEmpty())
            {
                message += " (" + value_text(isolate, location->GetScriptResourceName()) + ":" +
                           std::to_string(
                               location->GetLineNumber(isolate->GetCurrentContext()).FromMaybe(0)) +
                           ")";
            }
        }
        return fail(std::move(message));
    }

    void publish()
    {
        if (!App::is_initialized())
        {
            return;
        }
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
        if (enabled && !status.hot_reload)
        {
            last_attempt.clear();
        }
        status.hot_reload = enabled;
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
        if (text == last_manifest && available)
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
        candidate->builtin_root = App::builtin_assets_path();
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
            if (script_path(path))
            {
                std::string source;
                if (!read_text(file, source) ||
                    !matches_bytes(source, item["size"].get<std::uint64_t>(),
                                   item["hash"].get<std::string>()))
                {
                    return fail("Version script failed size or hash check: " + path);
                }
                candidate->scripts.emplace(path, std::move(source));
            }
            else if (!matches_file(file, item["size"].get<std::uint64_t>(),
                                   item["hash"].get<std::string>()))
            {
                return fail("Version asset failed size or hash check: " + path);
            }
        }
        for (const auto& item : document["deleted"])
        {
            if (!item.is_string())
            {
                return fail("Invalid deleted asset path");
            }
            const auto path = item.get<std::string>();
            std::string normalized;
            if (!relative_path(path, normalized) || normalized != path || seen.contains(path) ||
                !candidate->deleted.insert(path).second)
            {
                return fail("Unsafe or conflicting deleted asset path: " + path);
            }
        }
        last_manifest = std::move(text);
        status.available_revision = candidate->revision;
        available = std::move(candidate);
        return true;
    }

    [[nodiscard]] bool bind(v8::Local<v8::Context> context)
    {
        auto native = v8::Object::New(isolate);
        constexpr auto attributes =
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete);
        for (const auto& [name, level] :
             std::array<std::pair<const char*, LogLevel>, 3>{{{"Log", LogLevel::info},
                                                              {"LogWarning", LogLevel::warning},
                                                              {"LogError", LogLevel::error}}})
        {
            v8::Local<v8::String> key;
            v8::Local<v8::Function> function;
            if (!js_string(isolate, name).ToLocal(&key) ||
                !v8::Function::New(context, log_callback,
                                   v8::Int32::New(isolate, static_cast<int>(level)))
                     .ToLocal(&function) ||
                !native->DefineOwnProperty(context, key, function, attributes).FromMaybe(false))
            {
                return false;
            }
        }
        for (const auto& [name, path] : std::array<std::pair<const char*, fs::path>, 3>{
                 {{"builtinAssetsPath", App::builtin_assets_path()},
                  {"persistentDataPath", App::persistent_data_path()},
                  {"cachePath", App::cache_path()}}})
        {
            v8::Local<v8::String> key;
            v8::Local<v8::String> value;
            if (!js_string(isolate, name).ToLocal(&key) ||
                !js_string(isolate, PathUtils::to_utf8(path)).ToLocal(&value) ||
                !native->DefineOwnProperty(context, key, value, attributes).FromMaybe(false))
            {
                return false;
            }
        }
        return context->Global()
            ->DefineOwnProperty(context, v8::String::NewFromUtf8Literal(isolate, "__engineNative"),
                                native, attributes)
            .FromMaybe(false);
    }

    [[nodiscard]] std::unique_ptr<Run> precheck()
    {
        Snapshot snapshot;
        if (available)
        {
            snapshot = *available;
        }
        else
        {
            snapshot.builtin_root = App::builtin_assets_path();
            snapshot.entry = builtin_entry;
            snapshot.deleted = builtin_deleted;
        }
        if (!requested_entry.empty())
        {
            snapshot.entry = requested_entry;
        }
        auto candidate = std::make_unique<Run>(*this, std::move(snapshot));
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handles(isolate);
        v8::TryCatch caught(isolate);
        const auto context = v8::Context::New(isolate);
        if (context.IsEmpty())
        {
            (void)fail("Cannot create gameplay context");
            return {};
        }
        v8::Context::Scope context_scope(context);
        context->SetAlignedPointerInEmbedderData(0, candidate.get(),
                                                 v8::kEmbedderDataTypeTagDefault);
        candidate->context.Reset(isolate, context);
        v8::Local<v8::Module> module;
        // 加载 engine 但不执行，后续校验入口实例确实继承该 Context 的 Gameplay。
        v8::Local<v8::Module> engine;
        if (!bind(context) || !candidate->load_module("engine").ToLocal(&engine) ||
            !candidate->load_module(candidate->snapshot.entry).ToLocal(&module) ||
            !module->InstantiateModule(context, Run::resolve).FromMaybe(false))
        {
            (void)exception(caught, "Gameplay module precheck failed");
            return {};
        }
        if (module->IsGraphAsync())
        {
            (void)fail("Top-level await is not supported by gameplay lifecycle");
            return {};
        }
        return candidate;
    }

    [[nodiscard]] bool call(Run& run, const char* name, bool initialize)
    {
        const auto context = run.context.Get(isolate);
        const auto object = run.gameplay.Get(isolate);
        v8::TryCatch caught(isolate);
        v8::Local<v8::String> key;
        v8::Local<v8::Value> member;
        v8::Local<v8::Value> result;
        if (!js_string(isolate, name).ToLocal(&key) ||
            !object->Get(context, key).ToLocal(&member) || !member->IsFunction() ||
            !member.As<v8::Function>()->Call(context, object, 0, nullptr).ToLocal(&result))
        {
            return exception(caught, std::string("Gameplay.") + name + " failed");
        }
        if (initialize && (!result->IsBoolean() || !result->IsTrue()))
        {
            return fail("Gameplay.OnInitialize must return true to start");
        }
        if (result->IsPromise())
        {
            return fail(std::string("Gameplay.") + name + " must be synchronous");
        }
        isolate->PerformMicrotaskCheckpoint();
        return true;
    }

    void stop()
    {
        if (active)
        {
            v8::Isolate::Scope isolate_scope(isolate);
            v8::HandleScope handles(isolate);
            v8::Context::Scope context_scope(active->context.Get(isolate));
            if (active->initialization_entered)
            {
                active->initialization_entered = false;
                (void)call(*active, "OnShutdown", false);
            }
            active.reset();
        }
        status.running = false;
        status.active_revision.clear();
    }

    [[nodiscard]] bool activate(std::unique_ptr<Run> candidate)
    {
        stop();
        status.last_error.clear();
        active = std::move(candidate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handles(isolate);
        v8::TryCatch caught(isolate);
        const auto context = active->context.Get(isolate);
        v8::Context::Scope context_scope(context);
        const auto module = active->modules.find(active->snapshot.entry)->second.Get(isolate);
        v8::Local<v8::Value> evaluation;
        bool passed = module->Evaluate(context).ToLocal(&evaluation);
        if (passed && evaluation->IsPromise())
        {
            const auto promise = evaluation.As<v8::Promise>();
            passed = promise->State() == v8::Promise::kFulfilled;
            if (!passed && promise->State() == v8::Promise::kRejected)
            {
                (void)fail("Gameplay module execution failed: " +
                           value_text(isolate, promise->Result()));
            }
        }
        if (!passed)
        {
            if (status.last_error.empty())
            {
                (void)exception(caught, "Gameplay module execution failed");
            }
            stop();
            return false;
        }
        const auto engine = active->modules.find("engine")->second.Get(isolate);
        v8::Local<v8::Value> constructor;
        v8::Local<v8::Value> base;
        v8::Local<v8::Object> object;
        if (engine->GetStatus() != v8::Module::kEvaluated ||
            !module->GetModuleNamespace()
                 .As<v8::Object>()
                 ->Get(context, v8::String::NewFromUtf8Literal(isolate, "default"))
                 .ToLocal(&constructor) ||
            !constructor->IsFunction() ||
            !constructor.As<v8::Function>()->NewInstance(context).ToLocal(&object) ||
            !engine->GetModuleNamespace()
                 .As<v8::Object>()
                 ->Get(context, v8::String::NewFromUtf8Literal(isolate, "Gameplay"))
                 .ToLocal(&base) ||
            !base->IsObject() ||
            !object->InstanceOf(context, base.As<v8::Object>()).FromMaybe(false))
        {
            (void)exception(caught, "Entry must default-export a class extending Gameplay");
            stop();
            return false;
        }
        active->gameplay.Reset(isolate, object);
        active->initialization_entered = true;
        if (!call(*active, "OnInitialize", true))
        {
            const auto failure = status.last_error;
            stop();
            status.last_error = failure;
            return false;
        }
        status.active_revision = active->snapshot.revision;
        status.running = true;
        return true;
    }

    static v8::MaybeLocal<v8::Promise> dynamic_import(v8::Local<v8::Context> context,
                                                      v8::Local<v8::Data>, v8::Local<v8::Value>,
                                                      v8::Local<v8::String>,
                                                      v8::Local<v8::FixedArray>)
    {
        auto* isolate = v8::Isolate::GetCurrent();
        auto* state = static_cast<State*>(isolate->GetData(0));
        (void)state->fail("Dynamic import is not supported");
        v8::Local<v8::Promise::Resolver> resolver;
        if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) ||
            !resolver
                 ->Reject(context, v8::Exception::Error(v8::String::NewFromUtf8Literal(
                                       isolate, "Dynamic import is not supported")))
                 .FromMaybe(false))
        {
            return {};
        }
        return resolver->GetPromise();
    }

    static void promise_rejection(v8::PromiseRejectMessage message)
    {
        if (message.GetEvent() == v8::kPromiseRejectWithNoHandler)
        {
            auto* isolate = v8::Isolate::GetCurrent();
            auto* state = static_cast<State*>(isolate->GetData(0));
            (void)state->fail("Unhandled script promise: " +
                              value_text(isolate, message.GetValue()));
        }
    }
};

ScriptSystem::ScriptSystem() noexcept = default;

ScriptSystem::~ScriptSystem() noexcept
{
    shutdown();
}

bool ScriptSystem::on_initialize(const EngineConfig&)
{
    std::string builtin_entry = "scripts/game.js";
    std::set<std::string> builtin_deleted;
#if defined(SDL_PLATFORM_ANDROID)
    if (!prepare_builtin_manifest(builtin_entry, builtin_deleted, status_.last_error))
    {
        LogSystem::write(LogLevel::error, status_.last_error);
        return false;
    }
#endif
    auto process = v8_process();
    if (!process->initialize())
    {
        status_.last_error = "Cannot prepare ICU data or initialize the V8 platform";
        LogSystem::write(LogLevel::error, status_.last_error);
        return false;
    }
    status_.last_error.clear();
    status_.available_revision.clear();
    state_ = std::make_unique<State>(status_, std::move(process));
    state_->builtin_entry = std::move(builtin_entry);
    state_->builtin_deleted = std::move(builtin_deleted);
    state_->allocator.reset(v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    if (!state_->allocator)
    {
        return state_->fail("Cannot create V8 allocator");
    }
    v8::Isolate::CreateParams parameters;
    parameters.array_buffer_allocator = state_->allocator.get();
    state_->isolate = v8::Isolate::New(parameters);
    if (state_->isolate == nullptr)
    {
        return state_->fail("Cannot create V8 isolate");
    }
    state_->isolate->SetData(0, state_.get());
    state_->isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
    state_->isolate->SetHostImportModuleDynamicallyCallback(State::dynamic_import);
    state_->isolate->SetPromiseRejectCallback(State::promise_rejection);
    return true;
}

bool ScriptSystem::start_gameplay(std::string_view entry)
{
    if (!is_initialized() || !state_ || !App::is_initialized())
    {
        status_.last_error = "Gameplay requires initialized ScriptSystem and App";
        LogSystem::write(LogLevel::error, status_.last_error);
        return false;
    }
    std::string path;
    if (!entry.empty() && (!relative_path(entry, path) || !script_path(path)))
    {
        (void)state_->fail("Unsafe gameplay entry path");
        state_->publish();
        return false;
    }
    const auto previous_entry = state_->requested_entry;
    state_->requested_entry = std::move(path);
    state_->poll_control();
    if (!state_->poll_available())
    {
        state_->requested_entry = previous_entry;
        state_->publish();
        return false;
    }
    auto candidate = state_->precheck();
    if (!candidate)
    {
        state_->requested_entry = previous_entry;
        state_->publish();
        return false;
    }
    state_->gameplay_requested = true;
    state_->last_attempt = candidate->snapshot.revision;
    const bool passed = state_->activate(std::move(candidate));
    state_->publish();
    return passed;
}

void ScriptSystem::stop_gameplay() noexcept
{
    if (state_)
    {
        state_->gameplay_requested = false;
        state_->stop();
        state_->publish();
    }
}

void ScriptSystem::set_hot_reload(bool enabled)
{
    if (enabled && !status_.hot_reload && state_)
    {
        state_->last_attempt.clear();
    }
    status_.hot_reload = enabled;
    if (state_)
    {
        state_->publish();
    }
}

const ScriptStatus& ScriptSystem::status() const noexcept
{
    return status_;
}

bool ScriptSystem::on_tick(double)
{
    if (!state_ || !App::is_initialized())
    {
        return true;
    }
    state_->poll_control();
    const bool complete = state_->poll_available();
    if (complete && status_.hot_reload && state_->gameplay_requested && state_->available &&
        state_->available->revision != status_.active_revision &&
        state_->available->revision != state_->last_attempt)
    {
        state_->last_attempt = state_->available->revision;
        auto candidate = state_->precheck();
        if (candidate)
        {
            (void)state_->activate(std::move(candidate));
        }
    }
    if (status_.running && state_->active)
    {
        v8::Isolate::Scope isolate_scope(state_->isolate);
        v8::HandleScope handles(state_->isolate);
        v8::Context::Scope context_scope(state_->active->context.Get(state_->isolate));
        (void)state_->call(*state_->active, "Update", false);
    }
    state_->publish();
    return true;
}

void ScriptSystem::on_shutdown() noexcept
{
    stop_gameplay();
    state_.reset();
}

} // namespace engine
