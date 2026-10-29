#include "editor/asset_pipeline/project_lock.hpp"

#include "runtime/utils/path_utils.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
#include <string>

namespace engine::editor::asset_pipeline
{

ProjectLock::~ProjectLock() noexcept
{
#if defined(_WIN32)
    if (handle_ != nullptr)
    {
        CloseHandle(static_cast<HANDLE>(handle_));
    }
#else
    if (descriptor_ >= 0)
    {
        flock(descriptor_, LOCK_UN);
        close(descriptor_);
    }
#endif
}

bool ProjectLock::acquire(const std::filesystem::path& file, std::string& error)
{
    bool acquired = false;
    if (!try_acquire(file, acquired, error))
    {
        return false;
    }
    if (!acquired)
    {
        error = "asset build is locked";
    }
    return acquired;
}

bool ProjectLock::try_acquire(const std::filesystem::path& file, bool& acquired, std::string& error)
{
    acquired = false;
    error.clear();
    if (handle_ != nullptr || descriptor_ >= 0)
    {
        error = "lock object already owns a file";
        return false;
    }
#if defined(_WIN32)
    const auto handle = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        const auto code = GetLastError();
        if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION)
        {
            return true;
        }
        error = "lock file cannot be opened: " + std::to_string(code);
        return false;
    }
    handle_ = handle;
#else
    const int descriptor = open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0)
    {
        error = "lock file cannot be opened: " + std::to_string(errno);
        return false;
    }
    if (flock(descriptor, LOCK_EX | LOCK_NB) != 0)
    {
        const int code = errno;
        close(descriptor);
        if (code == EWOULDBLOCK || code == EAGAIN)
        {
            return true;
        }
        error = "cannot acquire file lock: " + std::to_string(code);
        return false;
    }
    descriptor_ = descriptor;
#endif
    acquired = true;
    return true;
}

} // namespace engine::editor::asset_pipeline
