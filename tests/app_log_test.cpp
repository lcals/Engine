#include "runtime/app/app.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/log/log_system.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    config.builtin_assets_path = root / "missing-package-assets";
    config.persistent_data_path = root / std::filesystem::path(u8"data-中文");
    config.cache_path = root / "cache";
    return config;
}

bool app_paths_and_failures(const std::filesystem::path& root)
{
    auto config = app_config(root);
    bool success = expect(!engine::App::is_initialized(), "App starts stopped");
    success &=
        expect(engine::App::cache_path().empty() && engine::App::persistent_data_path().empty() &&
                   engine::App::builtin_assets_path().empty(),
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
                          engine::App::builtin_assets_path().empty(),
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
    success &= expect(!std::filesystem::exists(engine::App::builtin_assets_path(), error) && !error,
                      "App does not create an absent package resource root");
    const auto original_path = engine::App::persistent_data_path();
    success &= expect(!engine::App::initialize(invalid) &&
                          engine::App::persistent_data_path() == original_path,
                      "duplicate initialization preserves the published paths");
    engine::App::shutdown();
    engine::App::shutdown();
    success &= expect(!engine::App::is_initialized() && engine::App::cache_path().empty() &&
                          engine::App::persistent_data_path().empty() &&
                          engine::App::builtin_assets_path().empty(),
                      "repeated shutdown clears all borrowed path values");
    success &= expect(std::filesystem::is_directory(original_path, error) && !error,
                      "App shutdown preserves persistent data");

    config.builtin_assets_path.clear();
    success &= expect(engine::App::initialize(config), "resolve the default SDL application root");
    success &= expect(engine::App::builtin_assets_path().is_absolute(),
                      "default application root is absolute");
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
    return failures == 0 ? 0 : 1;
}
