#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine
{

class TaskSystem;

enum class FileReadStatus
{
    reading,
    ready,
    delivered,
    failed,
    cancelled
};

enum class FileError
{
    none,
    unavailable,
    busy,
    budget_exceeded,
    invalid_location,
    invalid_range,
    io_failed,
    cancelled
};

struct FileAsyncConfig
{
    std::uint32_t max_in_flight = 4;
    std::uint64_t max_in_flight_bytes = 64U * 1024U * 1024U;
};

namespace schema
{
struct FileLocationT;
} // namespace schema

using ByteBuffer = std::vector<std::byte>;

struct ResourceMount
{
    std::string name;
    std::filesystem::path root;
};

using MountList = std::vector<ResourceMount>;

class FileRead final
{
public:
    ~FileRead() noexcept;
    FileRead(const FileRead&) = delete;
    FileRead& operator=(const FileRead&) = delete;
    FileRead(FileRead&&) = delete;
    FileRead& operator=(FileRead&&) = delete;

    [[nodiscard]] FileReadStatus status() const noexcept;
    [[nodiscard]] bool take(ByteBuffer& output);
    [[nodiscard]] bool cancel() noexcept;
    [[nodiscard]] FileError error() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

private:
    friend class VirtualFileSystem;
    struct State;
    explicit FileRead(std::shared_ptr<State> state) noexcept;
    std::shared_ptr<State> state_;
};

class VirtualFileSystem final
{
public:
    VirtualFileSystem() noexcept;
    ~VirtualFileSystem() noexcept;
    VirtualFileSystem(VirtualFileSystem&&) noexcept;
    VirtualFileSystem& operator=(VirtualFileSystem&&) noexcept;
    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

    // 仅配置路径，不扫描或打开文件；失败保留旧挂载。
    [[nodiscard]] bool configure(const MountList& mounts);
    [[nodiscard]] bool read(const schema::FileLocationT& location, std::uint64_t offset,
                            std::uint64_t size, ByteBuffer& output);
    [[nodiscard]] static bool read_file(const std::filesystem::path& path, ByteBuffer& output);
    // 固定 64 KiB 缓冲计算完整文件 FNV-1a 64 位摘要；失败两项输出清零。
    [[nodiscard]] static bool hash_file(const std::filesystem::path& path, std::uint64_t& hash,
                                        std::uint64_t& size);
    [[nodiscard]] const MountList& mounts() const noexcept;
    [[nodiscard]] FileError error() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;
    // tasks 必须先初始化并覆盖本对象；配置/提交/派发仅由所属线程调用。
    [[nodiscard]] bool configure_async(TaskSystem& tasks, const FileAsyncConfig& config = {});
    void shutdown_async() noexcept;
    [[nodiscard]] bool begin_read(const schema::FileLocationT& location, std::uint64_t offset,
                                  std::uint64_t size, std::unique_ptr<FileRead>& output);
    // 不等待文件完成；仅所属系统推进，操作的 status/take 不推进 I/O。
    std::uint32_t poll_async(std::uint32_t completion_budget);

private:
    struct AsyncState;
    [[nodiscard]] bool fail(FileError error, std::string_view message);
    MountList mounts_;
    std::shared_ptr<AsyncState> async_;
    FileError error_ = FileError::none;
    std::string last_error_;
};

} // namespace engine
