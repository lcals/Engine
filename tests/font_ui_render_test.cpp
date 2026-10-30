#include "editor/asset_pipeline/asset_pipeline_system.hpp"
#include "plugins/editor/font_import/font_importer.hpp"
#include "plugins/editor/ui_import/ui_importer.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/font/font_system.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/text_component.hpp"
#include "runtime/scene/ui_component.hpp"
#include "runtime/ui/ui_system.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <chrono>
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
namespace fs = std::filesystem;
constexpr double delta_time = 1.0 / 60.0;
constexpr int maximum_attempts = 200;
constexpr std::string_view custom_shader = "project/shaders/font-custom.shader";
constexpr std::string_view red_texture_guid = "d7b89356-7bf5-48dd-b22a-8eb869387657";
constexpr std::string_view blue_texture_guid = "9b3bdf74-8e8c-4c7a-a537-6bd26686f71b";

// Same generated 4x4 solid-color fixtures as the existing texture pipeline test.
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
    0xec, 0xe7, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

bool expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::fprintf(stderr, "font/UI GPU: %.*s (SDL: %s)\n", static_cast<int>(message.size()),
                     message.data(), SDL_GetError());
    }
    return condition;
}

bool write_text(const fs::path& path, std::string_view text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    return static_cast<bool>(stream);
}

bool write_meta(const fs::path& path, std::string_view guid, std::string_view importer)
{
    auto meta = path;
    meta += ".meta";
    return write_text(meta, "guid = \"" + std::string(guid) + "\"\nimporter = \"" +
                                std::string(importer) + "\"\nmeta_version = 1\n");
}

bool prepare_custom_shader(const fs::path& directory)
{
    constexpr std::array names = {"vs_font.sc", "fs_font.sc", "font_varying.def.sc"};
    constexpr std::array guids = {"59bef84c-a37d-4c1e-9dcb-f707a50e62bf",
                                  "20317145-d307-49df-b3db-62f52cdb4605",
                                  "abfb881a-7719-4a74-9c1c-22d3f81a48f6"};
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        std::string source;
        if (!engine::ResourceSystem::read_text(fs::path(ENGINE_FONT_TEST_SHADER_DIR) / names[index],
                                               source))
        {
            return false;
        }
        if (index == 1)
        {
            const auto end = source.rfind('}');
            if (end == std::string::npos)
            {
                return false;
            }
            // Keep the public reference license and rendering contract; visibly tint only text.
            source.insert(end, "    gl_FragColor.rgb *= vec3(0.35, 1.0, 0.65);\n");
        }
        if (!write_text(directory / names[index], source) ||
            !write_meta(directory / names[index], guids[index], "source"))
        {
            return false;
        }
    }
    const auto descriptor = directory / "font-custom.shader";
    return write_text(descriptor, "vertex = \"" + std::string(guids[0]) + "\"\nfragment = \"" +
                                      guids[1] + "\"\nvarying = \"" + guids[2] + "\"\n") &&
           write_meta(descriptor, "6b0f12c8-86f8-4d02-bd17-9e767e2f4826", "shader");
}

bool prepare_images(const fs::path& project)
{
    const auto directory = project / "assets/textures";
    if (!write_text(directory / "red.png",
                    {reinterpret_cast<const char*>(red_png), sizeof(red_png)}) ||
        !write_text(directory / "blue.png",
                    {reinterpret_cast<const char*>(blue_png), sizeof(blue_png)}))
    {
        return false;
    }
    constexpr std::array<std::string_view, 2> guids = {red_texture_guid, blue_texture_guid};
    constexpr std::array names = {"red.png", "blue.png"};
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        auto meta = directory / names[index];
        meta += ".meta";
        if (!write_text(meta, "guid = \"" + std::string(guids[index]) +
                                  "\"\nimporter = \"texture\"\nmeta_version = 1\n"
                                  "[texture]\nformat = \"rgba8\"\ncompression = \"uastc\"\n"
                                  "compression_level = 0\nmipmaps = false\n"))
        {
            return false;
        }
    }
    const auto meta = project / "assets/ui/launcher.rml.meta";
    const auto markup = project / "assets/ui/launcher.rml";
    std::string settings;
    std::string document;
    if (!engine::ResourceSystem::read_text(meta, settings) ||
        !engine::ResourceSystem::read_text(markup, document))
    {
        return false;
    }
    const auto at = settings.find("textures = []");
    const auto body = document.find("</body>");
    if (at == std::string::npos || body == std::string::npos)
    {
        return false;
    }
    settings.replace(at, std::string_view("textures = []").size(),
                     "textures = [\"project/textures/red.png\", \"project/textures/blue.png\"]");
    document.insert(body,
                    "<div style=\"display: block; position: absolute; right: 8px; top: 8px;\">"
                    "<img src=\"project/textures/red.png\" width=\"24\" height=\"24\"/>"
                    "<img src=\"" +
                        std::string(blue_texture_guid) + "\" width=\"24\" height=\"24\"/></div>\n");
    return write_text(meta, settings) && write_text(markup, document);
}

bool prepare_multiple_documents(const fs::path& project)
{
    constexpr std::array guids = {"3a5d5211-31d4-4b63-81f1-c9daaa018fce",
                                  "3f976ec5-9f1c-4fac-a31e-fb38ce5789f0"};
    constexpr std::array names = {"lower", "upper"};
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        const auto path = project / "assets/ui" / (std::string(names[index]) + ".rml");
        const auto left = index == 0 ? "40px" : "540px";
        const auto markup =
            "<rml><head><title>Independent UI document</title><style>"
            "body {display: block; position: absolute; margin: 0px; left: " +
            std::string(left) +
            "; top: 80px; width: 360px; height: 180px; background-color: #172433; "
            "font-family: shared; font-size: 24px; color: #ffffff;}"
            "p {display: block; position: absolute; left: 20px; top: 12px; margin: 0px;}"
            "input {display: block; position: absolute; left: 20px; top: 80px; width: 280px; "
            "height: 36px; background-color: #34475d; color: #ffffff; font-size: 22px;}"
            "</style></head><body><p>" +
            (index == 0 ? "Regular / built-in shader" : "Bold / project shader") +
            "</p><input type=\"text\" id=\"" + names[index] + "-input\"/></body></rml>";
        const auto metadata =
            "guid = \"" + std::string(guids[index]) +
            "\"\nimporter = \"ui\"\nmeta_version = 1\n[ui]\ntextures = []\n"
            "[[ui.fonts]]\nfamily = \"shared\"\nasset = \"project/fonts/" +
            (index == 0 ? "lato-regular.ttf" : "lato-bold.ttf") + "\"\nshader = \"" +
            (index == 0 ? "builtin/shaders/font" : std::string(custom_shader)) + "\"\n";
        auto meta = path;
        meta += ".meta";
        if (!write_text(path, markup) || !write_text(meta, metadata))
        {
            return false;
        }
    }
    const auto scene = project / "assets/scenes/multiple-documents.scene";
    const auto data = "name = \"Multiple UI documents\"\nactive_camera = 0\n"
                      "[[nodes]]\nid = 1\nname = \"Lower UI\"\nparent = 0\n"
                      "[[nodes.components]]\ntype = \"transform\"\n"
                      "position = [0.0, 0.0, 0.0]\nrotation = [0.0, 0.0, 0.0]\n"
                      "scale = [1.0, 1.0, 1.0]\n"
                      "[[nodes.components]]\ntype = \"ui\"\ndocument = \"" +
                      std::string(guids[0]) +
                      "\"\n[[nodes]]\nid = 2\nname = \"Upper UI\"\nparent = 0\n"
                      "[[nodes.components]]\ntype = \"transform\"\n"
                      "position = [0.0, 0.0, 0.0]\nrotation = [0.0, 0.0, 0.0]\n"
                      "scale = [1.0, 1.0, 1.0]\n"
                      "[[nodes.components]]\ntype = \"ui\"\ndocument = \"" +
                      guids[1] + "\"\n";
    return write_text(scene, data) &&
           write_meta(scene, "16b180bd-dd98-4d12-9abf-40a8cc87eef0", "scene");
}

struct Project
{
    fs::path root;

    bool initialize(const fs::path& source)
    {
        std::error_code error;
        root = fs::temp_directory_path(error);
        if (error)
        {
            return false;
        }
        root /= "engine-font-ui-gpu-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for (const auto* directory : {"settings", "cache", "assets/fonts", "assets/ui",
                                      "assets/scenes", "assets/shaders", "assets/textures"})
        {
            fs::create_directories(root / directory, error);
            if (error)
            {
                return false;
            }
        }
        for (const auto* file :
             {"fonts/lato-regular.ttf", "fonts/lato-regular.ttf.meta", "fonts/lato-bold.ttf",
              "fonts/lato-bold.ttf.meta", "ui/launcher.rml", "ui/launcher.rml.meta",
              "scenes/launcher.scene", "scenes/launcher.scene.meta", "scenes/font-gallery.scene",
              "scenes/font-gallery.scene.meta"})
        {
            if (!fs::copy_file(source / "assets" / file, root / "assets" / file,
                               fs::copy_options::none, error) ||
                error)
            {
                std::fprintf(stderr, "fixture copy: %s\n", file);
                return false;
            }
        }
        return prepare_custom_shader(root / "assets/shaders") && prepare_images(root) &&
               prepare_multiple_documents(root);
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

struct GpuCounts
{
    std::uint16_t vertices = 0;
    std::uint16_t indices = 0;
    std::uint16_t shaders = 0;
    std::uint16_t programs = 0;
    std::uint16_t uniforms = 0;
    std::uint16_t textures = 0;

    bool operator==(const GpuCounts&) const = default;
};

GpuCounts gpu_counts()
{
    const auto& stats = *bgfx::getStats();
    return {stats.numVertexBuffers, stats.numIndexBuffers, stats.numShaders,
            stats.numPrograms,      stats.numUniforms,     stats.numTextures};
}

struct Systems
{
    engine::ResourceSystem resources;
    engine::editor::AssetPipelineSystem pipeline;
    engine::WindowSystem window;
    engine::WorldSystem world;
    engine::FontSystem fonts;
    engine::UiSystem ui{window, world, fonts};
    engine::RenderSystem renderer{window, &world, &fonts, &ui};

    ~Systems()
    {
        // GPU consumers close before bgfx; resources and the native window outlive them.
        ui.shutdown();
        fonts.shutdown();
        renderer.shutdown();
        world.shutdown();
        window.shutdown();
        resources.shutdown();
        pipeline.shutdown();
    }

    bool initialize(const fs::path& project)
    {
        engine::EngineConfig config;
        config.window_title = "Engine font and UI integration test";
        config.width = 1024;
        config.height = 768;
        std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> font =
            std::make_unique<engine::plugins::font_import::FontImporter>();
        std::unique_ptr<engine::editor::asset_pipeline::AssetImporter> document =
            std::make_unique<engine::plugins::ui_import::UiImporter>();
        return expect(pipeline.initialize(config), "initialize asset pipeline") &&
               expect(pipeline.add_importer(font) && pipeline.add_importer(document),
                      "register font and UI importer plugins") &&
               expect(pipeline.build_assets(project), pipeline.last_error()) &&
               expect(resources.initialize(config), "initialize resource core") &&
               expect(
                   resources.update_catalog(pipeline.catalog(), {{"content", project / "cache"}}),
                   resources.last_error()) &&
               expect(window.initialize(config), "initialize native window") &&
               expect(world.initialize(config), "initialize scene world") &&
               expect(fonts.initialize(config), "initialize independent font system") &&
               expect(ui.initialize(config), "initialize RmlUi integration") &&
               expect(renderer.initialize(config), "initialize real rendering backend");
    }

    bool frames(int count = 8)
    {
        int rendered = 0;
        for (int attempt = 0; attempt < maximum_attempts && rendered < count; ++attempt)
        {
            if (!expect(window.tick(delta_time), "poll SDL events") ||
                !expect(!window.exit_requested(), "test window remains open"))
            {
                return false;
            }
            if (window.can_render())
            {
                if (!expect(renderer.render(), "render a real frame with font/UI consumers"))
                {
                    return false;
                }
                ++rendered;
            }
            else
            {
                SDL_Delay(10);
            }
        }
        return expect(rendered == count, "window became drawable before timeout");
    }

    void stop_scene()
    {
        ui.release_scene();
        fonts.release_scene();
        world.destroy_scene();
        renderer.release_scene();
    }

    bool screenshot(const fs::path& directory, const char* name)
    {
        std::error_code error;
        fs::create_directories(directory, error);
        const auto path = directory / name;
        if (!expect(!error, "create screenshot destination") ||
            !expect(renderer.request_screenshot(engine::PathUtils::to_utf8(path)),
                    "request GPU screenshot"))
        {
            return false;
        }
        for (int attempt = 0; attempt < maximum_attempts; ++attempt)
        {
            if (!frames(1))
            {
                return false;
            }
            if (renderer.screenshot_status() != engine::ScreenshotStatus::pending)
            {
                break;
            }
        }
        engine::ByteBuffer bytes;
        constexpr std::array<std::byte, 8> png = {std::byte{137}, std::byte{'P'}, std::byte{'N'},
                                                  std::byte{'G'}, std::byte{13},  std::byte{10},
                                                  std::byte{26},  std::byte{10}};
        return expect(renderer.screenshot_status() == engine::ScreenshotStatus::complete,
                      "GPU screenshot completed") &&
               expect(engine::ResourceSystem::read_binary(path, bytes) && bytes.size() > 128 &&
                          std::equal(png.begin(), png.end(), bytes.begin()),
                      "GPU callback wrote a PNG image");
    }
};

class TrackedLoader final : public engine::ResourceLoader
{
public:
    explicit TrackedLoader(engine::ResourceSystem& resources) : resources_(resources)
    {
        resources_.set_loader(this);
    }
    ~TrackedLoader() noexcept override
    {
        resources_.set_loader(nullptr);
    }
    bool load(std::string_view guid, engine::ResourcePtr& output) override
    {
        if (!resources_.load(guid, output))
        {
            return false;
        }
        loaded_.push_back(output);
        return true;
    }
    engine::ResourcePtr find(std::string_view guid) const noexcept override
    {
        return resources_.find(guid);
    }
    const std::string& last_error() const noexcept override
    {
        return resources_.last_error();
    }
    std::size_t live(std::string_view guid, std::uint64_t generation) const
    {
        return std::count_if(loaded_.begin(), loaded_.end(),
                             [&](const auto& weak)
                             {
                                 const auto resource = weak.lock();
                                 return resource && resource->info().guid == guid &&
                                        resource->generation() == generation;
                             });
    }

private:
    engine::ResourceSystem& resources_;
    std::vector<std::weak_ptr<const engine::Resource>> loaded_;
};

bool document_generations(Systems& systems, const fs::path& project, const GpuCounts& idle)
{
    TrackedLoader tracked(systems.resources);
    auto* scene = systems.world.load_scene("project/scenes/launcher.scene");
    if (!expect(scene != nullptr, "load the first view for document generation isolation"))
    {
        return false;
    }
    auto* original = scene->nodes().front()->get_component<engine::UiComponent>();
    auto* stable_node = scene->create_node("Stable view");
    auto* stable = stable_node ? stable_node->add_component<engine::UiComponent>() : nullptr;
    engine::UiElementHandle stable_button;
    if (!expect(original && stable && stable->set_document("project/ui/launcher.rml"),
                "two independent views share a document generation") ||
        !systems.frames() ||
        !expect(systems.ui.find_element(stable_node->id(), "scene-cube", stable_button) &&
                    systems.ui.set_text(stable_button, "Persistent View"),
                "store independent state and a handle in the unchanged view"))
    {
        return false;
    }
    const auto old_generation = systems.resources.generation();
    const auto font_guid = original->document()->ui()->fonts.front()->font_guid;
    const auto before = gpu_counts();
    if (!expect(tracked.live(red_texture_guid, old_generation) > 0 &&
                    tracked.live(font_guid, old_generation) > 0,
                "the visible document owns its image and font snapshot"))
    {
        return false;
    }
    std::string shader_source;
    const auto fragment = project / "assets/shaders/fs_font.sc";
    if (!expect(engine::ResourceSystem::read_text(fragment, shader_source),
                "read the authored font shader for a same-GUID revision"))
    {
        return false;
    }
    const auto tint = shader_source.find("vec3(0.35, 1.0, 0.65)");
    if (!expect(tint != std::string::npos, "the project shader has a distinct authored tint"))
    {
        return false;
    }
    shader_source.replace(tint, std::string_view("vec3(0.35, 1.0, 0.65)").size(),
                          "vec3(1.0, 0.45, 0.20)");
    std::error_code error;
    fs::copy_file(project / "assets/fonts/lato-bold.ttf", project / "assets/fonts/lato-regular.ttf",
                  fs::copy_options::overwrite_existing, error);
    if (!expect(!error && write_text(fragment, shader_source) &&
                    write_text(project / "assets/textures/red.png",
                               {reinterpret_cast<const char*>(blue_png), sizeof(blue_png)}),
                "revise font, image and shader without changing their GUIDs") ||
        !expect(systems.pipeline.build_assets(project), systems.pipeline.last_error()) ||
        !expect(systems.resources.update_catalog(systems.pipeline.catalog(),
                                                 {{"content", project / "cache"}}),
                systems.resources.last_error()) ||
        !expect(original->set_document("") && original->set_document("project/ui/launcher.rml"),
                "replace one view with the newly published document generation") ||
        !systems.frames())
    {
        return false;
    }
    std::string text;
    const auto after = gpu_counts();
    const auto new_generation = systems.resources.generation();
    if (!expect(new_generation != old_generation &&
                    original->document()->generation() == new_generation &&
                    stable->document()->generation() == old_generation &&
                    systems.ui.is_valid(stable_button) && systems.ui.text(stable_button, text) &&
                    text == "Persistent View",
                "the unchanged view retains its old document, handle and edited text") ||
        !expect(after.textures >= before.textures + 4 && after.programs >= before.programs + 1,
                "new image, font curve and shader versions create independent GPU resources") ||
        !expect(tracked.live(red_texture_guid, old_generation) > 0 &&
                    tracked.live(red_texture_guid, new_generation) > 0 &&
                    tracked.live(font_guid, old_generation) > 0 &&
                    tracked.live(font_guid, new_generation) > 0,
                "both live document generations retain only their own resource snapshots"))
    {
        return false;
    }
    if (!expect(scene->destroy_node(stable_node->id()),
                "remove the last view of the old version") ||
        !systems.frames() ||
        !expect(!systems.ui.is_valid(stable_button) &&
                    tracked.live(red_texture_guid, old_generation) == 0 &&
                    tracked.live(font_guid, old_generation) == 0 &&
                    tracked.live(red_texture_guid, new_generation) > 0 &&
                    tracked.live(font_guid, new_generation) > 0,
                "removing one generation releases its UI font and texture references only"))
    {
        return false;
    }
    original->set_enabled(false);
    if (!systems.frames() ||
        !expect(systems.ui.document_count() == 0 &&
                    tracked.live(red_texture_guid, new_generation) == 0 &&
                    tracked.live(font_guid, new_generation) == 0,
                "the last disabled view releases all UI-owned font and image snapshots"))
    {
        return false;
    }
    systems.stop_scene();
    return systems.frames() &&
           expect(gpu_counts() == idle, "scene stop retires both shader versions");
}

bool keyboard_launcher(Systems& systems, std::uint64_t node_id)
{
    engine::UiEvent event;
    while (systems.ui.poll_event(event))
    {
    }
    const auto window_id = SDL_GetWindowID(systems.window.native_window());
    for (const auto key : {SDLK_TAB, SDLK_RETURN})
    {
        SDL_Event input{};
        input.type = SDL_EVENT_KEY_DOWN;
        input.key.windowID = window_id;
        input.key.key = key;
        input.key.down = true;
        static_cast<void>(systems.ui.process_event(input));
        input.type = SDL_EVENT_KEY_UP;
        input.key.down = false;
        static_cast<void>(systems.ui.process_event(input));
    }
    while (systems.ui.poll_event(event))
    {
        if (event.type == "click")
        {
            return expect(event.node_id == node_id && event.element_id == "scene-cube",
                          "initial Tab and Enter activate the first launcher button");
        }
    }
    return expect(false, "keyboard navigation must publish the first launcher button click");
}

bool click_launcher(Systems& systems, std::uint64_t node_id)
{
    const auto window_id = SDL_GetWindowID(systems.window.native_window());
    int width = 0;
    int height = 0;
    if (!expect(SDL_GetWindowSize(systems.window.native_window(), &width, &height),
                "query logical UI size"))
    {
        return false;
    }
    // Exercise public SDL input and UI events without exposing RmlUi's document internals.
    // Search the authored responsive menu for a real button; labels and scene nodes stay intact.
    for (int y = 220; y < height - 20; y += 24)
    {
        for (int x = 72; x < width - 40; x += 48)
        {
            SDL_Event motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.motion.windowID = window_id;
            motion.motion.x = static_cast<float>(x);
            motion.motion.y = static_cast<float>(y);
            static_cast<void>(systems.ui.process_event(motion));
            SDL_Event press{};
            press.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            press.button.windowID = window_id;
            press.button.button = SDL_BUTTON_LEFT;
            press.button.down = true;
            press.button.clicks = 1;
            press.button.x = static_cast<float>(x);
            press.button.y = static_cast<float>(y);
            static_cast<void>(systems.ui.process_event(press));
            press.type = SDL_EVENT_MOUSE_BUTTON_UP;
            press.button.down = false;
            static_cast<void>(systems.ui.process_event(press));
            engine::UiEvent event;
            while (systems.ui.poll_event(event))
            {
                if (event.type == "click" && event.element_id.starts_with("scene-"))
                {
                    std::printf("UI button event: %s\n", event.element_id.c_str());
                    return expect(event.node_id == node_id,
                                  "button event identifies its ordinary scene node");
                }
            }
        }
    }
    return expect(false, "SDL mouse input must produce a launcher button click");
}

bool custom_shader_scenes(Systems& systems, const fs::path& project, const fs::path& screenshots,
                          const GpuCounts& idle)
{
    const auto meta = project / "assets/ui/launcher.rml.meta";
    std::string text;
    if (!expect(engine::ResourceSystem::read_text(meta, text), "read temporary UI metadata"))
    {
        return false;
    }
    const auto markup_path = project / "assets/ui/launcher.rml";
    std::string markup;
    if (!expect(engine::ResourceSystem::read_text(markup_path, markup), "read temporary UI markup"))
    {
        return false;
    }
    const auto end = markup.find("</body>");
    if (!expect(end != std::string::npos, "fixture contains a UI body"))
    {
        return false;
    }
    text += "\n[[ui.fonts]]\nfamily = \"accent\"\nasset = \"project/fonts/lato-regular.ttf\"\n"
            "shader = \"" +
            std::string(custom_shader) + "\"\n";
    markup.insert(end, "<div style=\"display: block; position: absolute; left: 56px; bottom: 18px; "
                       "font-family: accent; font-size: 22px; color: #ffffff;\">"
                       "Project shader / same font resource</div>\n");
    if (!expect(write_text(meta, text) && write_text(markup_path, markup),
                "bind the same font through a separate family and project shader") ||
        !expect(systems.pipeline.build_assets(project), systems.pipeline.last_error()) ||
        !expect(systems.resources.update_catalog(systems.pipeline.catalog(),
                                                 {{"content", project / "cache"}}),
                systems.resources.last_error()))
    {
        return false;
    }
    engine::Guid guid;
    engine::ResourcePtr shader;
    if (!expect(engine::ResourceSystem::path_to_guid(custom_shader, guid) &&
                    systems.resources.load(guid, shader) && shader->shader() != nullptr,
                "project shader is compiled and loaded through the normal resource pipeline") ||
        !expect(shader->shader()->fragment_code !=
                    engine::ResourceSystem::find_shader("builtin/shaders/font")->fragment_code,
                "project font fragment shader contains visibly different code"))
    {
        return false;
    }
    auto* scene = systems.world.load_scene("project/scenes/launcher.scene");
    if (!expect(scene != nullptr, "load UI with project font shader"))
    {
        return false;
    }
    const auto& bindings =
        scene->nodes().front()->get_component<engine::UiComponent>()->document()->ui()->fonts;
    if (!expect(bindings.size() == 3 && bindings[0]->family == "body" &&
                    bindings[1]->family == "heading" && bindings[2]->family == "accent" &&
                    bindings[0]->font_guid == bindings[2]->font_guid &&
                    bindings[0]->font_guid != bindings[1]->font_guid &&
                    bindings[0]->shader_path == "builtin/shaders/font" &&
                    bindings[2]->shader_path == custom_shader,
                "one UI combines two font resources and two independently bound shaders") ||
        !systems.frames() || !systems.screenshot(screenshots, "launcher-custom-shader.png"))
    {
        return false;
    }
    systems.stop_scene();
    if (!systems.frames() || !expect(gpu_counts() == idle, "custom UI shader retires after stop"))
    {
        return false;
    }
    scene = systems.world.load_scene("project/scenes/font-gallery.scene");
    if (!expect(scene != nullptr, "load 3D font gallery for project shader"))
    {
        return false;
    }
    for (const auto& node : scene->nodes())
    {
        if (auto* component = node->get_component<engine::TextComponent>())
        {
            auto settings = component->settings();
            settings.shader_path = custom_shader;
            if (!expect(component->set_settings(settings), "3D text accepts a project font shader"))
            {
                return false;
            }
        }
    }
    if (!systems.frames() || !systems.screenshot(screenshots, "font-gallery-custom-shader.png"))
    {
        return false;
    }
    shader.reset();
    systems.stop_scene();
    return systems.frames() &&
           expect(systems.fonts.gpu_resource_count() == 0 && gpu_counts() == idle,
                  "project font shader GPU resources are released after UI and 3D use");
}

bool click_at(Systems& systems, float x, float y)
{
    const auto window_id = SDL_GetWindowID(systems.window.native_window());
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = window_id;
    motion.motion.x = x;
    motion.motion.y = y;
    static_cast<void>(systems.ui.process_event(motion));
    SDL_Event click{};
    click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    click.button.windowID = window_id;
    click.button.button = SDL_BUTTON_LEFT;
    click.button.down = true;
    click.button.clicks = 1;
    click.button.x = x;
    click.button.y = y;
    const bool pressed = systems.ui.process_event(click);
    click.type = SDL_EVENT_MOUSE_BUTTON_UP;
    click.button.down = false;
    const bool released = systems.ui.process_event(click);
    return expect(pressed && released, "visible input consumes the pointer click") &&
           expect(SDL_TextInputActive(systems.window.native_window()),
                  "focused input enables native text entry");
}

bool type_in_document(Systems& systems, const char* text, std::uint64_t node_id,
                      std::string_view element)
{
    engine::UiEvent event;
    while (systems.ui.poll_event(event))
    {
    }
    SDL_Event input{};
    input.type = SDL_EVENT_TEXT_INPUT;
    input.text.windowID = SDL_GetWindowID(systems.window.native_window());
    input.text.text = text;
    if (!expect(systems.ui.process_event(input), "focused input consumes native text entry"))
    {
        return false;
    }
    std::size_t changes = 0;
    while (systems.ui.poll_event(event))
    {
        if (event.type == "change")
        {
            ++changes;
            if (!expect(event.node_id == node_id && event.element_id == element,
                        "only the focused document receives the text change"))
            {
                return false;
            }
        }
    }
    return expect(changes > 0, "text entry publishes a change from the requested UI node");
}

bool retain_pointer_capture(Systems& systems, std::uint64_t node_id)
{
    const auto window_id = SDL_GetWindowID(systems.window.native_window());
    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.windowID = window_id;
    button.button.button = SDL_BUTTON_LEFT;
    button.button.down = true;
    button.button.clicks = 1;
    button.button.x = 100.0f;
    button.button.y = 180.0f;
    if (!expect(systems.ui.process_event(button), "left input captures the first held button"))
    {
        return false;
    }
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = window_id;
    motion.motion.state = SDL_BUTTON_LMASK;
    motion.motion.x = 600.0f;
    motion.motion.y = 180.0f;
    static_cast<void>(systems.ui.process_event(motion));
    button.button.button = SDL_BUTTON_RIGHT;
    button.button.x = motion.motion.x;
    button.button.y = motion.motion.y;
    static_cast<void>(systems.ui.process_event(button));
    button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    button.button.down = false;
    static_cast<void>(systems.ui.process_event(button));
    button.button.button = SDL_BUTTON_LEFT;
    static_cast<void>(systems.ui.process_event(button));
    return type_in_document(systems, "Captured", node_id, "lower-input");
}

bool multiple_documents(Systems& systems, const fs::path& screenshots, const GpuCounts& idle)
{
    auto* scene = systems.world.load_scene("project/scenes/multiple-documents.scene");
    if (!expect(scene != nullptr && scene->nodes().size() == 2,
                "two independent documents belong to two ordinary nodes") ||
        !systems.frames() ||
        !expect(systems.ui.document_count() == 2, "both UI components create documents"))
    {
        return false;
    }
    const auto& lower =
        scene->nodes()[0]->get_component<engine::UiComponent>()->document()->ui()->fonts.front();
    const auto& upper =
        scene->nodes()[1]->get_component<engine::UiComponent>()->document()->ui()->fonts.front();
    if (!expect(lower->family == upper->family && lower->font_guid != upper->font_guid &&
                    lower->shader_path != upper->shader_path,
                "the same family alias has separate font and shader bindings per document") ||
        !click_at(systems, 600.0f, 180.0f) ||
        !type_in_document(systems, "Upper", scene->nodes()[1]->id(), "upper-input") ||
        !click_at(systems, 100.0f, 180.0f) ||
        !type_in_document(systems, "Lower", scene->nodes()[0]->id(), "lower-input") ||
        !systems.frames() || !systems.screenshot(screenshots, "multiple-documents.png") ||
        !retain_pointer_capture(systems, scene->nodes()[0]->id()) || !systems.frames())
    {
        return false;
    }
    systems.stop_scene();
    return systems.frames() &&
           expect(systems.ui.document_count() == 0 && systems.fonts.gpu_resource_count() == 0 &&
                      gpu_counts() == idle,
                  "independent documents release all font, shader and input resources");
}

bool run(Systems& systems, const fs::path& project, const fs::path& screenshots)
{
    if (!systems.frames() || !expect(bgfx::getRendererType() != bgfx::RendererType::Noop,
                                     "font/UI tests require a real GPU backend"))
    {
        return false;
    }
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
    if (!expect(bgfx::getRendererType() == bgfx::RendererType::Metal, "Apple uses Metal"))
#elif defined(SDL_PLATFORM_WINDOWS)
    if (!expect(bgfx::getRendererType() == bgfx::RendererType::Direct3D12, "Windows uses DX12"))
#else
    if (!expect(bgfx::getRendererType() == bgfx::RendererType::Vulkan, "platform uses Vulkan"))
#endif
    {
        return false;
    }
    const auto idle = gpu_counts();
    const auto idle_draws = bgfx::getStats()->numDraw;
    SDL_Window* const original_window = systems.window.native_window();
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        auto* scene = systems.world.load_scene("project/scenes/launcher.scene");
        if (!expect(scene != nullptr && scene->nodes().size() == 1,
                    "launcher is one ordinary Node; controls do not expand the scene") ||
            !expect(scene->active_camera() == nullptr, "UI-only scene needs no hidden camera"))
        {
            return false;
        }
        auto* component = scene->nodes().front()->get_component<engine::UiComponent>();
        if (!expect(component != nullptr && component->document() != nullptr,
                    "normal Node owns UiComponent and a generated UI resource") ||
            !systems.frames() ||
            !expect(systems.ui.document_count() == 1 && systems.fonts.gpu_resource_count() > 0,
                    "RmlUi document uses the independent font system") ||
            !expect(bgfx::getStats()->numDraw > idle_draws,
                    "UI submits actual background, control and text GPU draws") ||
            (cycle == 0 && !keyboard_launcher(systems, scene->nodes().front()->id())) ||
            !click_launcher(systems, scene->nodes().front()->id()) ||
            !expect(systems.world.scene() == scene && scene->nodes().size() == 1,
                    "button emits an event without implementing scene navigation"))
        {
            return false;
        }
        if (cycle == 0 && !systems.screenshot(screenshots, "launcher.png"))
        {
            return false;
        }
        component->set_enabled(false);
        if (!systems.frames() ||
            !expect(systems.ui.document_count() == 0, "disabled UI component closes its document"))
        {
            return false;
        }
        component->set_enabled(true);
        if (!systems.frames() || !expect(systems.ui.document_count() == 1,
                                         "reenabled UI component rebuilds its document"))
        {
            return false;
        }
        if (cycle == 0)
        {
            if (!expect(SDL_SetWindowSize(original_window, 640, 600), "resize to narrow layout") ||
                !systems.frames(12) || !systems.screenshot(screenshots, "launcher-narrow.png") ||
                !expect(SDL_SetWindowSize(original_window, 1024, 768), "restore desktop layout") ||
                !systems.frames(12))
            {
                return false;
            }
        }
        systems.stop_scene();
        systems.stop_scene();
        if (!expect(systems.fonts.gpu_resource_count() == 0 && systems.ui.document_count() == 0 &&
                        systems.world.scene() == nullptr,
                    "stopping releases UI documents and shared font GPU resources") ||
            !systems.frames() ||
            !expect(gpu_counts() == idle && bgfx::getStats()->numDraw == idle_draws,
                    "stop retires GPU resources and restores the blank frame"))
        {
            return false;
        }
        scene = systems.world.load_scene("project/scenes/font-gallery.scene");
        if (!expect(scene != nullptr && scene->nodes().size() == 5,
                    "3D gallery contains a camera and four ordinary text nodes") ||
            !expect(scene->nodes()[1]->get_component<engine::TextComponent>() != nullptr,
                    "3D text is a component") ||
            !systems.frames() ||
            !expect(systems.ui.document_count() == 0 && systems.fonts.gpu_resource_count() > 0,
                    "3D text uses FontSystem without a RmlUi document") ||
            !expect(bgfx::getStats()->numDraw >= idle_draws + 4,
                    "four 3D text components submit visible GPU geometry"))
        {
            return false;
        }
        if (cycle == 0 && !systems.screenshot(screenshots, "font-gallery.png"))
        {
            return false;
        }
        systems.stop_scene();
        if (!systems.frames() ||
            !expect(systems.fonts.gpu_resource_count() == 0 && gpu_counts() == idle &&
                        bgfx::getStats()->numDraw == idle_draws &&
                        systems.window.native_window() == original_window,
                    "repeated UI and 3D scene cycles restore the same idle window"))
        {
            return false;
        }
    }
    return custom_shader_scenes(systems, project, screenshots, idle) &&
           multiple_documents(systems, screenshots, idle) &&
           document_generations(systems, project, idle) &&
           systems.screenshot(screenshots, "stopped.png");
}
} // namespace

int main(int argc, char** argv)
{
    if (!expect(argc == 2 || argc == 3,
                "provide showcase project and optional screenshot directory"))
    {
        return 1;
    }
    Project project;
    if (!expect(project.initialize(engine::PathUtils::from_utf8(argv[1])),
                "create isolated UI/font project from authored fixtures"))
    {
        return 1;
    }
    std::error_code error;
    const auto screenshots = argc == 3 ? fs::absolute(engine::PathUtils::from_utf8(argv[2]), error)
                                       : project.root / "screenshots";
    if (!expect(!error, "resolve screenshot destination"))
    {
        return 1;
    }
    Systems systems;
    if (!systems.initialize(project.root) || !run(systems, project.root, screenshots))
    {
        return 1;
    }
    std::puts("Font/UI GPU draws, input, resize, scene reuse and resource retirement passed.");
    return 0;
}
