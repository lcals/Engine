#pragma once

#include <filesystem>
#include <string>

namespace engine::asset_pipeline
{

class ProjectLock final
{
public:
    ProjectLock() noexcept = default;
    ~ProjectLock() noexcept;
    ProjectLock(const ProjectLock&) = delete;
    ProjectLock& operator=(const ProjectLock&) = delete;

    [[nodiscard]] bool acquire(const std::filesystem::path& file, std::string& error);

private:
    [[maybe_unused]] void* handle_ = nullptr;
    [[maybe_unused]] int descriptor_ = -1;
};

} // namespace engine::asset_pipeline
