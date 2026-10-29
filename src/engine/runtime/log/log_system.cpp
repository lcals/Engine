#include "runtime/log/log_system.hpp"

#include "runtime/app/app.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <system_error>

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

LogSystem::LogSystem() noexcept = default;

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
    const std::lock_guard lock(mutex_);
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
    file_.clear();
    file_.open(path, std::ios::binary | std::ios::app);
    if (!file_.is_open())
    {
        return false;
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error)
    {
        file_.close();
        return false;
    }
    file_path_ = std::move(path);
    file_size_ = size;
    file_failed_ = false;
    return true;
}

void LogSystem::write(LogLevel level, std::string_view message)
{
    auto& log = instance();
    const auto record = format_record(level, message);
    const std::lock_guard lock(log.mutex_);
    std::fwrite(record.data(), 1, record.size(), stderr);
    if (!log.file_.is_open() || log.file_failed_)
    {
        return;
    }
    if ((log.file_size_ >= maximum_file_size ||
         record.size() > maximum_file_size - log.file_size_) &&
        !log.rotate_file())
    {
        log.report_file_failure();
        return;
    }
    log.file_.write(record.data(), static_cast<std::streamsize>(record.size()));
    if (level == LogLevel::error)
    {
        log.file_.flush();
    }
    if (!log.file_)
    {
        log.report_file_failure();
        return;
    }
    log.file_size_ += record.size();
}

bool LogSystem::flush()
{
    const std::lock_guard lock(mutex_);
    if (!file_.is_open())
    {
        return false;
    }
    file_.flush();
    if (!file_)
    {
        report_file_failure();
    }
    return !file_failed_;
}

const std::filesystem::path& LogSystem::file_path() const noexcept
{
    return file_path_;
}

bool LogSystem::rotate_file()
{
    file_.flush();
    const bool flushed = static_cast<bool>(file_);
    file_.close();
    if (!flushed || file_.fail())
    {
        return false;
    }

    std::error_code error;
    std::filesystem::remove(archive_path(file_path_, retained_files), error);
    if (error)
    {
        return false;
    }
    for (unsigned int index = retained_files; index > 1; --index)
    {
        const auto previous = archive_path(file_path_, index - 1);
        const bool exists = std::filesystem::exists(previous, error);
        if (error)
        {
            return false;
        }
        if (exists)
        {
            std::filesystem::rename(previous, archive_path(file_path_, index), error);
            if (error)
            {
                return false;
            }
        }
    }
    std::filesystem::rename(file_path_, archive_path(file_path_, 1), error);
    if (error)
    {
        return false;
    }
    file_.clear();
    file_.open(file_path_, std::ios::binary | std::ios::app);
    file_size_ = 0;
    return file_.is_open();
}

void LogSystem::report_file_failure() noexcept
{
    if (!file_failed_)
    {
        std::fputs("Log file output failed; records remain available on stderr\n", stderr);
    }
    file_failed_ = true;
}

void LogSystem::on_shutdown() noexcept
{
    const std::lock_guard lock(mutex_);
    if (file_.is_open())
    {
        file_.flush();
        if (!file_)
        {
            report_file_failure();
        }
        file_.close();
        if (file_.fail())
        {
            report_file_failure();
        }
    }
    file_path_.clear();
    file_size_ = 0;
}

} // namespace engine
