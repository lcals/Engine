#include "resource_fixture.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importers/light_probe_importer.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/codecs/light_probe_resource_codec.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <map>

namespace
{
using namespace engine;
bool check(bool okay, const char* reason)
{
    if (!okay)
    {
        std::fprintf(stderr, "light probes: %s\n", reason);
    }
    return okay;
}

bool exercise()
{
    schema::LightProbeDataT data;
    data.path = "lighting/test.lightprobe";
    data.bounds_min = schema::Vec3(-1, -1, -1);
    data.bounds_max = schema::Vec3(1, 1, 1);
    data.grid_x = data.grid_y = data.grid_z = 2;
    data.baked_transform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    data.coefficients.resize(72);
    data.valid.assign(8, 1);
    data.source_fingerprint = "test-scene-v1";
    for (unsigned i = 0; i < 8; ++i)
    {
        data.coefficients[i * 9] = schema::Vec3(i + 0.5, 1, 2);
    }
    if (!check(LightProbeResourceCodec::validate(data), "valid SH9 grid accepted"))
    {
        return false;
    }
    auto invalid = data;
    invalid.baked_transform[15] = 0;
    if (!check(!LightProbeResourceCodec::validate(invalid), "non-affine transforms rejected"))
    {
        return false;
    }
    invalid = data;
    invalid.coefficients.pop_back();
    if (!check(!LightProbeResourceCodec::validate(invalid), "truncated SH arrays rejected"))
    {
        return false;
    }
    invalid = data;
    invalid.valid[2] = 2;
    if (!check(!LightProbeResourceCodec::validate(invalid), "invalid occupancy rejected"))
    {
        return false;
    }
    invalid = data;
    invalid.coefficients[3] = schema::Vec3(std::numeric_limits<double>::infinity(), 0, 0);
    if (!check(!LightProbeResourceCodec::validate(invalid), "non-finite SH rejected"))
    {
        return false;
    }
    invalid = data;
    invalid.bounds_max = invalid.bounds_min;
    if (!check(!LightProbeResourceCodec::validate(invalid), "degenerate volume rejected"))
    {
        return false;
    }
    test_support::ResourceFixture fixture;
    ResourceSystem resources;
    if (!check(!fixture.root.empty() && resources.initialize({}) && fixture.add(data) &&
                   fixture.publish(resources),
               "resource catalog publication succeeds"))
    {
        return false;
    }
    ResourcePtr loaded;
    if (!check(fixture.load(data.path, loaded) && loaded && loaded->light_probe() &&
                   loaded->light_probe()->coefficients[63].x() == 7.5,
               "typed resource preserves x-fastest probe order and radiance SH"))
    {
        return false;
    }
    namespace io = editor::asset_pipeline;
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishLightProbeDataBuffer(builder, schema::LightProbeData::Pack(builder, &data));
    io::ImportAsset asset;
    asset.guid = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    asset.path = "renamed/probes.lightprobe";
    asset.source = fixture.root / "source.lightprobe";
    asset.kind = schema::AssetKind::light_probe;
    GuidMap identities;
    std::map<Guid, schema::AssetKind> kinds;
    io::ImportContext context{identities,           kinds, fixture.root, fixture.root,
                              fixture.root / "gen", {},    nullptr,      {}};
    io::LightProbeImporter importer;
    std::string error;
    ByteBuffer bytes;
    if (!check(
            io::write_bytes(asset.source,
                            std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())),
                            error) &&
                importer.convert(asset, context, error) &&
                ResourceSystem::read_binary(context.output / asset.guid, bytes),
            "probe source imports"))
    {
        return false;
    }
    const auto imported = schema::UnPackLightProbeData(bytes.data());
    if (!check(imported && imported->path == asset.path && imported->coefficients[63].x() == 7.5,
               "importer changes logical path while preserving SH"))
    {
        return false;
    }
    return check(io::write_bytes(asset.source,
                                 std::as_bytes(std::span(builder.GetBufferPointer(), 8)), error) &&
                     !importer.convert(asset, context, error),
                 "truncated source fails import");
}
} // namespace
int main()
{
    return exercise() ? 0 : 1;
}
