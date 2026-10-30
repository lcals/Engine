#include "plugins/runtime/script/script_system.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "plugins/runtime/script/bindings/script_binding_registry.hpp"

#include "runtime/app/app.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL.h>
#include <libplatform/libplatform.h>
#include <v8.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

namespace engine::plugins::script
{
namespace
{

namespace fs = std::filesystem;
constexpr std::size_t maximum_text_size = 4 * 1024 * 1024;

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

#if defined(SDL_PLATFORM_ANDROID) && ENGINE_V8_HAS_I18N
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

#endif

[[nodiscard]] bool script_path(std::string_view path)
{
    return path.ends_with(".js") || path.ends_with(".mjs");
}

#if defined(SDL_PLATFORM_ANDROID) && ENGINE_V8_HAS_I18N
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

#endif

#if ENGINE_V8_HAS_I18N
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
#endif

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
#if ENGINE_V8_HAS_I18N
        std::string icu;
        if (!prepare_icu_path(icu))
        {
            return false;
        }
        if (!v8::V8::InitializeICUDefaultLocation(nullptr, icu.c_str()))
        {
            return false;
        }
#endif
        attempted = true;
#if ENGINE_V8_HAS_TEMPORAL
        v8::V8::SetFlagsFromString("--harmony-temporal");
#endif
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

struct Snapshot
{
    std::string revision = "builtin";
    std::string entry = "scripts/game.js";
    std::shared_ptr<const ResourceCatalog> catalog;
    MountList mounts;
    std::map<std::string, Guid, std::less<>> module_guids;
    bool active_catalog = false;
};

[[nodiscard]] bool catalog_snapshot(const ResourceCatalog& catalog, const MountList& mounts,
                                    Snapshot& snapshot)
{
    snapshot.catalog = std::make_shared<ResourceCatalog>(catalog);
    snapshot.mounts = mounts;
    for (const auto& asset : catalog.data().entries)
    {
        if (asset->kind == schema::AssetKind::script &&
            (asset->module_path.empty() ||
             !snapshot.module_guids.emplace(asset->module_path, asset->guid).second))
        {
            return false;
        }
    }
    return true;
}

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
        std::vector<ResourcePtr> resources;
        bool initialization_entered = false;
        bindings::BindingContext bindings;

        Run(State& state, Snapshot version)
            : owner(state), snapshot(std::move(version)),
              bindings(state.world, state.renderer, state.ui)
        {
        }

        ~Run()
        {
            v8::Isolate::Scope isolate_scope(owner.isolate);
            v8::HandleScope handles(owner.isolate);
            if (!context.IsEmpty())
            {
                context.Get(owner.isolate)
                    ->SetAlignedPointerInEmbedderData(0, nullptr, v8::kEmbedderDataTypeTagDefault);
            }
            // 清空原生入口，再释放本次Run的场景；旧Context不能保留已销毁Run地址。
            bindings.shutdown();
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
                source = owner.binding_registry.source();
            }
            else
            {
                if (!script_path(path))
                {
                    owner.module_error("Module is deleted or is not JavaScript: " + path);
                    return {};
                }
                if (snapshot.catalog != nullptr)
                {
                    const auto mapped = snapshot.module_guids.find(path);
                    ByteBuffer bytes;
                    bool success = mapped != snapshot.module_guids.end();
                    if (success && snapshot.active_catalog)
                    {
                        ResourcePtr resource;
                        auto* system = ResourceSystem::instance();
                        success =
                            system != nullptr && system->loader().load(mapped->second, resource);
                        if (success)
                        {
                            const auto content = resource->bytes();
                            bytes.assign(content.begin(), content.end());
                            resources.push_back(std::move(resource));
                        }
                    }
                    else if (success)
                    {
                        success = ResourceSystem::read_resource(*snapshot.catalog, snapshot.mounts,
                                                                mapped->second, bytes);
                    }
                    if (!success || bytes.size() > maximum_text_size)
                    {
                        owner.module_error("Cannot read GUID module from supplied catalog: " +
                                           path);
                        return {};
                    }
                    if (!bytes.empty())
                    {
                        source.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                    }
                }
                else
                {
                    owner.module_error("Gameplay requires a supplied resource catalog");
                    return {};
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
            if (run == nullptr)
            {
                return {};
            }
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
    bindings::ScriptBindingRegistry binding_registry;
    WorldSystem* world = nullptr;
    RenderSystem* renderer = nullptr;
    UiSystem* ui = nullptr;
    double delta_time = 0.0;
    std::shared_ptr<V8Process> process;
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator;
    v8::Isolate* isolate = nullptr;
    std::unique_ptr<Run> active;
    std::unique_ptr<Run> prepared;

    State(ScriptStatus& state, std::shared_ptr<V8Process> platform, WorldSystem* scene_world,
          RenderSystem* scene_renderer, UiSystem* scene_ui)
        : status(state), world(scene_world), renderer(scene_renderer), ui(scene_ui),
          process(std::move(platform))
    {
    }

    ~State()
    {
        prepared.reset();
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

    [[nodiscard]] std::unique_ptr<Run> precheck(Snapshot snapshot)
    {
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
        if (!candidate->bindings.initialize(context) ||
            !binding_registry.register_functions(context) ||
            !candidate->load_module("engine").ToLocal(&engine) ||
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
        run.bindings.begin_call(delta_time);
        const bool called =
            js_string(isolate, name).ToLocal(&key) && object->Get(context, key).ToLocal(&member) &&
            member->IsFunction() &&
            member.As<v8::Function>()->Call(context, object, 0, nullptr).ToLocal(&result);
        run.bindings.end_call();
        if (!called)
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

    [[nodiscard]] bool call_binding(Run& run, const char* name)
    {
        const auto module = run.modules.find("engine");
        if (module == run.modules.end() ||
            module->second.Get(isolate)->GetStatus() != v8::Module::kEvaluated)
        {
            return true;
        }
        const auto context = run.context.Get(isolate);
        const auto object = module->second.Get(isolate)->GetModuleNamespace().As<v8::Object>();
        v8::TryCatch caught(isolate);
        v8::Local<v8::String> key;
        v8::Local<v8::Value> member;
        v8::Local<v8::Value> result;
        run.bindings.begin_call(delta_time);
        const bool called =
            js_string(isolate, name).ToLocal(&key) && object->Get(context, key).ToLocal(&member) &&
            member->IsFunction() &&
            member.As<v8::Function>()->Call(context, object, 0, nullptr).ToLocal(&result);
        run.bindings.end_call();
        if (!called)
        {
            return exception(caught, std::string("Script binding callback ") + name + " failed");
        }
        if (result->IsPromise())
        {
            return fail("Script binding callbacks must be synchronous");
        }
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
                delta_time = 0.0;
                (void)call(*active, "OnShutdown", false);
            }
            // Run 内部订阅释放后再销毁场景和 V8 Context。
            (void)call_binding(*active, "__clearBindings");
            active.reset();
        }
        delta_time = 0.0;
        status.running = false;
        status.active_revision.clear();
    }

    [[nodiscard]] bool activate(std::unique_ptr<Run> candidate)
    {
        stop();
        status.last_error.clear();
        if (candidate->snapshot.catalog != nullptr && !candidate->snapshot.active_catalog)
        {
            auto* system = ResourceSystem::instance();
            if (system == nullptr)
            {
                return fail("Gameplay requires an active resource system");
            }
            for (const auto& [name, module] : candidate->modules)
            {
                static_cast<void>(module);
                if (name == "engine")
                {
                    continue;
                }
                const auto mapped = candidate->snapshot.module_guids.find(name);
                const auto* expected = mapped == candidate->snapshot.module_guids.end()
                                           ? nullptr
                                           : candidate->snapshot.catalog->find(mapped->second);
                ResourcePtr resource;
                if (expected == nullptr || !system->loader().load(expected->guid, resource) ||
                    resource->info().content_hash != expected->content_hash ||
                    resource->info().module_path != expected->module_path)
                {
                    return fail("Active script resources differ from the prepared catalog");
                }
                candidate->resources.push_back(std::move(resource));
            }
        }
        active = std::move(candidate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handles(isolate);
        v8::TryCatch caught(isolate);
        const auto context = active->context.Get(isolate);
        v8::Context::Scope context_scope(context);
        active->bindings.activate();
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

ScriptSystem::ScriptSystem(WorldSystem* world, RenderSystem* renderer, UiSystem* ui) noexcept
    : world_(world), renderer_(renderer), ui_(ui)
{
}

ScriptSystem::~ScriptSystem() noexcept
{
    shutdown();
}

bool ScriptSystem::on_initialize(const EngineConfig&)
{
    auto process = v8_process();
    if (!process->initialize())
    {
        status_.last_error = "Cannot initialize the V8 platform";
        LogSystem::write(LogLevel::error, status_.last_error);
        return false;
    }
    status_.last_error.clear();
    status_.available_revision.clear();
    state_ = std::make_unique<State>(status_, std::move(process), world_, renderer_, ui_);
    if (!state_->binding_registry.initialize())
    {
        return state_->fail("Cannot register script binding modules");
    }
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

bool ScriptSystem::prepare_gameplay(const ResourceCatalog& catalog, const MountList& mounts,
                                    std::string_view entry, std::string_view revision)
{
    if (!is_initialized() || !state_)
    {
        status_.last_error = "Gameplay requires an initialized script system";
        return false;
    }
    state_->prepared.reset();
    Snapshot snapshot;
    const std::string_view selected = entry.empty() ? std::string_view{"scripts/game.js"} : entry;
    if (!relative_path(selected, snapshot.entry) || snapshot.entry != selected ||
        !script_path(snapshot.entry) || !catalog_snapshot(catalog, mounts, snapshot))
    {
        return state_->fail("Invalid gameplay entry or resource catalog");
    }
    snapshot.revision = revision.empty() ? std::string(catalog.revision()) : std::string(revision);
    status_.available_revision = snapshot.revision;
    state_->prepared = state_->precheck(std::move(snapshot));
    return state_->prepared != nullptr;
}

bool ScriptSystem::start_prepared_gameplay()
{
    if (!is_initialized() || !state_ || !state_->prepared)
    {
        status_.last_error = "No prepared gameplay is available";
        return false;
    }
    return state_->activate(std::move(state_->prepared));
}

bool ScriptSystem::start_gameplay(std::string_view entry)
{
    if (!is_initialized() || !state_)
    {
        status_.last_error = "Gameplay requires an initialized script system";
        return false;
    }
    Snapshot snapshot;
    const auto selected = entry.empty() ? std::string_view{"scripts/game.js"} : entry;
    if (!relative_path(selected, snapshot.entry) || snapshot.entry != selected ||
        !script_path(snapshot.entry))
    {
        return state_->fail("Unsafe gameplay entry path");
    }
    auto* resources = ResourceSystem::instance();
    if (resources == nullptr || resources->catalog().data().entries.empty() ||
        !catalog_snapshot(resources->catalog(), {}, snapshot))
    {
        return state_->fail("Gameplay requires an active runtime resource catalog");
    }
    snapshot.active_catalog = true;
    snapshot.revision = std::string(resources->catalog().revision());
    auto candidate = state_->precheck(std::move(snapshot));
    return candidate != nullptr && state_->activate(std::move(candidate));
}

void ScriptSystem::stop_gameplay() noexcept
{
    if (state_)
    {
        state_->stop();
    }
}

void ScriptSystem::set_hot_reload(bool enabled)
{
    status_.hot_reload = enabled;
}

const ScriptStatus& ScriptSystem::status() const noexcept
{
    return status_;
}

bool ScriptSystem::on_tick(double delta_time)
{
    if (!state_ || !App::is_initialized())
    {
        return true;
    }
    if (status_.running && state_->active)
    {
        v8::Isolate::Scope isolate_scope(state_->isolate);
        v8::HandleScope handles(state_->isolate);
        v8::Context::Scope context_scope(state_->active->context.Get(state_->isolate));
        state_->delta_time = delta_time;
        if (!state_->call_binding(*state_->active, "__dispatchBindingEvents") ||
            !state_->call(*state_->active, "Update", false))
        {
            const auto failure = status_.last_error;
            state_->stop();
            status_.last_error = failure;
        }
        state_->delta_time = 0.0;
    }
    return true;
}

void ScriptSystem::on_shutdown() noexcept
{
    stop_gameplay();
    state_.reset();
}

} // namespace engine::plugins::script
