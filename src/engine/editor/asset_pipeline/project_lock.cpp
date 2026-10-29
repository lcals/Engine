#include "editor/asset_pipeline/project_lock.hpp"

#include "runtime/utils/path_utils.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace engine::asset_pipeline
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
#if defined(_WIN32)
    const auto handle = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        error = "asset build is locked or lock file cannot be opened";
        return false;
    }
    handle_ = handle;
#else
    descriptor_ = open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor_ < 0 || flock(descriptor_, LOCK_EX | LOCK_NB) != 0)
    {
        error = "asset build is locked or lock file cannot be opened";
        if (descriptor_ >= 0)
        {
            close(descriptor_);
            descriptor_ = -1;
        }
        return false;
    }
#endif
    return true;
}

} // namespace engine::asset_pipeline
