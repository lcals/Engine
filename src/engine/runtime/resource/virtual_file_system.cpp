#include "runtime/resource/virtual_file_system.hpp"

#include "runtime/performance/profiling.hpp"
#include "runtime/schema/asset_catalog_generated.h"
#include "runtime/task/task_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_asyncio.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <limits>
#include <memory>
#include <set>
#include <string_view>
#include <system_error>

namespace engine
{
namespace
{

constexpr std::uint64_t maximum_read_size = 64U * 1024U * 1024U;
using Stream = std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>;

bool valid_relative_path(std::string_view path)
{
    for (const unsigned char character : path)
    {
        if (character < 32 || character == 127 || character == '\\' || character == ':')
        {
            return false;
        }
    }
    for (;;)
    {
        const auto slash = path.find('/');
        const auto segment = path.substr(0, slash);
        if (segment.empty() || segment == "." || segment == "..")
        {
            return false;
        }
        if (slash == std::string_view::npos)
        {
            return true;
        }
        path.remove_prefix(slash + 1);
    }
}

bool apk_root(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return encoded == u8"apk:/assets" || encoded.starts_with(u8"apk:/assets/");
}

bool contained_file(const std::filesystem::path& root, const std::filesystem::path& relative)
{
    if (apk_root(root))
    {
        return true;
    }
    std::error_code error;
    const auto root_status = std::filesystem::symlink_status(root, error);
    if (error || !std::filesystem::is_directory(root_status) ||
        std::filesystem::is_symlink(root_status))
    {
        return false;
    }
    auto current = root;
    for (const auto& segment : relative)
    {
        current /= segment;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error || std::filesystem::is_symlink(status))
        {
            return false;
        }
    }
    return std::filesystem::is_regular_file(std::filesystem::symlink_status(current, error)) &&
           !error;
}

Stream open_file(const std::filesystem::path& path)
{
    Stream stream(nullptr, SDL_CloseIO);
    const auto encoded = path.u8string();
    if (encoded.empty() || encoded.find(u8'\0') != std::u8string::npos)
    {
        return stream;
    }
    std::string filename(encoded.begin(), encoded.end());
    const bool apk = filename.starts_with("apk:");
    if (apk)
    {
        constexpr std::string_view prefix = "apk:/assets/";
        if (!filename.starts_with(prefix) ||
            !valid_relative_path(std::string_view(filename).substr(prefix.size())))
        {
            return stream;
        }
#if defined(SDL_PLATFORM_ANDROID)
        filename.erase(0, prefix.size());
#else
        return stream;
#endif
    }
    else
    {
        SDL_PathInfo info{};
        if (!SDL_GetPathInfo(filename.c_str(), &info) || info.type != SDL_PATHTYPE_FILE)
        {
            return stream;
        }
    }
    stream.reset(SDL_IOFromFile(filename.c_str(), "rb"));
#if defined(SDL_PLATFORM_ANDROID)
    if (stream && apk &&
        SDL_GetPointerProperty(SDL_GetIOProperties(stream.get()),
                               SDL_PROP_IOSTREAM_ANDROID_AASSET_POINTER, nullptr) == nullptr)
    {
        // SDL 优先查私有目录；内置逻辑路径只接受真实的 AAsset。
        stream.reset();
    }
#endif
    return stream;
}

bool read_exact(SDL_IOStream* stream, ByteBuffer& output)
{
    std::size_t position = 0;
    while (position < output.size())
    {
        const auto count = SDL_ReadIO(stream, output.data() + position, output.size() - position);
        if (count == 0)
        {
            return false;
        }
        position += count;
    }
    return true;
}

} // namespace

struct FileRead::State
{
    FileReadStatus status = FileReadStatus::reading;
    FileError error = FileError::none;
    std::string message;
    ByteBuffer bytes;
    std::atomic<bool> cancelled{false};

    bool cancel() noexcept
    {
        if (status == FileReadStatus::delivered || status == FileReadStatus::failed)
        {
            return false;
        }
        cancelled.store(true, std::memory_order_relaxed);
        status = FileReadStatus::cancelled;
        error = FileError::cancelled;
        ByteBuffer{}.swap(bytes);
        return true;
    }
};

FileRead::FileRead(std::shared_ptr<State> state) noexcept : state_(std::move(state))
{
}

FileRead::~FileRead() noexcept
{
    (void)cancel();
}

FileReadStatus FileRead::status() const noexcept
{
    return state_->status;
}

bool FileRead::take(ByteBuffer& output)
{
    output.clear();
    if (state_->status != FileReadStatus::ready)
    {
        return false;
    }
    output = std::move(state_->bytes);
    state_->status = FileReadStatus::delivered;
    return true;
}

bool FileRead::cancel() noexcept
{
    return state_->cancel();
}

FileError FileRead::error() const noexcept
{
    return state_->error;
}

std::string_view FileRead::last_error() const noexcept
{
    if (state_->error == FileError::cancelled)
    {
        return "File read cancelled";
    }
    return state_->message;
}

struct VirtualFileSystem::AsyncState
{
    struct Request
    {
        std::atomic<bool> prepared{false};
        SDL_AsyncIO* file = nullptr;
        bool read_started = false;
        bool opened = false;
        bool read_done = false;
        bool close_done = false;
        bool close_success = true;
        bool success = false;
        std::uint64_t reserved_bytes = 0;
        ByteBuffer bytes;
        std::shared_ptr<FileRead::State> result;
    };

    TaskSystem* tasks = nullptr;
    FileAsyncConfig config;
    SDL_AsyncIOQueue* queue = nullptr;
    std::vector<std::shared_ptr<Request>> requests;
    std::vector<std::weak_ptr<FileRead::State>> operations;
    std::uint64_t in_flight_bytes = 0;

    ~AsyncState() noexcept
    {
        // 准备任务持 shared_ptr；最后一个准备任务返回之后才会销毁队列。
        // 尚未派发的回调随 requests 销毁，绝不回调已关闭的业务对象。
        for (const auto& request : requests)
        {
            if (request->file != nullptr)
            {
                while (!SDL_CloseAsyncIO(request->file, false, queue, request.get()))
                {
                    // 提交关闭可能需要分配任务；先回收已完成任务再重试，保留读缓冲。
                    SDL_AsyncIOOutcome discarded{};
                    (void)SDL_WaitAsyncIOResult(queue, &discarded, 1);
                }
                request->file = nullptr;
            }
        }
        SDL_DestroyAsyncIOQueue(queue);
    }
};

VirtualFileSystem::VirtualFileSystem() noexcept = default;
VirtualFileSystem::~VirtualFileSystem() noexcept
{
    shutdown_async();
}

VirtualFileSystem::VirtualFileSystem(VirtualFileSystem&& other) noexcept
{
    *this = std::move(other);
}

VirtualFileSystem& VirtualFileSystem::operator=(VirtualFileSystem&& other) noexcept
{
    // 移动只用于尚未启用异步读取的值；活动系统保持稳定地址。
    assert(!async_ && !other.async_);
    if (this != &other)
    {
        mounts_ = std::move(other.mounts_);
        error_ = other.error_;
        last_error_ = std::move(other.last_error_);
    }
    return *this;
}

bool VirtualFileSystem::fail(FileError error, std::string_view message)
{
    error_ = error;
    last_error_ = message;
    return false;
}

FileError VirtualFileSystem::error() const noexcept
{
    return error_;
}

std::string_view VirtualFileSystem::last_error() const noexcept
{
    return last_error_;
}

bool VirtualFileSystem::configure_async(TaskSystem& tasks, const FileAsyncConfig& config)
{
    const bool unclaimed =
        async_ && std::any_of(async_->operations.begin(), async_->operations.end(),
                              [](const auto& weak)
                              {
                                  const auto operation = weak.lock();
                                  return operation &&
                                         (operation->status == FileReadStatus::reading ||
                                          operation->status == FileReadStatus::ready);
                              });
    if (!tasks.is_initialized() || config.max_in_flight == 0 || config.max_in_flight_bytes == 0 ||
        (async_ && !async_->requests.empty()) || unclaimed)
    {
        return fail(FileError::busy, "Invalid file task configuration or active reads");
    }
    auto candidate = std::make_shared<AsyncState>();
    candidate->tasks = &tasks;
    candidate->config = config;
    candidate->queue = SDL_CreateAsyncIOQueue();
    if (candidate->queue == nullptr)
    {
        return fail(FileError::io_failed, "Cannot create asynchronous I/O queue");
    }
    shutdown_async();
    async_ = std::move(candidate);
    error_ = FileError::none;
    last_error_.clear();
    return true;
}

void VirtualFileSystem::shutdown_async() noexcept
{
    if (async_)
    {
        for (const auto& weak : async_->operations)
        {
            if (const auto operation = weak.lock())
            {
                (void)operation->cancel();
            }
        }
        // 只在关闭边界使用；确保打开任务不再使用借用的任务域。
        (void)async_->tasks->wait();
        async_.reset();
    }
}

bool VirtualFileSystem::begin_read(const schema::FileLocationT& location, std::uint64_t offset,
                                   std::uint64_t size, std::unique_ptr<FileRead>& output)
{
    if (output)
    {
        return fail(FileError::busy, "File read output already owns an operation");
    }
    if (!async_)
    {
        return fail(FileError::unavailable, "Asynchronous file reading is not configured");
    }
    const auto mount = std::find_if(mounts_.begin(), mounts_.end(), [&location](const auto& value)
                                    { return value.name == location.mount; });
    constexpr auto maximum_offset = static_cast<std::uint64_t>(std::numeric_limits<Sint64>::max());
    if (mount == mounts_.end() || !valid_relative_path(location.path))
    {
        return fail(FileError::invalid_location, "Invalid file mount or relative path");
    }
    if (offset > location.length || size > location.length - offset ||
        location.offset > maximum_offset || location.length > maximum_offset - location.offset ||
        size > std::numeric_limits<std::size_t>::max())
    {
        return fail(FileError::invalid_range, "Invalid file range");
    }
    if (size > async_->config.max_in_flight_bytes)
    {
        return fail(FileError::budget_exceeded, "File range exceeds the asynchronous byte budget");
    }
    if (async_->requests.size() >= async_->config.max_in_flight ||
        size > async_->config.max_in_flight_bytes - async_->in_flight_bytes)
    {
        return fail(FileError::busy, "Asynchronous file budget is in use");
    }
    auto request = std::make_shared<AsyncState::Request>();
    request->reserved_bytes = size;
    request->result = std::make_shared<FileRead::State>();
    const auto root = mount->root;
    const auto relative = PathUtils::from_utf8(location.path);
    const auto state = async_;
    auto preparation = state->tasks->submit(
        "File.prepare",
        [state, request, root, relative, location, offset, size]()
        {
            ENGINE_PROFILE_ZONE("File.prepare");
            auto finish = [&request](bool success)
            {
                request->prepared.store(true, std::memory_order_release);
                return success;
            };
            if (request->result->cancelled.load(std::memory_order_relaxed) ||
                !contained_file(root, relative))
            {
                return finish(false);
            }
            const auto path = root / relative;
            auto filename = PathUtils::to_utf8(path);
            if (apk_root(root))
            {
                // SDL generic AsyncIO 同样使用 SDL_IOFromFile；预检拒绝私有文件遮挡。
                auto verified = open_file(path);
                if (!verified)
                {
                    return finish(false);
                }
                filename.erase(0, sizeof("apk:/assets/") - 1);
            }
            request->file = SDL_AsyncIOFromFile(filename.c_str(), "r");
            if (request->file == nullptr)
            {
                return finish(false);
            }
            request->opened = true;
            const auto length = SDL_GetAsyncIOSize(request->file);
            if (length >= 0 &&
                static_cast<std::uint64_t>(length) >= location.offset + location.length)
            {
                request->bytes.resize(static_cast<std::size_t>(size));
                if (size == 0)
                {
                    request->success = true;
                }
                else
                {
                    request->read_started = SDL_ReadAsyncIO(request->file, request->bytes.data(),
                                                            location.offset + offset, size,
                                                            state->queue, request.get());
                }
            }
            if (SDL_CloseAsyncIO(request->file, false, state->queue, request.get()))
            {
                request->file = nullptr;
            }
            return finish(request->read_started || request->success);
        },
        TaskPriority::normal);
    if (!preparation.valid())
    {
        return fail(FileError::busy, "File task domain rejected the preparation task");
    }
    output = std::unique_ptr<FileRead>(new FileRead(request->result));
    state->operations.push_back(request->result);
    state->requests.push_back(std::move(request));
    state->in_flight_bytes += size;
    ENGINE_PROFILE_PLOT("File.InFlight", static_cast<std::int64_t>(state->requests.size()));
    ENGINE_PROFILE_PLOT("File.InFlightBytes", static_cast<std::int64_t>(state->in_flight_bytes));
    error_ = FileError::none;
    last_error_.clear();
    return true;
}

std::uint32_t VirtualFileSystem::poll_async(std::uint32_t completion_budget)
{
    ENGINE_PROFILE_ZONE("File.complete");
    const auto state = async_;
    if (!state || completion_budget == 0)
    {
        return 0;
    }
    const auto maximum = std::min<std::size_t>(completion_budget, state->requests.size());
    SDL_AsyncIOOutcome outcome{};
    // 每项只有 read/close 两个结果；回调预算也约束本次出队工作。
    for (std::size_t count = 0; count < maximum * 2 && SDL_GetAsyncIOResult(state->queue, &outcome);
         ++count)
    {
        auto& request = *static_cast<AsyncState::Request*>(outcome.userdata);
        if (outcome.type == SDL_ASYNCIO_TASK_READ)
        {
            request.read_done = true;
            request.success = outcome.result == SDL_ASYNCIO_COMPLETE &&
                              outcome.bytes_transferred == outcome.bytes_requested;
        }
        else if (outcome.type == SDL_ASYNCIO_TASK_CLOSE)
        {
            request.close_done = true;
            if (outcome.result != SDL_ASYNCIO_COMPLETE)
            {
                request.close_success = false;
            }
        }
    }
    std::uint32_t dispatched = 0;
    for (std::size_t index = 0; index < state->requests.size() && dispatched < maximum;)
    {
        const auto request = state->requests[index];
        if (!request->prepared.load(std::memory_order_acquire))
        {
            ++index;
            continue;
        }
        if (request->file != nullptr &&
            SDL_CloseAsyncIO(request->file, false, state->queue, request.get()))
        {
            request->file = nullptr;
        }
        if ((request->read_started && !request->read_done) ||
            (request->opened && !request->close_done) || request->file != nullptr)
        {
            ++index;
            continue;
        }
        state->in_flight_bytes -= request->reserved_bytes;
        state->requests.erase(state->requests.begin() + static_cast<std::ptrdiff_t>(index));
        ++dispatched;
        const bool success = request->success && request->close_success;
        auto& result = *request->result;
        if (result.status == FileReadStatus::reading)
        {
            if (success)
            {
                result.bytes = std::move(request->bytes);
                result.status = FileReadStatus::ready;
            }
            else
            {
                result.error = FileError::io_failed;
                result.message = "Cannot open, read or close the requested file range";
                result.status = FileReadStatus::failed;
            }
        }
    }
    std::erase_if(state->operations,
                  [](const auto& weak)
                  {
                      const auto operation = weak.lock();
                      return !operation || operation->status == FileReadStatus::delivered ||
                             operation->status == FileReadStatus::failed ||
                             operation->status == FileReadStatus::cancelled;
                  });
    ENGINE_PROFILE_PLOT("File.InFlight", static_cast<std::int64_t>(state->requests.size()));
    ENGINE_PROFILE_PLOT("File.InFlightBytes", static_cast<std::int64_t>(state->in_flight_bytes));
    return dispatched;
}

bool VirtualFileSystem::configure(const MountList& mounts)
{
    std::set<std::string> names;
    for (const auto& mount : mounts)
    {
        const auto encoded = mount.root.generic_u8string();
        if (!valid_relative_path(mount.name) || mount.name.find('/') != std::string::npos ||
            !names.insert(mount.name).second || encoded.empty() ||
            encoded.find(u8'\0') != std::u8string::npos)
        {
            return fail(FileError::invalid_location, "Invalid or duplicate resource mount");
        }
        if (encoded.starts_with(u8"apk:"))
        {
            if (!apk_root(mount.root))
            {
                return fail(FileError::invalid_location, "Invalid APK resource mount");
            }
            constexpr std::size_t prefix_size = sizeof("apk:/assets/") - 1;
            if (encoded.size() >= prefix_size)
            {
                const std::string remainder(encoded.begin() + prefix_size, encoded.end());
                if (!valid_relative_path(remainder))
                {
                    return fail(FileError::invalid_location, "Invalid APK relative resource root");
                }
            }
        }
    }
    mounts_ = mounts;
    error_ = FileError::none;
    last_error_.clear();
    return true;
}

bool VirtualFileSystem::read(const schema::FileLocationT& location, std::uint64_t offset,
                             std::uint64_t size, ByteBuffer& output)
{
    output.clear();
    const auto mount = std::find_if(mounts_.begin(), mounts_.end(), [&location](const auto& value)
                                    { return value.name == location.mount; });
    constexpr auto maximum_offset = static_cast<std::uint64_t>(std::numeric_limits<Sint64>::max());
    if (mount == mounts_.end() || !valid_relative_path(location.path))
    {
        return fail(FileError::invalid_location, "Invalid file mount or relative path");
    }
    if (offset > location.length || size > location.length - offset ||
        size > std::numeric_limits<std::size_t>::max() || location.offset > maximum_offset ||
        location.length > maximum_offset - location.offset)
    {
        return fail(FileError::invalid_range, "Invalid file range");
    }
    const std::filesystem::path relative = PathUtils::from_utf8(location.path);
    if (!contained_file(mount->root, relative))
    {
        return fail(FileError::io_failed, "File is missing or escapes its mount");
    }
    auto stream = open_file(mount->root / relative);
    if (!stream)
    {
        return fail(FileError::io_failed, "Cannot open requested file");
    }
    const Sint64 file_size = SDL_GetIOSize(stream.get());
    const auto start = static_cast<Sint64>(location.offset + offset);
    if (file_size < 0 ||
        static_cast<std::uint64_t>(file_size) < location.offset + location.length ||
        SDL_SeekIO(stream.get(), start, SDL_IO_SEEK_SET) != start)
    {
        return fail(FileError::io_failed, "Cannot locate the complete file range");
    }
    output.resize(static_cast<std::size_t>(size));
    const bool success = read_exact(stream.get(), output);
    const bool closed = SDL_CloseIO(stream.release());
    if (!success || !closed)
    {
        output.clear();
        return fail(FileError::io_failed, "Cannot read or close the complete file range");
    }
    error_ = FileError::none;
    last_error_.clear();
    return true;
}

bool VirtualFileSystem::read_file(const std::filesystem::path& path, ByteBuffer& output)
{
    output.clear();
    auto stream = open_file(path);
    if (!stream)
    {
        return false;
    }
    const Sint64 size = SDL_GetIOSize(stream.get());
    if (size < 0 || static_cast<std::uint64_t>(size) > maximum_read_size)
    {
        return false;
    }
    output.resize(static_cast<std::size_t>(size));
    bool success = read_exact(stream.get(), output);
    if (success)
    {
        std::byte extra{};
        success = SDL_ReadIO(stream.get(), &extra, 1) == 0 &&
                  SDL_GetIOStatus(stream.get()) == SDL_IO_STATUS_EOF;
    }
    const bool closed = SDL_CloseIO(stream.release());
    if (!success || !closed)
    {
        output.clear();
        return false;
    }
    return true;
}

bool VirtualFileSystem::hash_file(const std::filesystem::path& path, std::uint64_t& hash,
                                  std::uint64_t& size)
{
    hash = 0;
    size = 0;
    auto stream = open_file(path);
    if (!stream)
    {
        return false;
    }
    const Sint64 expected_size = SDL_GetIOSize(stream.get());
    if (expected_size < 0)
    {
        return false;
    }
    const auto expected = static_cast<std::uint64_t>(expected_size);
    std::array<std::byte, 64U * 1024U> buffer{};
    std::uint64_t checksum = 14695981039346656037ULL;
    std::uint64_t position = 0;
    bool success = false;
    for (;;)
    {
        const auto count = SDL_ReadIO(stream.get(), buffer.data(), buffer.size());
        if (count == 0)
        {
            success = position == expected && SDL_GetIOStatus(stream.get()) == SDL_IO_STATUS_EOF;
            break;
        }
        if (count > expected - position)
        {
            break;
        }
        position += count;
        for (std::size_t index = 0; index < count; ++index)
        {
            checksum ^= std::to_integer<std::uint8_t>(buffer[index]);
            checksum *= 1099511628211ULL;
        }
    }
    const bool closed = SDL_CloseIO(stream.release());
    if (!success || !closed)
    {
        return false;
    }
    hash = checksum;
    size = position;
    return true;
}

const MountList& VirtualFileSystem::mounts() const noexcept
{
    return mounts_;
}

} // namespace engine
