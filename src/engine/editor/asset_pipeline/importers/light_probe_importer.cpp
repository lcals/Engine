#include "editor/asset_pipeline/importers/light_probe_importer.hpp"
#include "runtime/resource/virtual_file_system.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importer_utils.hpp"
#include "runtime/resource/codecs/light_probe_resource_codec.hpp"
#include "runtime/resource/resource_system.hpp"

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

namespace engine::editor::asset_pipeline
{
std::string_view LightProbeImporter::id() const noexcept
{
    return "light_probe";
}
schema::AssetKind LightProbeImporter::kind() const noexcept
{
    return schema::AssetKind::light_probe;
}
bool LightProbeImporter::matches(const std::filesystem::path& source) const
{
    return source_extension(source) == ".lightprobe";
}
bool LightProbeImporter::inspect_batch(const ImportContext&, std::string& fingerprint,
                                       std::string& error)
{
    return schema_fingerprint({"light_probe.fbs", "common.fbs"}, fingerprint, error);
}
bool LightProbeImporter::convert(const ImportAsset& asset, const ImportContext& context,
                                 std::string& error) const
{
    ByteBuffer bytes;
    if (!VirtualFileSystem::read_file(asset.source, bytes) || bytes.empty() ||
        bytes.size() > 32U * 1024U * 1024U)
    {
        error = "Cannot read a bounded light probe source";
        return false;
    }
    const auto* source = reinterpret_cast<const std::uint8_t*>(bytes.data());
    flatbuffers::Verifier verifier(source, bytes.size());
    if (!schema::VerifyLightProbeDataBuffer(verifier))
    {
        error = "Invalid light probe FlatBuffers source";
        return false;
    }
    auto probes = schema::UnPackLightProbeData(source);
    probes->path = asset.path;
    if (!LightProbeResourceCodec::validate(*probes))
    {
        error = "Invalid light probe grid, coefficients, or bake transform";
        return false;
    }
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishLightProbeDataBuffer(builder,
                                       schema::LightProbeData::Pack(builder, probes.get()));
    return write_bytes(context.output / asset.guid,
                       std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                       error);
}
} // namespace engine::editor::asset_pipeline
