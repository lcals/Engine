#include "file_text.hpp"
#include "resource_fixture.hpp"
#include "runtime/resource/virtual_file_system.hpp"

#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/animation_tools/retarget_importer.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace
{
namespace io = engine::editor::asset_pipeline;
namespace schema = engine::schema;
namespace fs = std::filesystem;
constexpr const char* source_guid = "10000000-0000-4000-8000-000000000001";
constexpr const char* target_guid = "10000000-0000-4000-8000-000000000002";

bool expect(bool value, const char* message, const std::string& error = {})
{
    if (!value)
    {
        std::fprintf(stderr, "retarget importer: %s: %s\n", message, error.c_str());
    }
    return value;
}

bool write(const fs::path& path, std::string_view text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    return static_cast<bool>(file);
}

std::string read(const fs::path& path)
{
    std::string result;
    return test_support::read_text(path, result) ? result : std::string{};
}

// Public, self-authored skeleton input keeps this importer test independent of FBX assets.
class FixtureSkeletonImporter final : public io::AssetImporter
{
public:
    std::string_view id() const noexcept override
    {
        return "fixture_skeleton";
    }
    schema::AssetKind kind() const noexcept override
    {
        return schema::AssetKind::skeleton;
    }
    bool matches(const fs::path& source) const override
    {
        return source.extension() == ".skeleton";
    }
    bool convert(const io::ImportAsset& asset, const io::ImportContext& context,
                 std::string&) const override
    {
        engine::ByteBuffer bytes;
        return engine::VirtualFileSystem::read_file(asset.source, bytes) &&
               write(context.output / asset.guid,
                     {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    }
};

bool skeleton_source(const fs::path& root, const char* filename, const char* guid, const char* name)
{
    schema::SkeletonDataT data;
    data.path = filename;
    auto joint = std::make_unique<schema::JointDataT>();
    joint->name = name;
    joint->inverse_bind = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    data.joints.push_back(std::move(joint));
    flatbuffers::FlatBufferBuilder builder;
    schema::FinishSkeletonDataBuffer(builder, schema::SkeletonData::Pack(builder, &data));
    return write(root / "assets" / filename,
                 {reinterpret_cast<const char*>(builder.GetBufferPointer()), builder.GetSize()}) &&
           write(root / "assets" / (std::string(filename) + ".meta"),
                 "guid = \"" + std::string(guid) +
                     "\"\nimporter = \"fixture_skeleton\"\nmeta_version = 1\n");
}

std::string descriptor(double scale)
{
    return "source_skeleton = \"" + std::string(source_guid) +
           "\"\n"
           "target_skeleton = \"" +
           target_guid +
           "\"\n"
           "hips_translation_mode = \"scaled_source_delta\"\n"
           "source_hips_path = \"Source\"\ntarget_hips_path = \"Target\"\n"
           "hips_translation_scale = " +
           std::to_string(scale) +
           "\n"
           "[[joint_mappings]]\nsource_joint_path = \"Source\"\ntarget_joint_path = \"Target\"\n"
           "[[source_reference_pose]]\ntranslation = [0,0,0]\nrotation = [0,0,0,1]\nscale = "
           "[1,1,1]\n"
           "[[target_reference_pose]]\ntranslation = [0,0,0]\nrotation = [0,0,0,1]\nscale = "
           "[1,1,1]\n";
}

bool exercise()
{
    test_support::ResourceFixture fixture;
    std::error_code code;
    for (const auto* directory : {"assets", "settings", "cache"})
    {
        fs::create_directories(fixture.root / directory, code);
        if (code)
        {
            return false;
        }
    }
    if (!write(fixture.root / "settings/editor-import.toml", "[common]\nimport_threads = 8\n"))
    {
        return false;
    }
    engine::ThreadSystem threads;
    engine::TaskSystem tasks(threads);
    engine::ResourceSystem resources;
    engine::editor::AssetPipelineSystem pipeline(tasks);
    std::unique_ptr<io::AssetImporter> skeleton = std::make_unique<FixtureSkeletonImporter>();
    std::unique_ptr<io::AssetImporter> retarget =
        std::make_unique<engine::plugins::animation_tools::RetargetImporter>();
    const auto source = fixture.root / "assets/test.retarget";
    if (!expect(threads.initialize({}) && tasks.initialize({}) &&
                    test_support::register_resource_codecs(resources) && resources.initialize({}) &&
                    pipeline.initialize({}) && pipeline.add_importer(skeleton) &&
                    pipeline.add_importer(retarget) &&
                    skeleton_source(fixture.root, "source.skeleton", source_guid, "Source") &&
                    skeleton_source(fixture.root, "target.skeleton", target_guid, "Target") &&
                    write(source, descriptor(1)) && pipeline.build_assets(fixture.root),
                "build retarget profile through the real resource pipeline", pipeline.last_error()))
    {
        return false;
    }
    const auto entry =
        std::find_if(pipeline.catalog().entries.begin(), pipeline.catalog().entries.end(),
                     [](const auto& item) { return item && item->path == "test.retarget"; });
    if (!expect(entry != pipeline.catalog().entries.end() &&
                    (*entry)->kind == schema::AssetKind::animation_retarget_profile &&
                    (*entry)->dependencies == std::vector<std::string>{source_guid, target_guid},
                "the profile has exactly its two explicit skeleton GUID dependencies"))
    {
        return false;
    }
    const auto guid = (*entry)->guid;
    const auto metadata = read(source.string() + ".meta");
    std::unique_ptr<engine::Resource> original;
    if (!expect(
            metadata.find("animation_retarget_profile") != std::string::npos &&
                metadata.find(guid) != std::string::npos &&
                resources.update_catalog(pipeline.catalog(),
                                         {{"content", fixture.root / "cache"}}) &&
                resources.read(guid, original) && original &&
                engine::resource_detail::data<engine::AnimationRetargetResource>(original.get()) &&
                engine::resource_detail::data<engine::AnimationRetargetResource>(original.get())
                        ->source_reference_pose.size() == 1 &&
                engine::resource_detail::data<engine::AnimationRetargetResource>(original.get())
                        ->hips_translation_scale == 1 &&
                pipeline.build_assets(fixture.root) && pipeline.last_build().imported == 0 &&
                pipeline.last_build().reused == 3,
            "unique meta and GUID entity load; unchanged profiles use incremental cache",
            pipeline.last_error()))
    {
        return false;
    }
    if (!expect(
            write(source, descriptor(2)) && pipeline.build_assets(fixture.root) &&
                pipeline.last_build().imported == 1 && pipeline.last_build().reused == 2 &&
                read(source.string() + ".meta") == metadata &&
                resources.update_catalog(pipeline.catalog(), {{"content", fixture.root / "cache"}}),
            "profile edits regenerate only the dependent resource and retain its GUID",
            pipeline.last_error()))
    {
        return false;
    }
    std::unique_ptr<engine::Resource> changed;
    if (!expect(
            resources.read(guid, changed) && changed &&
                engine::resource_detail::data<engine::AnimationRetargetResource>(changed.get()) &&
                engine::resource_detail::data<engine::AnimationRetargetResource>(changed.get())
                        ->hips_translation_scale == 2 &&
                engine::resource_detail::data<engine::AnimationRetargetResource>(original.get())
                        ->hips_translation_scale == 1,
            "a new resource generation never mutates an older immutable profile"))
    {
        return false;
    }
    const auto catalog = read(fixture.root / "cache/catalog.bin");
    const auto entity = read(fixture.root / "cache/gen" / guid);
    const auto valid = descriptor(2);
    auto unknown_joint = valid;
    const auto source_joint = unknown_joint.find("source_joint_path = \"Source\"");
    unknown_joint.replace(source_joint, std::string_view{"source_joint_path = \"Source\""}.size(),
                          "source_joint_path = \"Source/Missing\"");
    const auto mismatched_reference = valid + "[[source_reference_pose]]\ntranslation = [0,0,0]\n"
                                              "rotation = [0,0,0,1]\nscale = [1,1,1]\n";
    for (const auto& invalid :
         {std::string{"source_skeleton = 5\n"},
          valid + "[[joint_mappings]]\nsource_joint_path=\"Other\"\ntarget_joint_path=\"Target\"\n",
          std::string{"unknown_field = true\n"} + valid, unknown_joint, mismatched_reference})
    {
        if (!expect(write(source, invalid) && !pipeline.build_assets(fixture.root) &&
                        !pipeline.last_error().empty() &&
                        read(fixture.root / "cache/catalog.bin") == catalog &&
                        read(fixture.root / "cache/gen" / guid) == entity,
                    "invalid descriptor preserves the published catalog and complete resource"))
        {
            return false;
        }
    }
    return expect(write(source, valid) && pipeline.build_assets(fixture.root),
                  "valid source restores import after validation failure", pipeline.last_error());
}
} // namespace

int main()
{
    return exercise() ? 0 : 1;
}
