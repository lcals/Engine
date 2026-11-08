#pragma once

#include "runtime/resource/resource_catalog.hpp"
#include "runtime/resource/resource_codec.hpp"
#include "runtime/resource/resource_read.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "runtime/schema/resource_header_generated.h"
#include "runtime/system/system.hpp"

#include <map>
#include <memory>
#include <string>
#include <type_traits>

namespace engine
{
class TaskSystem;

struct ResourceReadConfig
{
    FileAsyncConfig files;
    std::uint32_t max_in_flight = 4;
    std::uint64_t max_in_flight_bytes = 64U * 1024U * 1024U;
    std::uint32_t completion_budget = 16;
};

class ResourceSystem final : public System
{
public:
    ResourceSystem() noexcept;
    ~ResourceSystem() noexcept override;

    template <typename TCodec> [[nodiscard]] bool add()
    {
        static_assert(std::is_base_of_v<ResourceCodec, TCodec>);
        using Value = typename TCodec::ResourceType;
        static_assert(
            std::is_same_v<Value, ResourceValue<typename Value::Data, Value::asset_kind>>);
        if (is_initialized())
        {
            return fail(ResourceError::busy, "Resource codecs are frozen while initialized");
        }
        auto codec = std::make_unique<TCodec>();
        if (codec->kind() != Value::asset_kind || codecs_.contains(codec->kind()))
        {
            return fail(ResourceError::identity_conflict,
                        "Duplicate or incompatible resource codec");
        }
        codecs_.emplace(codec->kind(), std::move(codec));
        error_ = ResourceError::none;
        last_error_.clear();
        return true;
    }

    [[nodiscard]] bool update_catalog(const schema::AssetCatalogDataT& data,
                                      const MountList& mounts);
    [[nodiscard]] const ResourceCatalog& catalog() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] bool configure_async(TaskSystem& file_tasks, TaskSystem& read_tasks,
                                       const ResourceReadConfig& config);
    [[nodiscard]] bool read(std::string_view guid, std::unique_ptr<Resource>& output);
    [[nodiscard]] bool begin_read(std::string_view guid, std::unique_ptr<ResourceRead>& output);
    [[nodiscard]] bool validate_memory(Resource& candidate);
    [[nodiscard]] bool begin_read_memory(std::unique_ptr<Resource>& candidate,
                                         std::unique_ptr<ResourceRead>& output);
    [[nodiscard]] bool validate_dependencies(const Resource& resource, ResourceView dependencies,
                                             ResourceError& error, std::string& message) const;
    [[nodiscard]] ResourceError error() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

    [[nodiscard]] static bool read_catalog(const std::filesystem::path& root,
                                           ResourceCatalog& output);
    [[nodiscard]] static bool read_resource(const ResourceCatalog& catalog, const MountList& mounts,
                                            std::string_view guid, ByteBuffer& payload);
    [[nodiscard]] static bool decode_resource(const schema::AssetEntryT& entry, ByteView entity,
                                              ByteBuffer& payload);
    [[nodiscard]] static bool encode_resource(std::string_view guid, schema::AssetKind kind,
                                              ByteView payload, ByteBuffer& output,
                                              std::uint32_t& header_size);
    [[nodiscard]] bool read_header(std::string_view guid, schema::ResourceHeaderDataT& output);

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    struct ReadState;
    [[nodiscard]] const ResourceCodec* codec(schema::AssetKind kind) const noexcept;
    [[nodiscard]] bool validate_memory_info(const Resource& resource);
    [[nodiscard]] static bool decode_single(const schema::AssetEntryT& entry,
                                            const ResourceCodec& codec, std::uint64_t generation,
                                            ByteView entity, std::unique_ptr<Resource>& output,
                                            std::string& message);
    [[nodiscard]] bool fail(ResourceError error, std::string message);
    void poll_reads();
    void shutdown_reads() noexcept;

    ResourceCatalog catalog_;
    VirtualFileSystem files_;
    std::uint64_t generation_ = 0;
    std::map<schema::AssetKind, std::unique_ptr<ResourceCodec>> codecs_;
    std::shared_ptr<ReadState> reads_;
    ResourceError error_ = ResourceError::none;
    std::string last_error_;
};
} // namespace engine
