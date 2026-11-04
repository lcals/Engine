#include "runtime/log/log_system.hpp"

#include "runtime/app/app.hpp"

#include <SDL3/SDL_log.h>

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/sinks/Sink.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace engine
{
namespace
{

constexpr std::uintmax_t maximum_file_size = 4 * 1024 * 1024;
constexpr std::size_t maximum_message_size = 64 * 1024;
constexpr unsigned int retained_files = 3;

const char* level_name(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::info:
        return "INFO";
    case LogLevel::warning:
        return "WARNING";
    case LogLevel::error:
        return "ERROR";
    }
    return "UNKNOWN";
}

std::string format_record(LogLevel level, std::string_view message)
{
    const auto now =
        std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    const auto day = std::chrono::floor<std::chrono::days>(now);
    const std::chrono::year_month_day date(day);
    const std::chrono::hh_mm_ss time(now - day);
    char prefix[96]{};
    const int length = std::snprintf(
        prefix, sizeof(prefix), "%04d-%02u-%02uT%02lld:%02lld:%02lld.%03lldZ [%s] ",
        static_cast<int>(date.year()), static_cast<unsigned int>(date.month()),
        static_cast<unsigned int>(date.day()), static_cast<long long>(time.hours().count()),
        static_cast<long long>(time.minutes().count()),
        static_cast<long long>(time.seconds().count()),
        static_cast<long long>(time.subseconds().count()), level_name(level));
    std::string record;
    if (length > 0)
    {
        record.append(prefix, std::min(static_cast<std::size_t>(length), sizeof(prefix) - 1));
    }
    const auto text = message.substr(0, maximum_message_size);
    if (!text.empty())
    {
        record.append(text.data(), text.size());
    }
    if (text.size() != message.size())
    {
        record.append(" [truncated]");
    }
    record.push_back('\n');
    return record;
}

std::filesystem::path archive_path(const std::filesystem::path& path, unsigned int index)
{
    auto result = path;
    result += "." + std::to_string(index);
    return result;
}

} // namespace

// Quill's standard file sinks abort on I/O failure in no-exception mode. This
// official Sink adapter preserves the host's status-returning file/SDL contract.
// Only the backend thread accesses the stream while the backend is running.
struct LogSystem::Output final : quill::Sink
{
    [[nodiscard]] bool open(const std::filesystem::path& path)
    {
        file.open(path, std::ios::binary | std::ios::app);
        if (!file.is_open())
        {
            return false;
        }
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error)
        {
            file.close();
            return false;
        }
        file_path = path;
        file_size = size;
        return true;
    }

    // SDL serializes its callback, but normal engine records arrive from Quill's
    // backend too. Preserve that serialization for the saved output function.
    void platform_output(int category, SDL_LogPriority priority, const char* message) noexcept
    {
        const std::lock_guard lock(platform_mutex);
        if (previous_output)
        {
            previous_output(previous_userdata, category, priority, message);
        }
    }

    static void SDLCALL sdl_output(void* userdata, int category, SDL_LogPriority priority,
                                   const char* message)
    {
        auto& output = *static_cast<Output*>(userdata);
        output.platform_output(category, priority, message);
        if (!quill::Backend::is_running())
        {
            return;
        }
        const std::string_view source = message ? std::string_view(message) : std::string_view{};
        std::string text(source.substr(0, maximum_message_size));
        if (text.size() != source.size())
        {
            text.append(" [truncated]");
        }
        switch (priority)
        {
        case SDL_LOG_PRIORITY_TRACE:
            QUILL_LOG_TRACE_L3(output.sdl_logger, "{}", text);
            break;
        case SDL_LOG_PRIORITY_VERBOSE:
            QUILL_LOG_TRACE_L1(output.sdl_logger, "{}", text);
            break;
        case SDL_LOG_PRIORITY_DEBUG:
            QUILL_LOG_DEBUG(output.sdl_logger, "{}", text);
            break;
        case SDL_LOG_PRIORITY_INFO:
            QUILL_LOG_INFO(output.sdl_logger, "{}", text);
            break;
        case SDL_LOG_PRIORITY_WARN:
            QUILL_LOG_WARNING(output.sdl_logger, "{}", text);
            break;
        case SDL_LOG_PRIORITY_ERROR:
            QUILL_LOG_ERROR(output.sdl_logger, "{}", text);
            output.sdl_logger->flush_log();
            break;
        case SDL_LOG_PRIORITY_CRITICAL:
            QUILL_LOG_CRITICAL(output.sdl_logger, "{}", text);
            output.sdl_logger->flush_log();
            break;
        default:
            break;
        }
    }

    void report_failure() noexcept
    {
        if (!failed.exchange(true, std::memory_order_relaxed))
        {
            // Never re-enter LogSystem or take its producer/lifecycle mutex here.
            platform_output(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_ERROR,
                            "Log file output failed; records remain available in the platform log");
        }
    }

    void write_log(quill::MacroMetadata const*, std::uint64_t, std::string_view, std::string_view,
                   const std::string&, std::string_view logger_name, quill::LogLevel level,
                   std::string_view, std::string_view,
                   const std::vector<std::pair<std::string, std::string>>*, std::string_view,
                   std::string_view record) override
    {
        if (logger_name != "engine.sdl")
        {
            const SDL_LogPriority priority =
                level == quill::LogLevel::Error     ? SDL_LOG_PRIORITY_ERROR
                : level == quill::LogLevel::Warning ? SDL_LOG_PRIORITY_WARN
                                                    : SDL_LOG_PRIORITY_INFO;
            // SDL_LogMessageV removes one trailing CRLF before invoking its output.
            auto platform_record = record;
            if (!platform_record.empty() && platform_record.back() == '\n')
            {
                platform_record.remove_suffix(1);
                if (!platform_record.empty() && platform_record.back() == '\r')
                {
                    platform_record.remove_suffix(1);
                }
            }
            const std::string text(platform_record);
            platform_output(SDL_LOG_CATEGORY_APPLICATION, priority, text.c_str());
        }
        if (!file.is_open() || failed.load(std::memory_order_relaxed))
        {
            return;
        }
        if ((file_size >= maximum_file_size || record.size() > maximum_file_size - file_size) &&
            !rotate())
        {
            report_failure();
            return;
        }
        file.write(record.data(), static_cast<std::streamsize>(record.size()));
        if (!file)
        {
            report_failure();
            return;
        }
        file_size += record.size();
    }

    void flush_sink() override
    {
        if (file.is_open())
        {
            file.flush();
            if (!file)
            {
                report_failure();
            }
        }
    }

    void close() noexcept
    {
        if (file.is_open())
        {
            flush_sink();
            file.close();
            if (file.fail())
            {
                report_failure();
            }
        }
    }

    [[nodiscard]] bool rotate()
    {
        file.flush();
        const bool flushed = static_cast<bool>(file);
        file.close();
        if (!flushed || file.fail())
        {
            return false;
        }
        std::error_code error;
        std::filesystem::remove(archive_path(file_path, retained_files), error);
        if (error)
        {
            return false;
        }
        for (unsigned int index = retained_files; index > 1; --index)
        {
            const auto previous = archive_path(file_path, index - 1);
            const bool exists = std::filesystem::exists(previous, error);
            if (error)
            {
                return false;
            }
            if (exists)
            {
                std::filesystem::rename(previous, archive_path(file_path, index), error);
                if (error)
                {
                    return false;
                }
            }
        }
        std::filesystem::rename(file_path, archive_path(file_path, 1), error);
        if (error)
        {
            return false;
        }
        file.clear();
        file.open(file_path, std::ios::binary | std::ios::app);
        file_size = 0;
        return file.is_open();
    }

    std::ofstream file;
    std::filesystem::path file_path;
    std::uintmax_t file_size = 0;
    std::atomic<bool> failed{false};
    quill::Logger* logger = nullptr; // Borrowed until blocking frontend removal.
    quill::Logger* sdl_logger = nullptr;
    SDL_LogOutputFunction previous_output = nullptr;
    void* previous_userdata = nullptr;
    std::mutex platform_mutex;
};

LogSystem::LogSystem() noexcept
{
    // Pin Quill's managers before the LogSystem singleton destructor is registered.
    // Quill's later atexit hook drains/stops the backend before our destructor fallback.
    (void)quill::Backend::is_running();
}

LogSystem::~LogSystem() noexcept
{
    shutdown();
}

LogSystem& LogSystem::instance() noexcept
{
    static LogSystem log;
    return log;
}

bool LogSystem::on_initialize(const EngineConfig&)
{
    SDL_LogOutputFunction previous_output = nullptr;
    void* previous_userdata = nullptr;
    SDL_GetLogOutputFunction(&previous_output, &previous_userdata);
    std::unique_lock lock(mutex_);
    if (!App::is_initialized())
    {
        return false;
    }

    const auto directory = App::persistent_data_path() / "logs";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error || !std::filesystem::is_directory(directory, error) || error)
    {
        return false;
    }

    auto path = directory / "engine.log";
    auto output = std::make_shared<Output>();
    output->previous_output = previous_output;
    output->previous_userdata = previous_userdata;
    if (!output->open(path))
    {
        return false;
    }
    quill::BackendOptions options;
    options.check_printable_char = {};
    options.error_notifier = [weak = std::weak_ptr<Output>(output)](const std::string& message)
    {
        const auto text = "Quill backend: " + message;
        if (const auto sink = weak.lock())
        {
            sink->report_failure();
            sink->platform_output(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_ERROR,
                                  text.c_str());
        }
        else
        {
            SDL_GetDefaultLogOutputFunction()(nullptr, SDL_LOG_CATEGORY_APPLICATION,
                                              SDL_LOG_PRIORITY_ERROR, text.c_str());
        }
    };
    quill::Backend::start(options);
    const quill::PatternFormatterOptions format{"%(time) [%(log_level)] %(message)",
                                                "%Y-%m-%dT%H:%M:%S.%QmsZ", quill::Timezone::GmtTime,
                                                false};
    output->logger = quill::Frontend::create_or_get_logger("engine", output, format);
    output->sdl_logger = quill::Frontend::create_or_get_logger("engine.sdl", output, format);
    output->sdl_logger->set_log_level(quill::LogLevel::TraceL3);
    output_ = std::move(output);
    file_path_ = std::move(path);
    lock.unlock();
    // SDL calls the bridge while holding its own log mutex. Never acquire that
    // mutex while holding the producer/lifecycle mutex.
    SDL_SetLogOutputFunction(&Output::sdl_output, output_.get());
    return true;
}

void LogSystem::write(LogLevel level, std::string_view message)
{
    auto& log = instance();
    const std::lock_guard lock(log.mutex_);
    if (!log.output_ || !quill::Backend::is_running())
    {
        const auto record = format_record(level, message);
        const SDL_LogPriority priority = level == LogLevel::error     ? SDL_LOG_PRIORITY_ERROR
                                         : level == LogLevel::warning ? SDL_LOG_PRIORITY_WARN
                                                                      : SDL_LOG_PRIORITY_INFO;
        SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, priority, "%s", record.c_str());
        return;
    }
    std::string text(message.substr(0, maximum_message_size));
    if (text.size() != message.size())
    {
        text.append(" [truncated]");
    }
    switch (level)
    {
    case LogLevel::info:
        QUILL_LOG_INFO(log.output_->logger, "{}", text);
        break;
    case LogLevel::warning:
        QUILL_LOG_WARNING(log.output_->logger, "{}", text);
        break;
    case LogLevel::error:
        QUILL_LOG_ERROR(log.output_->logger, "{}", text);
        log.output_->logger->flush_log();
        break;
    }
}

bool LogSystem::flush()
{
    const std::lock_guard lock(mutex_);
    if (!output_ || !quill::Backend::is_running())
    {
        return false;
    }
    output_->logger->flush_log();
    output_->sdl_logger->flush_log();
    return !output_->failed.load(std::memory_order_relaxed);
}

const std::filesystem::path& LogSystem::file_path() const noexcept
{
    return file_path_;
}

void LogSystem::on_shutdown() noexcept
{
    // The host has stopped its producers. Restore first, outside mutex_: SDL
    // serializes replacement with any callback already enqueueing a record.
    if (output_)
    {
        SDL_LogOutputFunction current_output = nullptr;
        void* current_userdata = nullptr;
        SDL_GetLogOutputFunction(&current_output, &current_userdata);
        if (current_output == &Output::sdl_output && current_userdata == output_.get())
        {
            SDL_SetLogOutputFunction(output_->previous_output, output_->previous_userdata);
        }
    }
    const std::lock_guard lock(mutex_);
    if (output_)
    {
        if (quill::Backend::is_running())
        {
            output_->logger->flush_log();
            output_->sdl_logger->flush_log();
            quill::Frontend::remove_logger_blocking(output_->sdl_logger);
            quill::Frontend::remove_logger_blocking(output_->logger);
            quill::Backend::stop();
        }
        // No backend may use the stream now, including during static teardown.
        output_->logger = nullptr;
        output_->sdl_logger = nullptr;
        output_->close();
        output_.reset();
    }
    file_path_.clear();
}

} // namespace engine
