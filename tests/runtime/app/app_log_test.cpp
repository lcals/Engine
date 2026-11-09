#include "runtime/app/app.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/utils/path_utils.hpp"
#include "utf8_fixture.hpp"

#include <SDL3/SDL_log.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace
{

bool expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::fprintf(stderr, "App/log test: %.*s\n", static_cast<int>(message.size()),
                     message.data());
    }
    return condition;
}

struct TestDirectory
{
    std::filesystem::path path;

    ~TestDirectory()
    {
        engine::LogSystem::instance().shutdown();
        engine::App::shutdown();
        std::error_code error;
        if (!path.empty())
        {
            std::filesystem::remove_all(path, error);
        }
    }
};

bool create_test_directory(TestDirectory& directory)
{
    std::error_code error;
    const auto temporary = std::filesystem::temp_directory_path(error);
    if (error)
    {
        return false;
    }
    const auto identity = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned int index = 0; index < 100; ++index)
    {
        const auto path = temporary / ("engine-app-log-test-" + std::to_string(identity) + "-" +
                                       std::to_string(index));
        if (std::filesystem::create_directory(path, error))
        {
            directory.path = path;
            return true;
        }
        if (error)
        {
            return false;
        }
    }
    return false;
}

bool write_text(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    return static_cast<bool>(file);
}

std::string read_text(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

engine::AppConfig app_config(const std::filesystem::path& root)
{
    engine::AppConfig config;
    config.assets_path = root / "missing-package-assets";
    config.persistent_data_path =
        root / engine::PathUtils::from_utf8(test_support::utf8_fixture("data_directory"));
    config.cache_path = root / "cache";
    return config;
}

bool app_paths_and_failures(const std::filesystem::path& root)
{
    auto config = app_config(root);
    bool success = expect(!engine::App::is_initialized(), "App starts stopped");
    success &=
        expect(engine::App::cache_path().empty() && engine::App::persistent_data_path().empty() &&
                   engine::App::assets_path().empty(),
               "uninitialized paths are empty");
    auto invalid = config;
    invalid.application_name = "../bad";
    success &= expect(!engine::App::initialize(invalid), "reject a path as application name");
    invalid = config;
    invalid.cache_path = invalid.persistent_data_path;
    success &= expect(!engine::App::initialize(invalid), "cache and persistent roots differ");
    invalid = config;
    invalid.cache_path = root / "blocker" / "cache";
    if (!write_text(root / "blocker", "not a directory"))
    {
        return expect(false, "prepare invalid directory fixture");
    }
    success &= expect(!engine::App::initialize(invalid), "directory creation failure propagates");
    success &= expect(!engine::App::is_initialized() && engine::App::cache_path().empty() &&
                          engine::App::persistent_data_path().empty() &&
                          engine::App::assets_path().empty(),
                      "failed initialization does not publish partial paths");

    if (!expect(engine::App::initialize(config), "initialize explicit platform paths"))
    {
        return false;
    }
    std::error_code error;
    success &=
        expect(std::filesystem::is_directory(engine::App::persistent_data_path(), error) && !error,
               "persistent directory exists");
    success &= expect(std::filesystem::is_directory(engine::App::cache_path(), error) && !error,
                      "cache directory exists");
    success &= expect(!std::filesystem::exists(engine::App::assets_path(), error) && !error,
                      "App does not create an absent package resource root");
    const auto original_path = engine::App::persistent_data_path();
    success &= expect(!engine::App::initialize(invalid) &&
                          engine::App::persistent_data_path() == original_path,
                      "duplicate initialization preserves the published paths");
    engine::App::shutdown();
    engine::App::shutdown();
    success &= expect(!engine::App::is_initialized() && engine::App::cache_path().empty() &&
                          engine::App::persistent_data_path().empty() &&
                          engine::App::assets_path().empty(),
                      "repeated shutdown clears all borrowed path values");
    success &= expect(std::filesystem::is_directory(original_path, error) && !error,
                      "App shutdown preserves persistent data");

    config.assets_path.clear();
    success &= expect(engine::App::initialize(config), "resolve the default SDL application root");
    success &=
        expect(engine::App::assets_path().is_absolute(), "default application root is absolute");
    engine::App::shutdown();
    return success;
}

bool log_initialization_failures(const std::filesystem::path& root)
{
    auto& log = engine::LogSystem::instance();
    const engine::EngineConfig config;
    bool success = expect(!log.initialize(config), "logging requires App initialization");
    success &= expect(log.file_path().empty() && !log.flush(), "failed logger has no open file");
    if (!engine::App::initialize(app_config(root)))
    {
        return expect(false, "App initialization for logger failures");
    }
    const auto log_directory = engine::App::persistent_data_path() / "logs";
    if (!write_text(log_directory, "not a directory"))
    {
        return expect(false, "prepare invalid log directory");
    }
    success &= expect(!log.initialize(config) && !log.is_initialized() && log.file_path().empty(),
                      "invalid log directory rejects initialization without partial state");
    std::error_code error;
    success &= expect(std::filesystem::remove(log_directory, error) && !error,
                      "remove the invalid fixture");
    log.shutdown();
    engine::App::shutdown();
    return success;
}

bool concurrent_logging_and_reopen(const std::filesystem::path& root)
{
    auto& log = engine::LogSystem::instance();
    const engine::EngineConfig config;
    if (!engine::App::initialize(app_config(root)) || !log.initialize(config))
    {
        return expect(false, "initialize concurrent logger");
    }
    const auto log_path = log.file_path();
    engine::LogSystem::write(engine::LogLevel::info, "initial-info");
    engine::LogSystem::write(engine::LogLevel::warning, "initial-warning");
    engine::LogSystem::write(engine::LogLevel::error, "initial-error");

    const bool error_visible =
        expect(read_text(log_path).find("[ERROR] initial-error\n") != std::string::npos,
               "error logging is visible before write returns");

    constexpr unsigned int worker_count = 4;
    constexpr unsigned int records_per_worker = 32;
    std::vector<std::thread> workers;
    for (unsigned int worker = 0; worker < worker_count; ++worker)
    {
        workers.emplace_back(
            [worker]()
            {
                for (unsigned int index = 0; index < records_per_worker; ++index)
                {
                    engine::LogSystem::write(engine::LogLevel::info,
                                             "worker-" + std::to_string(worker) + "-record-" +
                                                 std::to_string(index));
                }
            });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
    bool success = expect(log.flush(), "concurrent records flush successfully");
    success &= error_visible;
    const auto records = read_text(log_path);
    success &= expect(records.find("Z [INFO] initial-info\n") != std::string::npos &&
                          records.find("Z [WARNING] initial-warning\n") != std::string::npos &&
                          records.find("Z [ERROR] initial-error\n") != std::string::npos,
                      "all levels include a UTC timestamp and complete message");
    for (unsigned int worker = 0; worker < worker_count; ++worker)
    {
        for (unsigned int index = 0; index < records_per_worker; ++index)
        {
            const auto needle = "[INFO] worker-" + std::to_string(worker) + "-record-" +
                                std::to_string(index) + "\n";
            const auto first = records.find(needle);
            success &= expect(first != std::string::npos &&
                                  records.find(needle, first + needle.size()) == std::string::npos,
                              "each concurrent record appears once without interleaving");
        }
    }
    log.shutdown();
    log.shutdown();
    success &= expect(log.file_path().empty() && !log.flush(), "logger closes idempotently");
    engine::LogSystem::write(engine::LogLevel::info, "stderr-only-after-shutdown");
    success &= expect(read_text(log_path) == records, "shutdown writes do not reopen the log");
    success &= expect(log.initialize(config), "logger reinitializes after shutdown");
    engine::LogSystem::write(engine::LogLevel::info, "appended-after-reopen");
    success &= expect(log.flush(), "reopened file flushes");
    const auto appended = read_text(log_path);
    success &= expect(appended.starts_with(records) &&
                          appended.find("appended-after-reopen\n") != std::string::npos,
                      "reinitialization appends without discarding previous records");
    log.shutdown();
    engine::App::shutdown();
    return success;
}

bool log_rotation(const std::filesystem::path& root)
{
    auto& log = engine::LogSystem::instance();
    const engine::EngineConfig config;
    if (!engine::App::initialize(app_config(root)))
    {
        return expect(false, "initialize rotation App");
    }
    const auto path = engine::App::persistent_data_path() / "logs" / "engine.log";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.seekp(4 * 1024 * 1024 - 1);
        file.put('\n');
        file.close();
        if (!file)
        {
            return expect(false, "prepare a full log file");
        }
    }
    auto first = path;
    auto second = path;
    auto third = path;
    first += ".1";
    second += ".2";
    third += ".3";
    if (!write_text(first, "previous-one") || !write_text(second, "previous-two") ||
        !write_text(third, "discard-oldest") || !log.initialize(config))
    {
        return expect(false, "prepare rotation archives");
    }
    engine::LogSystem::write(engine::LogLevel::info, "first-record-after-rotation");
    bool success = expect(log.flush(), "rotation flushes successfully");
    std::error_code error;
    success &= expect(std::filesystem::file_size(first, error) == 4 * 1024 * 1024 && !error,
                      "full log becomes the first archive");
    success &= expect(read_text(second) == "previous-one" && read_text(third) == "previous-two",
                      "rotation retains three generations and removes the oldest");
    success &= expect(read_text(path).find("first-record-after-rotation\n") != std::string::npos,
                      "current record reaches the new active file");
    log.shutdown();
    engine::App::shutdown();
    return success;
}

bool log_io_failures(const std::filesystem::path& root)
{
    auto& log = engine::LogSystem::instance();
    const engine::EngineConfig config;
    if (!engine::App::initialize(app_config(root / "io-failure")))
    {
        return expect(false, "initialize log I/O failure App");
    }
    const auto directory = engine::App::persistent_data_path() / "logs";
    const auto path = directory / "engine.log";
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error)
    {
        return expect(false, "prepare directory at log file path");
    }
    bool success = expect(!log.initialize(config) && log.file_path().empty() && !log.flush(),
                          "file open failure returns false without aborting");
    std::filesystem::remove(path, error);
    if (error)
    {
        return expect(false, "remove the invalid log file path");
    }
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.seekp(4 * 1024 * 1024 - 1);
        file.put('\n');
        file.close();
        if (!file)
        {
            return expect(false, "prepare full file for rotation failure");
        }
    }
    auto blocked_archive = path;
    blocked_archive += ".3";
    std::filesystem::create_directory(blocked_archive, error);
    if (error || !write_text(blocked_archive / "keep", "cannot remove nonempty directory") ||
        !log.initialize(config))
    {
        return expect(false, "prepare obstructed rotation");
    }
    engine::LogSystem::write(engine::LogLevel::info, "platform-only-after-file-failure");
    success &= expect(!log.flush(), "rotation I/O error reaches the synchronous status");
    engine::LogSystem::write(engine::LogLevel::error, "error-after-file-failure");
    success &= expect(!log.flush(), "file failure remains sticky without aborting error output");
    success &= expect(std::filesystem::file_size(path, error) == 4 * 1024 * 1024 && !error,
                      "failed rotation preserves the original log");
    log.shutdown();
    std::filesystem::remove_all(blocked_archive, error);
    if (error || !log.initialize(config))
    {
        return expect(false, "reopen after removing the rotation obstruction");
    }
    engine::LogSystem::write(engine::LogLevel::error, "recovered-after-file-failure");
    success &= expect(log.flush() && read_text(path).find("recovered-after-file-failure\n") !=
                                         std::string::npos,
                      "reinitialization clears failure and restores queued output");
    log.shutdown();
    engine::App::shutdown();
    return success;
}

bool sdl_logging_bridge(const std::filesystem::path& root)
{
    struct PlatformCapture
    {
        PlatformCapture()
        {
            SDL_GetLogOutputFunction(&previous, &previous_userdata);
            SDL_SetLogOutputFunction(output, this);
        }
        ~PlatformCapture()
        {
            engine::LogSystem::instance().shutdown();
            SDL_SetLogOutputFunction(previous, previous_userdata);
        }
        static void SDLCALL output(void* userdata, int category, SDL_LogPriority priority,
                                   const char* message)
        {
            auto& capture = *static_cast<PlatformCapture*>(userdata);
            const std::lock_guard lock(capture.mutex);
            capture.messages.emplace_back(message);
            if (std::string_view(message) == "bridge-error")
            {
                capture.error_identity =
                    category == SDL_LOG_CATEGORY_RENDER && priority == SDL_LOG_PRIORITY_ERROR;
            }
        }
        std::size_t occurrences(std::string_view message)
        {
            const std::lock_guard lock(mutex);
            std::size_t count = 0;
            for (const auto& record : messages)
            {
                count += record.find(message) != std::string::npos ? 1 : 0;
            }
            return count;
        }
        SDL_LogOutputFunction previous = nullptr;
        void* previous_userdata = nullptr;
        std::mutex mutex;
        std::vector<std::string> messages;
        bool error_identity = false;
    } capture;

    auto& log = engine::LogSystem::instance();
    const engine::EngineConfig config;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "bridge-before-initialize");
    if (!engine::App::initialize(app_config(root / "bridge")) || !log.initialize(config))
    {
        return expect(false, "initialize SDL logging bridge");
    }
    const auto path = log.file_path();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s",
                test_support::utf8_fixture("log_message").c_str());
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "bridge-error");
    bool success = expect(read_text(path).find("[ERROR] bridge-error\n") != std::string::npos,
                          "SDL errors enter the file before the SDL call returns");
    engine::LogSystem::write(engine::LogLevel::info, "engine-direct");
    std::vector<std::thread> workers;
    for (unsigned int index = 0; index < 4; ++index)
    {
        workers.emplace_back(
            [index]()
            {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "bridge-worker-%u", index);
                engine::LogSystem::write(engine::LogLevel::info,
                                         "engine-worker-" + std::to_string(index));
            });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
    success &= expect(log.flush(), "SDL and direct records share the synchronous flush");
    const auto records = read_text(path);
    success &= expect(records.find("[INFO] " + test_support::utf8_fixture("log_message") + "\n") !=
                              std::string::npos &&
                          records.find("[INFO] engine-direct\n") != std::string::npos &&
                          records.find("bridge-before-initialize") == std::string::npos,
                      "the bridge records both APIs only while initialized");
    success &= expect(capture.error_identity &&
                          capture.occurrences(test_support::utf8_fixture("log_message")) == 1 &&
                          capture.occurrences("bridge-error") == 1 &&
                          capture.occurrences("engine-direct") == 1,
                      "original SDL output retains identity and receives each record once");
    for (unsigned int index = 0; index < 4; ++index)
    {
        const auto sdl_message = "bridge-worker-" + std::to_string(index);
        const auto engine_message = "engine-worker-" + std::to_string(index);
        success &= expect(records.find(sdl_message) != std::string::npos &&
                              records.find(engine_message) != std::string::npos &&
                              capture.occurrences(sdl_message) == 1 &&
                              capture.occurrences(engine_message) == 1,
                          "concurrent SDL and direct records do not recurse or duplicate");
    }
    log.shutdown();
    SDL_LogOutputFunction restored = nullptr;
    void* restored_userdata = nullptr;
    SDL_GetLogOutputFunction(&restored, &restored_userdata);
    success &= expect(restored == PlatformCapture::output && restored_userdata == &capture,
                      "shutdown restores the original SDL callback and userdata");
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "bridge-after-shutdown");
    engine::LogSystem::write(engine::LogLevel::info, "engine-after-shutdown");
    success &=
        expect(capture.occurrences("bridge-after-shutdown") == 1 &&
                   capture.occurrences("engine-after-shutdown") == 1 && read_text(path) == records,
               "closed logging keeps platform output without reopening the file");
    success &= expect(log.initialize(config), "reinitialize the SDL logging bridge");
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "bridge-after-reopen");
    success &=
        expect(log.flush() && read_text(path).find("bridge-after-reopen\n") != std::string::npos,
               "reopened bridge writes to the original file");
    log.shutdown();
    engine::App::shutdown();
    return success;
}

} // namespace

int main()
{
    TestDirectory directory;
    if (!create_test_directory(directory))
    {
        return 1;
    }
    int failures = 0;
    failures += !app_paths_and_failures(directory.path);
    failures += !log_initialization_failures(directory.path);
    failures += !concurrent_logging_and_reopen(directory.path);
    failures += !log_rotation(directory.path);
    failures += !log_io_failures(directory.path);
    failures += !sdl_logging_bridge(directory.path);
    return failures == 0 ? 0 : 1;
}
