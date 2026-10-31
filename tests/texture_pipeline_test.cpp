#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "ktx_fixture.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/codecs/texture_resource_codec.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/mesh_renderer.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/schema/texture_generated.h"
#include "runtime/utils/path_utils.hpp"
#include "runtime/world/world_system.hpp"

#include <basisu/transcoder/basisu_transcoder.h>
#include <flatbuffers/verifier.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{

using namespace engine;

constexpr const char* material_guid = "b0000000-0000-4000-8000-000000000001";
constexpr const char* mesh_guid = "b0000000-0000-4000-8000-000000000002";
constexpr const char* scene_guid = "b0000000-0000-4000-8000-000000000003";

// Generated 4x4 solid-color images keep tests independent from image tools at runtime.
constexpr unsigned char red_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x08, 0x06, 0x00, 0x00, 0x00, 0xa9,
    0xf1, 0x9e, 0x7e, 0x00, 0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8,
    0xcf, 0xc0, 0xf0, 0x1f, 0x19, 0x33, 0x90, 0x2e, 0x00, 0x00, 0x3c, 0x40, 0x1f, 0xe1, 0xe0,
    0x81, 0x77, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
constexpr unsigned char blue_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x08, 0x06, 0x00, 0x00, 0x00, 0xa9,
    0xf1, 0x9e, 0x7e, 0x00, 0x00, 0x00, 0x11, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60,
    0x60, 0xf8, 0xff, 0x1f, 0x15, 0x93, 0x2c, 0x00, 0x00, 0x1c, 0x60, 0x1f, 0xe1, 0xb6, 0x90,
    0xec, 0xe7, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
constexpr unsigned char jpeg[] = {
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x48,
    0x00, 0x48, 0x00, 0x00, 0xff, 0xe1, 0x00, 0x4c, 0x45, 0x78, 0x69, 0x66, 0x00, 0x00, 0x4d, 0x4d,
    0x00, 0x2a, 0x00, 0x00, 0x00, 0x08, 0x00, 0x01, 0x87, 0x69, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xa0, 0x01, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xa0, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x04, 0xa0, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
    0x00, 0x00, 0xff, 0xed, 0x00, 0x38, 0x50, 0x68, 0x6f, 0x74, 0x6f, 0x73, 0x68, 0x6f, 0x70, 0x20,
    0x33, 0x2e, 0x30, 0x00, 0x38, 0x42, 0x49, 0x4d, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x38, 0x42, 0x49, 0x4d, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0xd4, 0x1d, 0x8c, 0xd9,
    0x8f, 0x00, 0xb2, 0x04, 0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e, 0xff, 0xc0, 0x00, 0x11,
    0x08, 0x00, 0x04, 0x00, 0x04, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01, 0xff,
    0xc4, 0x00, 0x1f, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    0xff, 0xc4, 0x00, 0xb5, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04,
    0x04, 0x00, 0x00, 0x01, 0x7d, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41,
    0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1,
    0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19,
    0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64,
    0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84,
    0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2,
    0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9,
    0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3,
    0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xc4, 0x00, 0x1f, 0x01, 0x00, 0x03, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03,
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x11, 0x00, 0x02, 0x01,
    0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02,
    0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32,
    0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72,
    0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29,
    0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53,
    0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73,
    0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a,
    0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
    0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6,
    0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4,
    0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xff,
    0xdb, 0x00, 0x43, 0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x02, 0x02, 0x03, 0x05, 0x03,
    0x03, 0x03, 0x05, 0x06, 0x05, 0x05, 0x05, 0x05, 0x06, 0x08, 0x06, 0x06, 0x06, 0x06, 0x06, 0x08,
    0x0a, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0c,
    0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
    0x0f, 0x0f, 0x0f, 0x0f, 0xff, 0xdb, 0x00, 0x43, 0x01, 0x02, 0x02, 0x02, 0x04, 0x04, 0x04, 0x07,
    0x04, 0x04, 0x07, 0x10, 0x0b, 0x09, 0x0b, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0xff, 0xdd, 0x00, 0x04, 0x00, 0x01, 0xff,
    0xda, 0x00, 0x0c, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3f, 0x00, 0xf8, 0xbe, 0x8a,
    0x28, 0xaf, 0xe5, 0x33, 0xfd, 0xfc, 0x3f, 0xff, 0xd9};

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "texture pipeline: %s\n", message);
    }
    return condition;
}

bool write(const std::filesystem::path& path, std::string_view content)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    stream.close();
    return static_cast<bool>(stream);
}

template <std::size_t Size>
bool write_image(const std::filesystem::path& path, const unsigned char (&bytes)[Size])
{
    return write(path, {reinterpret_cast<const char*>(bytes), Size});
}

std::string read(const std::filesystem::path& path)
{
    std::string text;
    return ResourceSystem::read_text(path, text) ? text : std::string{};
}

std::string meta(std::string_view guid, std::string_view importer)
{
    return "guid = \"" + std::string(guid) + "\"\nimporter = \"" + std::string(importer) +
           "\"\nmeta_version = 1\n";
}

struct Fixture
{
    std::filesystem::path root;

    bool initialize()
    {
        std::error_code error;
        const auto temporary = std::filesystem::temp_directory_path(error);
        if (error)
        {
            return false;
        }
        root = temporary /
               ("engine-texture-test-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        for (const auto* directory : {"assets", "cache", "settings"})
        {
            std::filesystem::create_directories(root / directory, error);
            if (error)
            {
                return false;
            }
        }
        return true;
    }

    ~Fixture() noexcept
    {
        std::error_code error;
        if (!root.empty())
        {
            std::filesystem::remove_all(root, error);
        }
    }
};

const schema::AssetEntryT* entry(const engine::editor::AssetPipelineSystem& pipeline,
                                 std::string_view path)
{
    for (const auto& resource : pipeline.catalog().entries)
    {
        if (resource->path == path)
        {
            return resource.get();
        }
    }
    return nullptr;
}

bool build(engine::editor::AssetPipelineSystem& pipeline, const Fixture& fixture)
{
    if (!pipeline.build_assets(fixture.root))
    {
        std::fprintf(stderr, "texture pipeline import: %s\n", pipeline.last_error().c_str());
        return false;
    }
    return true;
}

bool texture(const Fixture& fixture, std::string_view guid, schema::TextureDataT& data)
{
    ResourceCatalog catalog;
    ByteBuffer payload;
    if (!ResourceSystem::read_catalog(fixture.root / "cache", catalog) ||
        !ResourceSystem::read_resource(catalog, {{"content", fixture.root / "cache"}}, guid,
                                       payload))
    {
        return false;
    }
    flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                   payload.size());
    if (!schema::VerifyTextureDataBuffer(verifier))
    {
        return false;
    }
    schema::GetTextureData(payload.data())->UnPackTo(&data);
    return true;
}

bool transcode(const schema::TextureDataT& texture, bool red)
{
    basist::basisu_transcoder transcoder;
    const auto size = static_cast<std::uint32_t>(texture.data.size());
    std::array<std::uint8_t, 64> pixels{};
    if (!transcoder.validate_file_checksums(texture.data.data(), size, true) ||
        !transcoder.start_transcoding(texture.data.data(), size) ||
        !transcoder.transcode_image_level(texture.data.data(), size, 0, 0, pixels.data(), 16,
                                          basist::transcoder_texture_format::cTFRGBA32))
    {
        return false;
    }
    return pixels[red ? 0 : 2] > 220 && pixels[red ? 2 : 0] < 30 && pixels[3] == 255;
}

bool shader_dependencies(engine::editor::AssetPipelineSystem& pipeline, const Fixture& fixture)
{
    constexpr const char* shader_guid = "b0000000-0000-4000-8000-000000000010";
    constexpr std::array sources = {"vs_cube.sc",     "fs_cube.sc",        "varying.def.sc",
                                    "pbr_helpers.sh", "shadow_helpers.sh", "material_helpers.sh",
                                    "csm_helpers.sh"};
    constexpr std::array identities = {
        "b0000000-0000-4000-8000-000000000011", "b0000000-0000-4000-8000-000000000012",
        "b0000000-0000-4000-8000-000000000013", "b0000000-0000-4000-8000-000000000014",
        "b0000000-0000-4000-8000-000000000015", "b0000000-0000-4000-8000-000000000016",
        "b0000000-0000-4000-8000-000000000017"};
    const auto upstream = std::filesystem::path(ENGINE_TEST_SOURCE_DIR).parent_path() /
                          "src/engine/runtime/render/shaders";
    for (std::size_t index = 0; index < sources.size(); ++index)
    {
        const auto content = read(upstream / sources[index]);
        auto target = fixture.root / "assets" / sources[index];
        if (content.empty() || !write(target, content))
        {
            return false;
        }
        target += ".meta";
        if (!write(target, meta(identities[index], "source")))
        {
            return false;
        }
    }
    const std::string descriptor =
        std::string("vertex=\"") + identities[0] + "\"\nfragment=\"" + identities[1] +
        "\"\nvarying=\"" + identities[2] +
        "\"\n[[samplers]]\nname=\"baseColorTexture\"\nuniform=\"s_base_color\"\nstage=0\n"
        "default_texture=\"builtin/textures/white-srgb\"\n"
        "[[samplers]]\nname=\"normalTexture\"\nuniform=\"s_normal\"\nstage=1\n"
        "default_texture=\"builtin/textures/flat-normal\"\n"
        "[[samplers]]\nname=\"ormTexture\"\nuniform=\"s_orm\"\nstage=2\n"
        "default_texture=\"builtin/textures/white-linear\"\n";
    const auto path = fixture.root / "assets/custom.shader";
    auto metadata = path;
    metadata += ".meta";
    if (!write(path, descriptor) || !write(metadata, meta(shader_guid, "shader")) ||
        !build(pipeline, fixture))
    {
        return false;
    }
    const auto* shader = entry(pipeline, "project/custom.shader");
    if (!expect(shader && shader->dependencies.size() == 3 &&
                    std::find(shader->dependencies.begin(), shader->dependencies.end(),
                              "00000000-0000-4000-8000-000000000006") !=
                        shader->dependencies.end() &&
                    std::none_of(shader->dependencies.begin(), shader->dependencies.end(),
                                 [&](const auto& guid)
                                 {
                                     return std::find(identities.begin(), identities.end(), guid) !=
                                            identities.end();
                                 }),
                "shader runtime dependencies include defaults and exclude compile-only sources"))
    {
        return false;
    }
    const auto stable = read(fixture.root / "cache/catalog.bin");
    for (const auto* uniform :
         {"u_ambient_occlusion", "s_ambient_occlusion", "u_assao_params", "u_assao_rect",
          "u_hdr_params", "u_hdr_texel", "s_hdr_scene", "s_hdr_bloom"})
    {
        const auto extra = std::string("[[samplers]]\nname=\"reserved\"\nuniform=\"") + uniform +
                           "\"\nstage=3\ndefault_texture=\"builtin/textures/white-linear\"\n";
        if (!expect(write(path, descriptor + extra) && !pipeline.build_assets(fixture.root) &&
                        read(fixture.root / "cache/catalog.bin") == stable,
                    "reserved renderer uniforms fail import without replacing the catalog"))
        {
            return false;
        }
    }
    if (!expect(write(path, descriptor +
                               "[[samplers]]\nname=\"reserved_slot\"\nuniform=\"s_extra\"\n"
                               "stage=11\ndefault_texture=\"builtin/textures/white-linear\"\n") &&
                    !pipeline.build_assets(fixture.root) &&
                    read(fixture.root / "cache/catalog.bin") == stable,
                "AO sampler slot 11 is reserved during import"))
    {
        return false;
    }
    return expect(write(path, descriptor +
                                  "[[samplers]]\nname=\"invalid\"\nuniform=\"s_extra\"\nstage=3\n"
                                  "default_texture=\"project/颜色.png\"\n") &&
                      !pipeline.build_assets(fixture.root) &&
                      read(fixture.root / "cache/catalog.bin") == stable,
                  "shader defaults cannot depend on project textures");
}

bool tests()
{
    Fixture fixture;
    ResourceSystem resources;
    engine::editor::AssetPipelineSystem pipeline;
    EngineConfig config;
    if (!fixture.initialize() || !resources.initialize(config) || !pipeline.initialize(config))
    {
        return false;
    }
    const auto image_path = fixture.root / "assets" / PathUtils::from_utf8("颜色.png");
    auto meta_path = image_path;
    meta_path += ".meta";
    if (!expect(write_image(image_path, red_png) && build(pipeline, fixture),
                "PNG is read through Unicode path and encoded by BasisU"))
    {
        return false;
    }
    const auto* image_entry = entry(pipeline, "project/颜色.png");
    if (!expect(image_entry && image_entry->kind == schema::AssetKind::texture,
                "catalog identifies a texture resource"))
    {
        return false;
    }
    const std::string guid = image_entry->guid;
    const auto generated = fixture.root / "cache/gen" / guid;
    const auto original_meta = read(meta_path);
    schema::TextureDataT data;
    if (!expect(original_meta.find("rdo_quality_scalar") != std::string::npos &&
                    original_meta.find("format = \"auto\"") != std::string::npos &&
                    original_meta.find("srgb = true") != std::string::npos,
                "new adjacent meta records all active default texture settings") ||
        !expect(texture(fixture, guid, data) && data.width == 4 && data.height == 4 &&
                    data.mip_count == 3 && data.srgb &&
                    data.encoding == schema::TextureEncoding::basis && transcode(data, true),
                "GUID cache contains real BasisU data and complete mipmaps") ||
        !expect(build(pipeline, fixture) && pipeline.last_build().imported == 0 &&
                    pipeline.last_build().reused == 1,
                "unchanged texture reuses verified cache"))
    {
        return false;
    }
    const auto material_path = fixture.root / "assets/surface.material";
    const auto mesh_path = fixture.root / "assets/quad.mesh";
    const auto scene_path = fixture.root / "assets/main.scene";
    const std::string material = "shader = \"00000000-0000-4000-8000-000000000001\"\n[[textures]]\n"
                                 "name = \"baseColorTexture\"\ntexture = \"" +
                                 guid + "\"\n";
    const std::string mesh =
        "vertices = [[-1,-1,0,0,0,1],[1,-1,0,0,0,1],[1,1,0,0,0,1],[-1,1,0,0,0,1]]\n"
        "indices = [0,1,2,0,2,3]\ntexcoords = [[0,0],[1,0],[1,1],[0,1]]\n"
        "tangents = [[1,0,0,1],[1,0,0,1],[1,0,0,1],[1,0,0,1]]\n"
        "colors = [[1,1,1,1],[1,1,1,1],[1,1,1,1],[1,1,1,1]]\n"
        "[[submeshes]]\nfirst_index=0\nindex_count=3\nmaterial_slot=0\n"
        "[[submeshes]]\nfirst_index=3\nindex_count=3\nmaterial_slot=1\n";
    const std::string scene =
        std::string("name=\"Textured scene\"\nactive_camera=\"1\"\n") +
        "[[nodes]]\nid=\"1\"\nname=\"Camera\"\n[[nodes.components]]\ntype=\"transform\"\n"
        "[[nodes.components]]\ntype=\"camera\"\nprojection=\"orthographic\"\northographic_size=4\n"
        "[[nodes]]\nid=\"2\"\nname=\"Mesh\"\n[[nodes.components]]\ntype=\"transform\"\n"
        "[[nodes.components]]\ntype=\"mesh_renderer\"\nmesh=\"" +
        mesh_guid + "\"\nmaterial=\"" + material_guid + "\"\nadditional_materials=[\"" +
        material_guid +
        "\"]\n[[nodes]]\nid=\"3\"\nname=\"Spot\"\n[[nodes.components]]\ntype=\"transform\"\n"
        "[[nodes.components]]\ntype=\"light\"\nkind=\"spot\"\nrange=12\ninner_angle=15\nouter_"
        "angle=25\n";
    if (!write(material_path, material) ||
        !write(material_path.string() + ".meta", meta(material_guid, "material")) ||
        !write(mesh_path, mesh) || !write(mesh_path.string() + ".meta", meta(mesh_guid, "mesh")) ||
        !write(scene_path, scene) ||
        !write(scene_path.string() + ".meta", meta(scene_guid, "scene")) ||
        !expect(build(pipeline, fixture) && pipeline.last_build().imported == 3,
                "material, extended mesh and scene import together"))
    {
        return false;
    }
    const auto* material_entry = entry(pipeline, "project/surface.material");
    if (!expect(material_entry && std::find(material_entry->dependencies.begin(),
                                            material_entry->dependencies.end(),
                                            guid) != material_entry->dependencies.end(),
                "material registers texture as a necessary runtime dependency"))
    {
        return false;
    }
    {
        ResourcePtr loaded;
        if (!expect(resources.update_catalog(pipeline.catalog(),
                                             {{"content", fixture.root / "cache"}}) &&
                        resources.load(scene_guid, loaded) && loaded->scene(),
                    "resource core reads the imported scene entity"))
        {
            return false;
        }
        ResourcePtr mesh_resource;
        ResourcePtr material_resource;
        ResourcePtr texture_resource;
        if (!expect(ResourceSystem::find_mesh("project/quad.mesh") == nullptr &&
                        resources.load(mesh_guid, mesh_resource) &&
                        resources.load(material_guid, material_resource) &&
                        resources.load(guid, texture_resource) && material_resource->material() &&
                        texture_resource->texture(),
                    "pipeline outputs load independently without a recursive cache plugin"))
        {
            return false;
        }
        const auto* imported_mesh = mesh_resource->mesh();
        if (!expect(imported_mesh && imported_mesh->texcoords.size() == 4 &&
                        imported_mesh->submeshes.size() == 2,
                    "runtime receives UVs, colors, tangents and material submeshes"))
        {
            return false;
        }
        loaded.reset();
    }
    schema::SceneDataT scene_data;
    schema::SceneDataT roundtrip;
    std::string error;
    if (!expect(engine::editor::SceneImporter::read(scene_path, scene_data, error) &&
                    engine::editor::SceneImporter::write(fixture.root / "settings/roundtrip.scene",
                                                         scene_data, error) &&
                    engine::editor::SceneImporter::read(fixture.root / "settings/roundtrip.scene",
                                                        roundtrip, error) &&
                    scene_data == roundtrip,
                "camera/light/submesh material fields survive scene TOML round trip"))
    {
        return false;
    }
    const std::string linear_meta =
        meta(guid, "texture") +
        "[texture]\nformat=\"rgba8\"\ncompression=\"uastc\"\ncompression_level=2\n"
        "rdo_quality_scalar=0.5\nsrgb=false\nmipmaps=false\n";
    if (!expect(
            write(meta_path, linear_meta) && build(pipeline, fixture) &&
                pipeline.last_build().imported == 3 && pipeline.last_build().reused == 1 &&
                texture(fixture, guid, data) && !data.srgb && data.mip_count == 1 &&
                data.format == schema::TextureFormat::rgba8,
            "texture meta changes reimport dependent material/scene and retain independent mesh") ||
        !expect(write_image(image_path, blue_png) && build(pipeline, fixture) &&
                    pipeline.last_build().imported == 3 && texture(fixture, guid, data) &&
                    transcode(data, false),
                "source edits update BasisU cache and dependents"))
    {
        return false;
    }
    const auto stable_catalog = read(fixture.root / "cache/catalog.bin");
    const auto stable_cache = read(generated);
    auto invalid_scene = scene;
    invalid_scene.replace(invalid_scene.find("range=12"), std::string_view("range=12").size(),
                          "range=-1");
    if (!expect(write(mesh_path,
                      mesh + "[[submeshes]]\nfirst_index=0\nindex_count=3\nmaterial_slot=0\n") &&
                    !pipeline.build_assets(fixture.root) &&
                    read(fixture.root / "cache/catalog.bin") == stable_catalog &&
                    write(mesh_path, mesh),
                "overlapping submesh ranges reject the batch without publishing") ||
        !expect(write(scene_path, invalid_scene) && !pipeline.build_assets(fixture.root) &&
                    read(fixture.root / "cache/catalog.bin") == stable_catalog &&
                    write(scene_path, scene),
                "invalid light settings do not publish a scene"))
    {
        return false;
    }
    for (const auto* invalid :
         {"format=\"invalid\"", "compression=\"invalid\"", "quality=300", "compression_level=5",
          "rdo_quality_scalar=nan", "srgb=3", "mipmaps=\"true\"", "unknown_option=true"})
    {
        if (!expect(write(meta_path, meta(guid, "texture") + "[texture]\n" + invalid + "\n") &&
                        !pipeline.build_assets(fixture.root) &&
                        read(fixture.root / "cache/catalog.bin") == stable_catalog &&
                        read(generated) == stable_cache,
                    "invalid texture meta preserves published catalog and cache"))
        {
            return false;
        }
    }
    if (!expect(write(meta_path, linear_meta) && write(image_path, "broken PNG") &&
                    !pipeline.build_assets(fixture.root) && read(generated) == stable_cache &&
                    write_image(image_path, blue_png),
                "bad image bytes never replace published texture") ||
        !expect(write(material_path, "shader=\"00000000-0000-4000-8000-000000000001\"\n"
                                     "[[textures]]\nname=\"baseColorTexture\"\ntexture=\"b0000000-"
                                     "0000-4000-8000-000000000099\"\n") &&
                    !pipeline.build_assets(fixture.root) && write(material_path, material),
                "missing texture GUID prevents publication"))
    {
        return false;
    }
    const std::string etc_meta = meta(guid, "texture") +
                                 "[texture]\nformat=\"etc2\"\ncompression=\"etc1s\"\ncompression_"
                                 "level=2\nquality=128\nsrgb=true\nmipmaps=true\n";
    if (!expect(write(meta_path, etc_meta) && build(pipeline, fixture) &&
                    texture(fixture, guid, data) && data.mip_count == 3 &&
                    data.format == schema::TextureFormat::etc2 && transcode(data, false),
                "ETC1S uses upstream quality/effort settings and produces valid BasisU data"))
    {
        return false;
    }
    const auto jpeg_path = fixture.root / "assets/picture.jpeg";
    if (!expect(write_image(jpeg_path, jpeg) && build(pipeline, fixture) &&
                    pipeline.last_build().imported == 1,
                "JPEG independently imports through the memory decoder"))
    {
        return false;
    }
    const auto* jpeg_entry = entry(pipeline, "project/picture.jpeg");
    return expect(jpeg_entry && texture(fixture, jpeg_entry->guid, data) && data.width == 4 &&
                      data.height == 4 && transcode(data, true),
                  "JPEG cache contains transcodable texture pixels") &&
           shader_dependencies(pipeline, fixture);
}

bool ktx_tests()
{
    Fixture fixture;
    engine::editor::AssetPipelineSystem pipeline;
    ResourceSystem resources;
    const auto cube_bytes = test_support::ktx_image(true, true, 3);
    const auto lut_bytes = test_support::ktx_image(false, false, 1);
    if (!expect(fixture.initialize() && pipeline.initialize({}) && resources.initialize({}) &&
                    !cube_bytes.empty() && !lut_bytes.empty(),
                "create HDR KTX import fixture") ||
        !expect(write(fixture.root / "assets/reflection.ktx", cube_bytes) &&
                    write(fixture.root / "assets/brdf.ktx", lut_bytes) && build(pipeline, fixture),
                "upstream bimg KTX files import with source-specific default metadata"))
    {
        return false;
    }
    const auto* cube_entry = entry(pipeline, "project/reflection.ktx");
    const auto* lut_entry = entry(pipeline, "project/brdf.ktx");
    if (!expect(cube_entry != nullptr && lut_entry != nullptr,
                "KTX sources receive ordinary independent texture GUIDs"))
    {
        return false;
    }
    const auto cube_guid = cube_entry->guid;
    const auto lut_guid = lut_entry->guid;
    const auto meta_path = fixture.root / "assets/reflection.ktx.meta";
    const auto default_meta = read(meta_path);
    ResourcePtr cube;
    ResourcePtr lut;
    if (!expect(default_meta.find("texture_ktx") != std::string::npos &&
                    default_meta.find("srgb = false") != std::string::npos &&
                    resources.update_catalog(pipeline.catalog(),
                                             {{"content", fixture.root / "cache"}}) &&
                    resources.load(cube_guid, cube) && resources.load(lut_guid, lut) &&
                    cube->texture() != nullptr && lut->texture() != nullptr,
                "load HDR KTX resources through the runtime codec"))
    {
        return false;
    }
    const auto& data = *cube->texture();
    const auto& lut_data = *lut->texture();
    if (!expect(
            data.encoding == schema::TextureEncoding::ktx && data.cube_map &&
                data.format == schema::TextureFormat::rgba16f && !data.srgb && data.width == 4 &&
                data.height == 4 && data.mip_count == 3 &&
                std::string(reinterpret_cast<const char*>(data.data.data()), data.data.size()) ==
                    cube_bytes &&
                !lut_data.cube_map && lut_data.format == schema::TextureFormat::rg16f &&
                lut_data.mip_count == 1 && !lut_data.srgb,
            "retain all HDR faces, roughness mips and BRDF channel format without recompression") ||
        !expect(build(pipeline, fixture) && pipeline.last_build().imported == 0 &&
                    pipeline.last_build().reused == 2,
                "unchanged HDR textures reuse standard GUID artifacts"))
    {
        return false;
    }
    WorldSystem world;
    if (!expect(world.initialize({}), "initialize ordinary scene for material sampler checks"))
    {
        return false;
    }
    auto* scene = world.create_scene("Texture dimensions");
    auto* node = scene ? scene->create_node("Mesh") : nullptr;
    auto* mesh = node ? node->add_component<MeshRenderer>() : nullptr;
    if (!expect(mesh != nullptr, "create material through ordinary MeshRenderer component"))
    {
        return false;
    }
    auto& material = mesh->material();
    std::string_view bound;
    if (!expect(material.get_texture("baseColorTexture", bound), "read default 2D binding"))
    {
        return false;
    }
    const std::string original_binding(bound);
    if (!expect(!material.set_texture("baseColorTexture", "project/reflection.ktx") &&
                    material.get_texture("baseColorTexture", bound) && bound == original_binding,
                "ordinary 2D material sampler rejects a cubemap without changing its binding") ||
        !expect(material.set_texture("baseColorTexture", "project/brdf.ktx"),
                "ordinary material accepts valid 2D HDR KTX"))
    {
        return false;
    }
    const std::string definition =
        "shader = \"00000000-0000-4000-8000-000000000001\"\n[[textures]]\n"
        "name = \"baseColorTexture\"\ntexture = \"" +
        cube_guid + "\"\n";
    ResourcePtr invalid_material;
    ResourcePtr shader;
    ResourcePtr cube_dependency;
    if (!expect(write(fixture.root / "assets/cube.material", definition) &&
                    write(fixture.root / "assets/cube.material.meta",
                          meta(material_guid, "material")) &&
                    build(pipeline, fixture) &&
                    resources.update_catalog(pipeline.catalog(),
                                             {{"content", fixture.root / "cache"}}) &&
                    resources.load(material_guid, invalid_material) &&
                    resources.load(invalid_material->material()->shader_guid, shader) &&
                    resources.load(cube_guid, cube_dependency),
                "load authored material and direct dependencies independently"))
    {
        return false;
    }
    const std::array dependencies{shader, cube_dependency};
    if (!expect(!resources.validate_dependencies(*invalid_material, dependencies) &&
                    !material.set_type("project/cube.material") &&
                    material.get_texture("baseColorTexture", bound) && bound == "project/brdf.ktx",
                "material dependency validation and type replacement reject cubemap samplers"))
    {
        return false;
    }
    world.shutdown();
    auto invalid = data;
    invalid.cube_map = false;
    if (!expect(!TextureResourceCodec::validate(invalid), "reject mismatched cubemap metadata"))
    {
        return false;
    }
    invalid = data;
    invalid.srgb = true;
    if (!expect(!TextureResourceCodec::validate(invalid), "reject sRGB half-float KTX"))
    {
        return false;
    }
    invalid = data;
    invalid.format = schema::TextureFormat::rg16f;
    if (!expect(!TextureResourceCodec::validate(invalid), "reject mismatched HDR format"))
    {
        return false;
    }
    for (const auto offset : {std::size_t{48}, std::size_t{52}, std::size_t{56}, std::size_t{64}})
    {
        invalid = data;
        invalid.data[offset] = 2;
        if (!expect(!TextureResourceCodec::validate(invalid),
                    "reject illegal array count, face count, mip count or face size"))
        {
            return false;
        }
    }
    invalid = data;
    invalid.data.pop_back();
    if (!expect(!TextureResourceCodec::validate(invalid), "reject truncated final face/mip"))
    {
        return false;
    }
    invalid = data;
    invalid.data.push_back(0);
    if (!expect(!TextureResourceCodec::validate(invalid),
                "reject trailing bytes outside KTX image"))
    {
        return false;
    }
    const auto stable_catalog = read(fixture.root / "cache/catalog.bin");
    const auto stable_texture = read(fixture.root / "cache/gen" / cube_guid);
    for (const auto* setting :
         {"compression=\"uastc\"\nsrgb=false", "compression=\"none\"\nsrgb=true",
          "compression=\"none\"\nsrgb=0", "compression=\"none\"\nsrgb=false\nformat=\"rg16f\"",
          "compression=\"none\"\nsrgb=false\nmipmaps=false"})
    {
        if (!expect(
                write(meta_path, meta(cube_guid, "texture_ktx") + "[texture]\n" + setting + "\n") &&
                    !pipeline.build_assets(fixture.root) &&
                    read(fixture.root / "cache/catalog.bin") == stable_catalog &&
                    read(fixture.root / "cache/gen" / cube_guid) == stable_texture,
                "invalid KTX meta preserves the published catalog and texture"))
        {
            return false;
        }
    }
    return expect(write(meta_path, default_meta), "restore KTX meta");
}

} // namespace

int main()
{
    return tests() && ktx_tests() ? 0 : 1;
}
