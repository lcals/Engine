#include "runtime/app/app.hpp"
#include "runtime/app/runtime_host.hpp"
#include "runtime/render/render_system.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "runtime/utils/path_utils.hpp"
#include "runtime/window/window_system.hpp"
#include "runtime/world/world_system.hpp"
#include <memory>

#include <SDL3/SDL_log.h>
#include <emscripten.h>

#include <cstdint>
#include <string>

namespace engine::test_support
{
namespace
{
void progress(int state)
{
    // clang-format off
    MAIN_THREAD_EM_ASM({ globalThis.webRenderProgress = $0; }, state);
    // clang-format on
}

class WebReadbackTest final : public System
{
public:
    WebReadbackTest(RenderSystem& renderer, WindowSystem& windows)
        : render_(renderer), window_(windows)
    {
    }
    ~WebReadbackTest() noexcept override
    {
        shutdown();
    }

protected:
    bool on_initialize(const EngineConfig&) override
    {
        progress(-2);
        return true;
    }
    bool on_tick(double) override
    {
        if (complete_)
        {
            return true;
        }
        if (id_ == ScreenshotId{})
        {
            progress(-1);
            path_ = PathUtils::to_utf8(
                App::cache_path() / (resized_ ? "web-readback-resized.png" : "web-readback.png"));
            ScreenshotRequest request;
            request.path = path_;
            return render_.request_screenshot(request, id_);
        }
        ScreenshotSnapshot snapshot;
        if (!render_.screenshot_status(id_, snapshot))
        {
            return false;
        }
        if (last_state_ != static_cast<int>(snapshot.state))
        {
            last_state_ = static_cast<int>(snapshot.state);
            progress(last_state_);
        }
        if (snapshot.state == ScreenshotState::failed ||
            snapshot.state == ScreenshotState::cancelled)
        {
            SDL_LogError(SDL_LOG_CATEGORY_TEST, "Web screenshot failed: %s",
                         snapshot.error.c_str());
            return false;
        }
        if (snapshot.state != ScreenshotState::complete)
        {
            return true;
        }
        ByteBuffer bytes;
        if (!VirtualFileSystem::read_file(PathUtils::from_utf8(path_), bytes) ||
            bytes.size() < 33 || snapshot.frame.kind != RenderFrameKind::scene ||
            snapshot.frame.scene_id == 0)
        {
            return false;
        }
        const auto u32 = [&](std::size_t offset)
        {
            std::uint32_t value = 0;
            for (std::size_t index = 0; index < 4; ++index)
            {
                value = (value << 8) | std::to_integer<std::uint8_t>(bytes[offset + index]);
            }
            return value;
        };
        const auto width = u32(16), height = u32(20);
        if (width == 0 || height == 0 || !render_.release_screenshot(id_))
        {
            return false;
        }
        id_ = {};
        if (!resized_)
        {
            first_width_ = width;
            first_height_ = height;
            resized_ = true;
            return window_.set_size(640, 360);
        }
        if (first_width_ == width && first_height_ == height)
        {
            return false;
        }
        complete_ = true;
        const auto receipt = "{\"state\":\"passed\",\"width\":" + std::to_string(width) +
                             ",\"height\":" + std::to_string(height) +
                             ",\"firstWidth\":" + std::to_string(first_width_) +
                             ",\"firstHeight\":" + std::to_string(first_height_) +
                             ",\"sceneId\":\"" + std::to_string(snapshot.frame.scene_id) +
                             "\",\"path\":\"" + path_ + "\"}";
        // clang-format off
        MAIN_THREAD_EM_ASM({
            globalThis.webRenderTest = JSON.parse(UTF8ToString($0));
            globalThis.webRenderTest.url = URL.createObjectURL(new Blob(
                [Module.FS.readFile(globalThis.webRenderTest.path)], {type: "image/png"}));
            console.log("WEB_RENDER_READBACK:" + JSON.stringify(globalThis.webRenderTest));
        }, receipt.c_str());
        // clang-format on
        return true;
    }

    void on_shutdown() noexcept override
    {
        if (id_ != ScreenshotId{})
        {
            (void)render_.cancel_screenshot(id_);
            (void)render_.release_screenshot(id_);
            id_ = {};
        }
    }

private:
    RenderSystem& render_;
    WindowSystem& window_;
    ScreenshotId id_;
    std::string path_;
    std::uint32_t first_width_ = 0;
    std::uint32_t first_height_ = 0;
    bool resized_ = false;
    bool complete_ = false;
    int last_state_ = -2;
};

} // namespace

bool configure_web_render_test(RuntimeStage stage, WorldSystem& world, RenderSystem& renderer,
                               WindowSystem& window)
{
    if (stage != RuntimeStage::after_physics)
    {
        return true;
    }
    std::unique_ptr<System> test = std::make_unique<WebReadbackTest>(renderer, window);
    return world.add_subsystem(test);
}
} // namespace engine::test_support
