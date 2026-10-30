#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "ktx_fixture.hpp"
#include "plugins/editor/font_import/font_importer.hpp"
#include "plugins/editor/ui_import/ui_importer.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/resource/resource_system.hpp"

#include <algorithm>
#include <chrono>
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
namespace fs = std::filesystem;

bool expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::fprintf(stderr, "UI pipeline: %.*s\n", static_cast<int>(message.size()),
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
    return engine::ResourceSystem::read_text(path, text) ? text : std::string{};
}

std::string metadata(std::string_view guid, std::string_view settings)
{
    return "guid = \"" + std::string(guid) + "\"\nimporter = \"ui\"\nmeta_version = 1\n[ui]\n" +
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
        root /= "engine-ui-pipeline-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for (const char* directory : {"assets", "settings", "cache"})
        {
            fs::create_directories(root / directory, error);
            if (error)
            {
                return false;
            }
        }
        return true;
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

bool texture_dimensions(const Project& project, engine::editor::AssetPipelineSystem& pipeline,
                        engine::ResourceSystem& resources, std::string_view ui_guid)
{
    const auto hdr = test_support::ktx_image(false, true, 1);
    const auto cube = test_support::ktx_image(true, true, 3);
    if (!expect(!hdr.empty() && !cube.empty() && write(project.root / "assets/hdr.ktx", hdr) &&
                    write(project.root / "assets/cube.ktx", cube),
                "create valid 2D HDR and cube texture fixtures"))
    {
        return false;
    }
    for (const auto* texture :
         {"builtin/textures/white-srgb", "project/hdr.ktx", "project/cube.ktx"})
    {
        const std::string markup =
            std::string("<rml><body><img src=\"") + texture + "\"/></body></rml>";
        const auto settings = metadata(
            ui_guid, std::string("textures = [\"") + texture +
                         "\"]\n[[ui.fonts]]\nfamily = \"body\"\nasset = \"project/lato.ttf\"\n");
        engine::ResourcePtr document;
        if (!expect(write(project.root / "assets/menu.rml", markup) &&
                        write(project.root / "assets/menu.rml.meta", settings) &&
                        pipeline.build_assets(project.root),
                    pipeline.last_error()) ||
            !expect(resources.update_catalog(pipeline.catalog(),
                                             {{"content", project.root / "cache"}}) &&
                        resources.load(ui_guid, document),
                    resources.last_error()))
        {
            return false;
        }
        std::vector<engine::ResourcePtr> dependencies;
        for (const auto& guid : document->info().dependencies)
        {
            engine::ResourcePtr resource;
            if (!expect(resources.load(guid, resource), resources.last_error()))
            {
                return false;
            }
            dependencies.push_back(std::move(resource));
        }
        const bool ldr = std::string_view(texture).starts_with("builtin/");
        if (!expect(resources.validate_dependencies(*document, dependencies) == ldr,
                    "UI accepts 2D LDR and rejects HDR KTX/cubemap dependencies"))
        {
            return false;
        }
    }
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    using namespace engine;
    constexpr std::string_view ui_guid = "7b158bd1-83a1-46b8-b69a-f001bfda083f";
    constexpr std::string_view font_guid = "4d736348-022b-4d44-b53d-de3ccf16eeb9";
    constexpr std::string_view markup =
        "<rml><head><title>Menu</title><style>body {font-family: Lato; font-size: 20px;}"
        "button:hover {color: #f80;}</style></head>"
        "<body><h1>Scene menu</h1><button id=\"courtyard\">Courtyard</button></body></rml>";
    Project project;
    if (!expect(argc == 2, "provide source TTF path") ||
        !expect(project.initialize(), "create project"))
    {
        return 1;
    }
    const auto source = read(argv[1]);
    const auto path = project.root / "assets/menu.rml";
    const auto meta = project.root / "assets/menu.rml.meta";
    const auto valid_meta = metadata(
        ui_guid, "textures = []\n[[ui.fonts]]\nfamily = \"Lato\"\nasset = \"project/lato.ttf\"\n"
                 "[[ui.fonts]]\nfamily = \"caption\"\nasset = \"project/lato.ttf\"\n");
    editor::AssetPipelineSystem pipeline;
    ResourceSystem resources;
    std::unique_ptr<editor::asset_pipeline::AssetImporter> font_importer =
        std::make_unique<plugins::font_import::FontImporter>();
    std::unique_ptr<editor::asset_pipeline::AssetImporter> ui_importer =
        std::make_unique<plugins::ui_import::UiImporter>();
    if (!expect(!source.empty() && write(project.root / "assets/lato.ttf", source),
                "copy font fixture") ||
        !expect(write(project.root / "assets/lato.ttf.meta",
                      "guid = \"" + std::string(font_guid) +
                          "\"\nimporter = \"font\"\nmeta_version = 1\n"),
                "write font identity") ||
        !expect(write(path, markup) && write(meta, valid_meta), "write UI source") ||
        !expect(pipeline.initialize({}), "initialize pipeline") ||
        !expect(pipeline.add_importer(font_importer) && pipeline.add_importer(ui_importer),
                "register optional font and UI import plugins") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 2,
                "font and UI import as independent resources") ||
        !expect(resources.initialize({}), "initialize resource core") ||
        !expect(resources.update_catalog(pipeline.catalog(), {{"content", project.root / "cache"}}),
                resources.last_error()))
    {
        return 1;
    }
    ResourcePtr original;
    Guid shader_guid;
    if (!expect(resources.load(ui_guid, original), resources.last_error()) ||
        !expect(original->ui() != nullptr && original->ui()->markup == markup &&
                    original->ui()->fonts.size() == 2 &&
                    original->ui()->fonts.front()->font_guid == font_guid &&
                    original->ui()->fonts.front()->family == "lato" &&
                    original->ui()->fonts.back()->family == "caption" &&
                    original->ui()->fonts.front()->shader_path == "builtin/shaders/font",
                "UI preserves aliases, normalizes families and resolves shared font GUID") ||
        !expect(resources.catalog().identities().path_to_guid("builtin/shaders/font", shader_guid),
                "builtin font shader is catalogued") ||
        !expect(original->info().dependencies.size() == 2 &&
                    std::find(original->info().dependencies.begin(),
                              original->info().dependencies.end(),
                              shader_guid) != original->info().dependencies.end(),
                "UI deduplicates shared font and shader dependencies across bindings") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 0 && pipeline.last_build().reused == 2,
                "unchanged UI and font reuse cache"))
    {
        return 1;
    }
    const auto changed = std::string(markup) + "\n<!-- authored menu revision -->";
    if (!expect(write(path, changed), "edit UI markup") ||
        !expect(pipeline.build_assets(project.root), pipeline.last_error()) ||
        !expect(pipeline.last_build().imported == 1 && pipeline.last_build().reused == 1,
                "UI edit reimports only its resource"))
    {
        return 1;
    }
    const auto revision = pipeline.last_build().revision;
    const auto published = read(project.root / "cache/gen" / std::string(ui_guid));
    const auto rejects = [&](std::string_view text, std::string_view settings, const char* label)
    {
        return expect(write(path, text) && write(meta, settings), "write invalid UI fixture") &&
               expect(!pipeline.build_assets(project.root), label) &&
               expect(pipeline.last_build().revision == revision &&
                          read(project.root / "cache/gen" / std::string(ui_guid)) == published,
                      "invalid UI preserves the published resource version");
    };
    for (const char* invalid :
         {"fonts = []\n", "fonts = \"project/lato.ttf\"\n", "fonts = [\"project/missing.ttf\"]\n",
          "fonts = [\"builtin/shaders/font\"]\n",
          "fonts = [\"project/lato.ttf\", \"project/lato.ttf\"]\n",
          "fonts = [{family = \"body\", asset = \"project/lato.ttf\", shader = false}]\n",
          "fonts = [{family = \"body\", asset = \"project/lato.ttf\", shader = "
          "\"project/lato.ttf\"}]\n",
          "fonts = [{family = \"body\", asset = \"project/missing.ttf\"}]\n",
          "fonts = [{family = \"body\", asset = \"project/lato.ttf\", unknown = 1}]\n",
          "fonts = [{family = \"Body\", asset = \"project/lato.ttf\"}, {family = \"body\", asset = "
          "\"project/lato.ttf\"}]\n",
          "fonts = [{family = \"\", asset = \"project/lato.ttf\"}]\n",
          "fonts = [{asset = \"project/lato.ttf\"}]\n", "fonts = [{family = \"body\"}]\n",
          "fonts = [{family = \"body\", asset = \"project/lato.ttf\"}]\nunknown = 1\n"})
    {
        if (!rejects(markup, metadata(ui_guid, invalid), "reject invalid UI metadata"))
        {
            return 1;
        }
    }
    for (const char* invalid :
         {"<rml><head><link href=\"other.rcss\"/></head><body/></rml>",
          "<rml><body><script>alert(1)</script></body></rml>",
          "<rml><body><img src=\"project/missing.png\"/></body></rml>",
          "<rml><head><style>@import 'other.rcss';</style></head><body/></rml>",
          "<rml><body style=\"decorator: image(texture.png);\"/></rml>",
          "<rml><body><progress style=\"fill-image: project/unknown.png;\"/></body></rml>",
          "<rml><head><style>@spritesheet icons {src: project/icons.png;}</style></head>"
          "<body/></rml>",
          "<rml><body template=\"other\"/></rml>", "<rml><body id=unquoted/></rml>",
          "<rml><body/><!-- unterminated", "plain text without a document"})
    {
        if (!rejects(invalid, valid_meta, "reject unsupported or invalid UI resource references"))
        {
            return 1;
        }
    }
    if (!expect(original->ui()->markup == markup, "loaded UI reference remains immutable"))
    {
        return 1;
    }
    if (!texture_dimensions(project, pipeline, resources, ui_guid))
    {
        return 1;
    }
    original.reset();
    resources.shutdown();
    pipeline.shutdown();
    return 0;
}
