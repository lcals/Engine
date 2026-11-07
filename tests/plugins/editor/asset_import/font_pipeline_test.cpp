#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "file_text.hpp"
#include "plugins/editor/asset_import/font_importer.hpp"
#include "resource_fixture.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/codecs/font_resource_codec.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/task/task_system.hpp"
#include "runtime/task/thread_system.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
namespace fs = std::filesystem;

bool expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::fprintf(stderr, "font pipeline: %.*s\n", static_cast<int>(message.size()),
                     message.data());
    }
    return condition;
}

bool write(const fs::path& path, std::string_view text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    return static_cast<bool>(stream);
}

std::string read(const fs::path& path)
{
    std::string text;
    return test_support::read_text(path, text) ? text : std::string{};
}

std::string metadata(std::string_view guid, std::string_view settings)
{
    return "guid = \"" + std::string(guid) + "\"\nimporter = \"font\"\nmeta_version = 1\n[font]\n" +
           std::string(settings);
}

struct Project
{
    fs::path root;

    bool initialize()
    {
        std::error_code error;
        root = fs::temp_directory_path(error);
        if (error)
        {
            return false;
        }
        root /= "engine-font-pipeline-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for (const char* directory : {"assets", "settings", "cache"})
        {
            fs::create_directories(root / directory, error);
            if (error)
            {
                return false;
            }
        }
        return write(root / "settings/editor-import.toml", "[common]\nimport_threads = 8\n");
    }

    ~Project()
    {
        if (!root.empty())
        {
            std::error_code error;
            fs::remove_all(root, error);
        }
    }
};
} // namespace

int main(int argc, char** argv)
{
    using namespace engine;
    Project project;
    if (!expect(argc == 2, "provide the source TTF path") ||
        !expect(project.initialize(), "create project"))
    {
        return 1;
    }
    const auto source = read(argv[1]);
    const auto path = project.root / "assets/test.ttf";
    const auto meta = project.root / "assets/test.ttf.meta";
    engine::ThreadSystem threads;
    engine::TaskSystem tasks(threads, {"Editor", 4, engine::TaskPriority::low});
    editor::AssetPipelineSystem pipeline(tasks);
    ResourceSystem resources;
    std::unique_ptr<editor::asset_pipeline::AssetImporter> importer =
        std::make_unique<plugins::asset_import::FontImporter>();
    if (!expect(!source.empty() && write(path, source), "copy fixture font") ||
        !expect(threads.initialize({}) && tasks.initialize({}) && pipeline.initialize({}),
                "initialize pipeline") ||
        !expect(pipeline.add_importer(importer), "register font import plugin") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 1, "font source generates one resource") ||
        !expect(test_support::register_resource_codecs(resources) && resources.initialize({}),
                "initialize resource core") ||
        !expect(resources.update_catalog(pipeline.catalog(), {{"content", project.root / "cache"}}),
                std::string(resources.last_error())))
    {
        return 1;
    }
    Guid guid;
    std::unique_ptr<Resource> original;
    if (!expect(resources.catalog().identities().path_to_guid("test.ttf", guid),
                "source font has a GUID") ||
        !expect(read(meta).find("[font]") != std::string::npos, "font meta defaults generated") ||
        !expect(resources.read(guid, original), std::string(resources.last_error())) ||
        !expect(original->info().kind == schema::AssetKind::font &&
                    engine::resource_detail::data<engine::FontResource>(original.get()) != nullptr,
                "core decodes standalone font data") ||
        !expect(
            engine::resource_detail::data<engine::FontResource>(original.get())->family == "Lato" &&
                engine::resource_detail::data<engine::FontResource>(original.get())->units_per_em ==
                    2000 &&
                engine::resource_detail::data<engine::FontResource>(original.get())->glyphs.size() >
                    95 &&
                !engine::resource_detail::data<engine::FontResource>(original.get())
                     ->curves.empty(),
            "font has normalized outlines and Unicode glyphs") ||
        !expect(engine::resource_detail::data<engine::FontResource>(original.get())->ascender >
                        0.5F &&
                    engine::resource_detail::data<engine::FontResource>(original.get())->ascender <
                        2.0F,
                "font metrics use em units") ||
        !expect(original->info().dependencies.empty(), "font has no runtime parser dependencies") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 0 && pipeline.last_build().reused == 1,
                "unchanged source and meta reuse the generated font"))
    {
        return 1;
    }
    const auto find_glyph = [&](std::uint32_t codepoint) -> const schema::FontGlyphT*
    {
        const auto found = std::find_if(
            engine::resource_detail::data<engine::FontResource>(original.get())->glyphs.begin(),
            engine::resource_detail::data<engine::FontResource>(original.get())->glyphs.end(),
            [codepoint](const auto& glyph) { return glyph->codepoint == codepoint; });
        return found != engine::resource_detail::data<engine::FontResource>(original.get())
                            ->glyphs.end()
                   ? found->get()
                   : nullptr;
    };
    const auto* space = find_glyph(0x20);
    const auto* accented = find_glyph(0xe9);
    if (!expect(space != nullptr && space->curve_count == 0 && space->advance_x > 0.0F,
                "space has layout advance without drawable curves") ||
        !expect(accented != nullptr && accented->curve_count > 0 && accented->max_y < 2.0F,
                "composite TrueType accent is decomposed into normalized curves"))
    {
        return 1;
    }
    const auto old_revision = pipeline.last_build().revision;
    const auto changed_meta = metadata(guid, "unicode_ranges = [\"0020-007E\"]\nkerning = false\n");
    std::unique_ptr<Resource> changed;
    if (!expect(write(meta, changed_meta), "update font import settings") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 1 &&
                    pipeline.last_build().revision != old_revision,
                "font meta change regenerates the same GUID resource") ||
        !expect(resources.update_catalog(pipeline.catalog(), {{"content", project.root / "cache"}}),
                std::string(resources.last_error())) ||
        !expect(resources.read(guid, changed), std::string(resources.last_error())) ||
        !expect(
            engine::resource_detail::data<engine::FontResource>(changed.get())->glyphs.size() ==
                    96 &&
                engine::resource_detail::data<engine::FontResource>(changed.get())->kerning.empty(),
            "ASCII subset plus missing glyph respects kerning toggle") ||
        !expect(
            engine::resource_detail::data<engine::FontResource>(original.get())->glyphs.size() >
                engine::resource_detail::data<engine::FontResource>(changed.get())->glyphs.size(),
            "old font reference remains immutable"))
    {
        return 1;
    }
    const auto revision = pipeline.last_build().revision;
    const auto published = read(project.root / "cache/gen" / guid);
    const auto rejects = [&](std::string_view data, std::string_view meta_text, const char* label)
    {
        return expect(write(path, data) && write(meta, meta_text), "write invalid font fixture") &&
               expect(!pipeline.build_assets(project.root), label) &&
               expect(pipeline.last_build().revision == revision &&
                          read(project.root / "cache/gen" / guid) == published,
                      "font import failure preserves published cache and revision");
    };
    for (const char* invalid :
         {"face_index = \"0\"\n", "face_index = -1\n", "face_index = 255\n", "kerning = \"true\"\n",
          "unicode_ranges = []\n", "unicode_ranges = \"ASCII\"\n",
          "unicode_ranges = [\"0000-007E\"]\n", "unicode_ranges = [\"D800-DFFF\"]\n",
          "unicode_ranges = [\"007E-0020\"]\n", "unicode_ranges = [\"0001-CFFF\"]\n",
          "unicode_ranges = [\"0020-007E\", \"0070-0080\"]\n", "size = 24\n"})
    {
        if (!rejects(source, metadata(guid, invalid), "reject invalid font metadata"))
        {
            return 1;
        }
    }
    if (!rejects(source.substr(0, 32), changed_meta, "reject truncated font") ||
        !rejects("not a font", changed_meta, "reject invalid font file"))
    {
        return 1;
    }
    auto invalid = *engine::resource_detail::data<engine::FontResource>(changed.get());
    invalid.glyphs[1]->curve_count = std::numeric_limits<std::uint32_t>::max();
    if (!expect(!FontResourceCodec::validate(invalid), "core rejects out-of-bounds glyph curves"))
    {
        return 1;
    }
    invalid = *engine::resource_detail::data<engine::FontResource>(changed.get());
    invalid.glyphs[1]->codepoint = 0;
    if (!expect(!FontResourceCodec::validate(invalid), "core rejects duplicate glyph codepoints"))
    {
        return 1;
    }
    invalid = *engine::resource_detail::data<engine::FontResource>(changed.get());
    invalid.ascender = std::numeric_limits<float>::quiet_NaN();
    if (!expect(!FontResourceCodec::validate(invalid), "core rejects nonfinite font metrics"))
    {
        return 1;
    }
    invalid = *engine::resource_detail::data<engine::FontResource>(changed.get());
    invalid.kerning.emplace_back(0x110000, 0x41, -0.1F);
    if (!expect(!FontResourceCodec::validate(invalid), "core rejects unknown kerning codepoint"))
    {
        return 1;
    }
    changed.reset();
    original.reset();
    resources.shutdown();
    pipeline.shutdown();
    return 0;
}
