#pragma once

#include "runtime/render/render_system.hpp"

#include <bgfx/bgfx.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>

namespace engine
{

struct RenderSystem::RendererCallbacks final : bgfx::CallbackI
{
    std::atomic<ScreenshotStatus> status{ScreenshotStatus::idle};
    std::mutex mutex;
    std::string pending_path;
    struct Capture;
    std::shared_ptr<Capture> capture;
    std::future<bool> writing;
    bool readback_received = false;

    // 以下两个方法只由主线程调用；bgfx 关闭后才可 finish。
    void process(TaskSystem* tasks);
    void finish() noexcept;

    void fatal(const char* file, std::uint16_t line, bgfx::Fatal::Enum code,
               const char* message) override;
    void traceVargs(const char* file, std::uint16_t line, const char* format,
                    va_list arguments) override;
    void profilerBegin(const char* name, std::uint32_t color, const char* file,
                       std::uint16_t line) override;
    void profilerBeginLiteral(const char* name, std::uint32_t color, const char* file,
                              std::uint16_t line) override;
    void profilerEnd() override;
    std::uint32_t cacheReadSize(std::uint64_t id) override;
    bool cacheRead(std::uint64_t id, void* data, std::uint32_t size) override;
    void cacheWrite(std::uint64_t id, const void* data, std::uint32_t size) override;
    void captureBegin(std::uint32_t width, std::uint32_t height, std::uint32_t pitch,
                      bgfx::TextureFormat::Enum format, bool yflip) override;
    void captureEnd() override;
    void captureFrame(const void* data, std::uint32_t size) override;
    void screenShot(const char* path, std::uint32_t width, std::uint32_t height,
                    std::uint32_t pitch, const void* data, std::uint32_t size, bool yflip) override;
};

} // namespace engine
