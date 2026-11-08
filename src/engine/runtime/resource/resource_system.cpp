#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/hash_utils.hpp"

#include "runtime/resource/resource_read_state.hpp"
#include "runtime/task/task_system.hpp"

#include <SDL3/SDL_stdinc.h>
#include <flatbuffers/verifier.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <set>
#include <utility>

namespace engine
{
namespace
{
constexpr std::uint64_t maximum_read_size = 64U * 1024U * 1024U;
std::string_view target_platform() noexcept
{
#if defined(SDL_PLATFORM_ANDROID)
    return "android";
#elif defined(SDL_PLATFORM_IOS)
    return "ios";
#elif defined(SDL_PLATFORM_MACOS)
    return "macos";
#elif defined(SDL_PLATFORM_WINDOWS)
    return "windows";
#else
    return "linux";
#endif
}

bool matches_content(ByteView bytes, const schema::AssetEntryT& entry)
{
    if (entry.content_size != bytes.size() || entry.content_hash.size() != 16)
    {
        return false;
    }
    return entry.content_hash == hash::hex(hash::fnv1a(bytes));
}

bool parse_header(const schema::AssetEntryT& entry, ByteView bytes,
                  schema::ResourceHeaderDataT& output)
{
    output = {};
    if (entry.header_size < 16 || entry.header_size > 65536 ||
        entry.header_size > entry.content_size || bytes.size() < entry.header_size ||
        std::memcmp(bytes.data(), "ERS1", 4) != 0)
    {
        return false;
    }
    std::uint32_t length = 0;
    for (std::size_t index = 0; index < 4; ++index)
    {
        length |= std::to_integer<std::uint32_t>(bytes[index + 4]) << (index * 8);
    }
    if (length != entry.header_size - 8)
    {
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(bytes.data() + 8), length);
    if (!schema::VerifyResourceHeaderDataBuffer(verifier))
    {
        return false;
    }
    auto header = schema::UnPackResourceHeaderData(bytes.data() + 8);
    if (header->guid != entry.guid || header->kind != entry.kind ||
        header->format_version != entry.format_version || header->format_version != 1 ||
        header->payload_offset != entry.header_size ||
        header->payload_size != entry.content_size - entry.header_size)
    {
        return false;
    }
    output = std::move(*header);
    return true;
}

const ResourceMount* find_mount(const MountList& mounts, std::string_view name)
{
    const auto found = std::find_if(mounts.begin(), mounts.end(),
                                    [name](const auto& mount) { return mount.name == name; });
    return found == mounts.end() ? nullptr : &*found;
}

ResourceError file_error(FileError value)
{
    if (value == FileError::busy)
    {
        return ResourceError::busy;
    }
    if (value == FileError::budget_exceeded)
    {
        return ResourceError::budget_exceeded;
    }
    if (value == FileError::unavailable)
    {
        return ResourceError::unavailable;
    }
    return ResourceError::io_failed;
}
} // namespace

struct ResourceSystem::ReadState
{
    struct Work
    {
        std::shared_ptr<ResourceRead::State> result;
        schema::AssetEntryT entry;
        const ResourceCodec* codec = nullptr;
        std::uint64_t generation = 0;
        std::uint64_t cost = 0;
        bool memory = false;
        std::unique_ptr<FileRead> file;
        ByteBuffer bytes;
        std::unique_ptr<Resource> candidate;
        std::string message;
        std::future<bool> decoding;
    };
    TaskSystem* tasks = nullptr;
    ResourceReadConfig config;
    std::uint64_t in_flight_bytes = 0;
    std::vector<std::shared_ptr<Work>> pending;
    std::vector<std::weak_ptr<ResourceRead::State>> operations;

    bool has_operations() const noexcept
    {
        return !pending.empty() ||
               std::any_of(operations.begin(), operations.end(),
                           [](const auto& weak)
                           {
                               const auto value = weak.lock();
                               return value && (value->status == ResourceReadStatus::reading ||
                                                value->status == ResourceReadStatus::ready);
                           });
    }
};

ResourceSystem::ResourceSystem() noexcept = default;
ResourceSystem::~ResourceSystem() noexcept
{
    shutdown();
}
const ResourceCatalog& ResourceSystem::catalog() const noexcept
{
    return catalog_;
}
std::uint64_t ResourceSystem::generation() const noexcept
{
    return generation_;
}
ResourceError ResourceSystem::error() const noexcept
{
    return error_;
}
std::string_view ResourceSystem::last_error() const noexcept
{
    return last_error_;
}

bool ResourceSystem::fail(ResourceError error, std::string message)
{
    error_ = error;
    last_error_ = std::move(message);
    return false;
}

const ResourceCodec* ResourceSystem::codec(schema::AssetKind kind) const noexcept
{
    const auto found = codecs_.find(kind);
    return found == codecs_.end() ? nullptr : found->second.get();
}

bool ResourceSystem::configure_async(TaskSystem& file_tasks, TaskSystem& read_tasks,
                                     const ResourceReadConfig& config)
{
    if (!is_initialized() || !file_tasks.is_initialized() || !read_tasks.is_initialized() ||
        &file_tasks == &read_tasks || config.max_in_flight == 0 ||
        config.max_in_flight_bytes == 0 || config.completion_budget == 0)
    {
        return fail(ResourceError::unavailable, "Invalid resource read task domains or budgets");
    }
    if (reads_ && reads_->has_operations())
    {
        return fail(ResourceError::busy, "Resource reads still own work or unclaimed results");
    }
    if (!files_.configure_async(file_tasks, config.files))
    {
        return fail(file_error(files_.error()), std::string(files_.last_error()));
    }
    reads_ = std::make_shared<ReadState>();
    reads_->tasks = &read_tasks;
    reads_->config = config;
    error_ = ResourceError::none;
    last_error_.clear();
    return true;
}

bool ResourceSystem::update_catalog(const schema::AssetCatalogDataT& data, const MountList& mounts)
{
    ResourceCatalog candidate;
    VirtualFileSystem validation;
    if (generation_ == std::numeric_limits<std::uint64_t>::max() ||
        data.target_platform != target_platform() || !candidate.assign(data) ||
        !validation.configure(mounts))
    {
        return fail(ResourceError::invalid_data, "Invalid resource catalog, platform or mounts");
    }
    for (const auto& entry : candidate.data().entries)
    {
        if (entry->location && !find_mount(mounts, entry->location->mount))
        {
            return fail(ResourceError::invalid_data,
                        "Missing resource mount: " + entry->location->mount);
        }
    }
    if (!files_.configure(mounts))
    {
        return fail(ResourceError::invalid_data, "Invalid resource mounts");
    }
    catalog_ = std::move(candidate);
    ++generation_;
    error_ = ResourceError::none;
    last_error_.clear();
    return true;
}

bool ResourceSystem::read(std::string_view guid, std::unique_ptr<Resource>& output)
{
    if (output)
    {
        return fail(ResourceError::busy, "Resource output must be empty");
    }
    if (!is_initialized())
    {
        return fail(ResourceError::unavailable, "Resource system is unavailable");
    }
    const auto* entry = catalog_.find(guid);
    if (!entry)
    {
        return fail(ResourceError::not_found, "Unknown resource: " + std::string(guid));
    }
    const auto* handler = codec(entry->kind);
    if (!handler)
    {
        return fail(ResourceError::unsupported_type, "Resource codec is not registered");
    }
    if (!entry->location)
    {
        return fail(ResourceError::not_loaded,
                    "Resource has no file location; load its memory definition explicitly");
    }
    ByteBuffer entity;
    if (!files_.read(*entry->location, 0, entry->content_size, entity))
    {
        return fail(file_error(files_.error()), std::string(files_.last_error()));
    }
    if (!decode_single(*entry, *handler, generation_, entity, output, last_error_))
    {
        error_ = ResourceError::invalid_data;
        return false;
    }
    error_ = ResourceError::none;
    last_error_.clear();
    return true;
}

bool ResourceSystem::begin_read(std::string_view guid, std::unique_ptr<ResourceRead>& output)
{
    if (output)
    {
        return fail(ResourceError::busy, "Read operation output must be empty");
    }
    if (!is_initialized() || !reads_)
    {
        return fail(ResourceError::unavailable, "Resource async reads are unavailable");
    }
    const auto* entry = catalog_.find(guid);
    if (!entry)
    {
        return fail(ResourceError::not_found, "Unknown resource: " + std::string(guid));
    }
    const auto* handler = codec(entry->kind);
    if (!handler)
    {
        return fail(ResourceError::unsupported_type, "Resource codec is not registered");
    }
    if (!entry->location)
    {
        return fail(ResourceError::not_loaded, "Resource has no physical location");
    }
    if (entry->content_size > reads_->config.max_in_flight_bytes)
    {
        return fail(ResourceError::budget_exceeded, "Resource exceeds single-read byte budget");
    }
    if (reads_->pending.size() >= reads_->config.max_in_flight ||
        entry->content_size > reads_->config.max_in_flight_bytes - reads_->in_flight_bytes)
    {
        return fail(ResourceError::busy, "Resource read budget is temporarily full");
    }
    auto work = std::make_shared<ReadState::Work>();
    work->result = std::make_shared<ResourceRead::State>();
    work->entry = *entry;
    work->codec = handler;
    work->generation = generation_;
    work->cost = entry->content_size;
    if (!files_.begin_read(*entry->location, 0, entry->content_size, work->file))
    {
        return fail(file_error(files_.error()), std::string(files_.last_error()));
    }
    reads_->in_flight_bytes += work->cost;
    reads_->pending.push_back(work);
    reads_->operations.push_back(work->result);
    output = std::unique_ptr<ResourceRead>(new ResourceRead(work->result));
    error_ = ResourceError::none;
    last_error_.clear();
    return true;
}

bool ResourceSystem::validate_memory_info(const Resource& resource)
{
    if (!is_initialized())
    {
        return fail(ResourceError::unavailable, "Resource system is unavailable");
    }
    const auto& info = resource.info();
    if (!GuidMap::valid_guid(info.guid) || !GuidMap::valid_path(info.path) ||
        info.path.starts_with("editor://") || info.kind != resource.kind() || info.location ||
        info.format_version != 1 || info.content_size != 0 || info.header_size != 0 ||
        !info.content_hash.empty())
    {
        return fail(ResourceError::invalid_data, "Invalid memory resource metadata");
    }
    if (!codec(info.kind))
    {
        return fail(ResourceError::unsupported_type, "Resource codec is not registered");
    }
    const auto* current = catalog_.find(info.guid);
    Guid existing;
    if ((current && (current->location || *current != info)) ||
        (catalog_.identities().path_to_guid(info.path, existing) && existing != info.guid))
    {
        return fail(ResourceError::identity_conflict,
                    "Memory identity conflicts with the current catalog");
    }
    std::set<std::string_view> dependencies;
    for (const auto& guid : info.dependencies)
    {
        if (!GuidMap::valid_guid(guid) || guid == info.guid || !dependencies.insert(guid).second)
        {
            return fail(ResourceError::invalid_data,
                        "Invalid memory resource dependency declaration");
        }
    }
    return true;
}

bool ResourceSystem::validate_memory(Resource& candidate)
{
    if (!validate_memory_info(candidate))
    {
        return false;
    }
    if (!codec(candidate.kind())->validate_data(candidate, last_error_))
    {
        if (last_error_.empty())
        {
            last_error_ = "Invalid memory resource data";
        }
        error_ = ResourceError::invalid_data;
        return false;
    }
    candidate.generation_ = generation_;
    error_ = ResourceError::none;
    last_error_.clear();
    return true;
}

bool ResourceSystem::begin_read_memory(std::unique_ptr<Resource>& candidate,
                                       std::unique_ptr<ResourceRead>& output)
{
    if (output)
    {
        return fail(ResourceError::busy, "Read operation output must be empty");
    }
    if (!is_initialized() || !reads_)
    {
        return fail(ResourceError::unavailable, "Resource async reads are unavailable");
    }
    if (!candidate)
    {
        return fail(ResourceError::invalid_data, "Memory resource is empty");
    }
    if (!validate_memory_info(*candidate))
    {
        return false;
    }
    if (reads_->pending.size() >= reads_->config.max_in_flight)
    {
        return fail(ResourceError::busy, "Resource read budget is temporarily full");
    }
    auto work = std::make_shared<ReadState::Work>();
    work->result = std::make_shared<ResourceRead::State>();
    work->entry = candidate->info();
    work->codec = codec(candidate->kind());
    work->generation = generation_;
    work->memory = true;
    work->candidate = std::move(candidate);
    reads_->pending.push_back(work);
    reads_->operations.push_back(work->result);
    output = std::unique_ptr<ResourceRead>(new ResourceRead(work->result));
    error_ = ResourceError::none;
    last_error_.clear();
    return true;
}

bool ResourceSystem::decode_single(const schema::AssetEntryT& entry, const ResourceCodec& handler,
                                   std::uint64_t generation, ByteView entity,
                                   std::unique_ptr<Resource>& output, std::string& message)
{
    ByteBuffer payload;
    message.clear();
    if (!decode_resource(entry, entity, payload))
    {
        message = "Invalid resource envelope or hash: " + entry.path;
        return false;
    }
    std::unique_ptr<Resource> candidate;
    if (!handler.decode(entry, payload, candidate, message) || !candidate ||
        candidate->info() != entry || candidate->kind() != entry.kind ||
        !handler.validate_data(*candidate, message))
    {
        if (message.empty())
        {
            message = "Invalid resource payload: " + entry.path;
        }
        return false;
    }
    candidate->generation_ = generation;
    output = std::move(candidate);
    return true;
}

bool ResourceSystem::validate_dependencies(const Resource& resource, ResourceView dependencies,
                                           ResourceError& error, std::string& message) const
{
    error = ResourceError::none;
    message.clear();
    if (!is_initialized())
    {
        error = ResourceError::unavailable;
        message = "Resource system is unavailable";
        return false;
    }
    const auto* handler = codec(resource.kind());
    if (!handler)
    {
        error = ResourceError::unsupported_type;
        message = "Resource codec is not registered";
        return false;
    }
    std::set<std::string_view> references;
    for (const auto* item : dependencies)
    {
        if (!item || item->generation() != resource.generation() ||
            !references.insert(item->info().guid).second)
        {
            error = ResourceError::dependency_failed;
            message = "Duplicate or mixed-generation dependency";
            return false;
        }
    }
    if (dependencies.size() != resource.info().dependencies.size() ||
        !std::all_of(resource.info().dependencies.begin(), resource.info().dependencies.end(),
                     [&references](const auto& guid) { return references.contains(guid); }) ||
        !handler->validate_dependencies(resource, dependencies, message))
    {
        error = ResourceError::dependency_failed;
        if (message.empty())
        {
            message = "Invalid resource dependency contract: " + resource.info().path;
        }
        return false;
    }
    return true;
}

void ResourceSystem::poll_reads()
{
    const auto state = reads_;
    if (!state)
    {
        return;
    }
    std::uint32_t completed = 0;
    for (std::size_t index = 0;
         index < state->pending.size() && completed < state->config.completion_budget;)
    {
        const auto work = state->pending[index];
        auto& result = *work->result;
        auto fail = [&result](ResourceError error, std::string_view message)
        {
            result.cancelled.store(true, std::memory_order_relaxed);
            result.status = ResourceReadStatus::failed;
            result.error = error;
            result.message = message;
        };
        if (result.status == ResourceReadStatus::reading && generation_ != work->generation)
        {
            fail(ResourceError::generation_changed, "Resource catalog changed during reading");
        }
        if (result.status == ResourceReadStatus::reading && work->file)
        {
            if (work->file->status() == FileReadStatus::ready)
            {
                if (!work->file->take(work->bytes))
                {
                    fail(ResourceError::io_failed, "Cannot take completed file data");
                }
                work->file.reset();
            }
            else if (work->file->status() != FileReadStatus::reading)
            {
                fail(ResourceError::io_failed, work->file->last_error());
            }
        }
        if (result.status == ResourceReadStatus::reading && !work->file && !work->decoding.valid())
        {
            work->decoding = state->tasks->submit(
                "Resource.read.decode",
                [work]()
                {
                    if (work->result->cancelled.load(std::memory_order_relaxed))
                    {
                        return false;
                    }
                    bool success = false;
                    if (work->memory)
                    {
                        success = work->codec->validate_data(*work->candidate, work->message);
                        if (success)
                        {
                            work->candidate->generation_ = work->generation;
                        }
                    }
                    else
                    {
                        success = decode_single(work->entry, *work->codec, work->generation,
                                                work->bytes, work->candidate, work->message);
                    }
                    ByteBuffer{}.swap(work->bytes);
                    return success;
                });
        }
        if (result.status == ResourceReadStatus::reading && !work->file && !work->decoding.valid())
        {
            fail(ResourceError::busy, "Resource decode task domain rejected work");
        }
        const bool decoding = work->decoding.valid();
        const bool done = decoding && work->decoding.wait_for(std::chrono::seconds(0)) ==
                                          std::future_status::ready;
        if (done)
        {
            const bool success = work->decoding.get();
            if (result.status == ResourceReadStatus::reading)
            {
                if (success)
                {
                    result.resource = std::move(work->candidate);
                    result.status = ResourceReadStatus::ready;
                }
                else
                {
                    fail(ResourceError::invalid_data,
                         work->message.empty() ? "Invalid resource entity: " + work->entry.guid
                                               : work->message);
                }
            }
        }
        if (result.status != ResourceReadStatus::reading)
        {
            if (work->file)
            {
                (void)work->file->cancel();
            }
            if (!decoding || done)
            {
                state->in_flight_bytes -= work->cost;
                state->pending.erase(state->pending.begin() + static_cast<std::ptrdiff_t>(index));
                ++completed;
                continue;
            }
        }
        ++index;
    }
    std::erase_if(state->operations,
                  [](const auto& weak)
                  {
                      const auto result = weak.lock();
                      return !result || result->status == ResourceReadStatus::delivered ||
                             result->status == ResourceReadStatus::cancelled ||
                             result->status == ResourceReadStatus::failed;
                  });
}

void ResourceSystem::shutdown_reads() noexcept
{
    const auto state = std::move(reads_);
    if (!state)
    {
        return;
    }
    for (const auto& weak : state->operations)
    {
        if (const auto result = weak.lock())
        {
            (void)result->cancel();
        }
    }
    for (const auto& work : state->pending)
    {
        if (work->file)
        {
            (void)work->file->cancel();
        }
    }
    (void)state->tasks->wait();
}

bool ResourceSystem::on_initialize(const EngineConfig&)
{
    schema::AssetCatalogDataT data;
    data.schema_version = 2;
    data.revision = "builtin-only";
    data.target_platform = target_platform();
    return update_catalog(data, {});
}

bool ResourceSystem::on_tick(double)
{
    if (reads_)
    {
        (void)files_.poll_async(reads_->config.completion_budget);
        poll_reads();
    }
    return true;
}

void ResourceSystem::on_shutdown() noexcept
{
    shutdown_reads();
    files_.shutdown_async();
    files_ = {};
    catalog_ = {};
    error_ = ResourceError::none;
    last_error_.clear();
}

bool ResourceSystem::read_catalog(const std::filesystem::path& root, ResourceCatalog& output)
{
    output = {};
    ByteBuffer bytes;
    // 读取清单同样拒绝符号链接；目录实体大小不需要预先知道。
    const auto encoded = root.generic_u8string();
    if (!encoded.starts_with(u8"apk:/assets"))
    {
        std::error_code error;
        const auto directory = std::filesystem::symlink_status(root, error);
        if (error || !std::filesystem::is_directory(directory) ||
            std::filesystem::is_symlink(directory))
        {
            return false;
        }
        const auto file = std::filesystem::symlink_status(root / "catalog.bin", error);
        if (error || !std::filesystem::is_regular_file(file) || std::filesystem::is_symlink(file))
        {
            return false;
        }
    }
    if (!VirtualFileSystem::read_file(root / "catalog.bin", bytes))
    {
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size());
    if (!schema::VerifyAssetCatalogDataBuffer(verifier) ||
        schema::GetAssetCatalogData(bytes.data())->entries()->size() > 65536)
    {
        return false;
    }
    auto data = schema::UnPackAssetCatalogData(bytes.data());
    return output.assign(*data);
}

bool ResourceSystem::encode_resource(std::string_view guid, schema::AssetKind kind,
                                     ByteView payload, ByteBuffer& output,
                                     std::uint32_t& header_size)
{
    output.clear();
    header_size = 0;
    if (!GuidMap::valid_guid(guid) || kind < schema::AssetKind::MIN ||
        kind > schema::AssetKind::MAX || payload.size() > maximum_read_size)
    {
        return false;
    }
    schema::ResourceHeaderDataT header;
    header.guid = guid;
    header.kind = kind;
    header.payload_size = payload.size();
    flatbuffers::FlatBufferBuilder builder;
    builder.ForceDefaults(true);
    builder.Finish(schema::ResourceHeaderData::Pack(builder, &header), "ERSH");
    header.payload_offset = 8 + builder.GetSize();
    builder.Clear();
    builder.ForceDefaults(true);
    builder.Finish(schema::ResourceHeaderData::Pack(builder, &header), "ERSH");
    if (8 + builder.GetSize() != header.payload_offset || header.payload_offset > 65536 ||
        payload.size() > maximum_read_size - header.payload_offset)
    {
        return false;
    }
    header_size = static_cast<std::uint32_t>(header.payload_offset);
    output.resize(header_size + payload.size());
    std::memcpy(output.data(), "ERS1", 4);
    const auto length = static_cast<std::uint32_t>(builder.GetSize());
    for (std::size_t index = 0; index < 4; ++index)
    {
        output[index + 4] = static_cast<std::byte>((length >> (index * 8)) & 255U);
    }
    std::memcpy(output.data() + 8, builder.GetBufferPointer(), builder.GetSize());
    if (!payload.empty())
    {
        std::memcpy(output.data() + header_size, payload.data(), payload.size());
    }
    return true;
}

bool ResourceSystem::decode_resource(const schema::AssetEntryT& entry, ByteView entity,
                                     ByteBuffer& payload)
{
    payload.clear();
    schema::ResourceHeaderDataT header;
    if (!matches_content(entity, entry) || !parse_header(entry, entity, header))
    {
        return false;
    }
    payload.assign(entity.begin() + entry.header_size, entity.end());
    return true;
}

bool ResourceSystem::read_resource(const ResourceCatalog& catalog, const MountList& mounts,
                                   std::string_view guid, ByteBuffer& payload)
{
    payload.clear();
    const auto* entry = catalog.find(guid);
    VirtualFileSystem files;
    ByteBuffer entity;
    return entry && entry->location && files.configure(mounts) &&
           files.read(*entry->location, 0, entry->content_size, entity) &&
           decode_resource(*entry, entity, payload);
}

bool ResourceSystem::read_header(std::string_view guid, schema::ResourceHeaderDataT& output)
{
    output = {};
    error_ = ResourceError::none;
    last_error_.clear();
    if (!is_initialized())
    {
        return fail(ResourceError::unavailable, "Resource reader is unavailable");
    }
    const auto* entry = catalog_.find(guid);
    ByteBuffer bytes;
    if (entry == nullptr || !entry->location ||
        !files_.read(*entry->location, 0, entry->header_size, bytes) ||
        !parse_header(*entry, bytes, output))
    {
        output = {};
        return fail(ResourceError::io_failed, "Cannot read resource header: " + std::string(guid));
    }
    return true;
}

} // namespace engine
