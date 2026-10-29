#pragma once

#include <filesystem>
#include <string>

namespace engine::editor::asset_pipeline
{

class ProjectLock final
{
public:
    ProjectLock() noexcept = default;
    ~ProjectLock() noexcept;
    ProjectLock(const ProjectLock&) = delete;
    ProjectLock& operator=(const ProjectLock&) = delete;

    [[nodiscard]] bool acquire(const std::filesystem::path& file, std::string& error);
    // 返回 false 表示检查失败；返回 true 时 acquired 区分成功持锁与其他进程占用。
    [[nodiscard]] bool try_acquire(const std::filesystem::path& file, bool& acquired,
                                   std::string& error);

private:
    [[maybe_unused]] void* handle_ = nullptr;
    [[maybe_unused]] int descriptor_ = -1;
};

} // namespace engine::editor::asset_pipeline
