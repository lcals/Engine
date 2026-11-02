#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine
{

class TaskSystem;

enum class FileReadStatus
{
    accepted,
    busy,
    failed
};

struct FileAsyncConfig
{
    std::size_t max_in_flight = 4;
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
                            std::uint64_t size, ByteBuffer& output) const;
    [[nodiscard]] static bool read_file(const std::filesystem::path& path, ByteBuffer& output);
    // 固定 64 KiB 缓冲计算完整文件 FNV-1a 64 位摘要；失败两项输出清零。
    [[nodiscard]] static bool hash_file(const std::filesystem::path& path, std::uint64_t& hash,
                                        std::uint64_t& size);
    [[nodiscard]] const MountList& mounts() const noexcept;
    // tasks 必须先初始化并覆盖本对象；配置/提交/派发仅由所属线程调用。
    [[nodiscard]] bool configure_async(TaskSystem& tasks, const FileAsyncConfig& config = {});
    void shutdown_async() noexcept;
    [[nodiscard]] FileReadStatus read_async(const schema::FileLocationT& location,
                                            std::uint64_t offset, std::uint64_t size,
                                            std::function<void(bool, ByteBuffer)> completed);
    // 不等待文件完成；回调在调用线程执行。析构取消回调，仅关闭时可等 I/O 收尾。
    std::size_t poll_async(std::size_t max_callbacks);

private:
    struct AsyncState;
    MountList mounts_;
    std::shared_ptr<AsyncState> async_;
};

} // namespace engine
