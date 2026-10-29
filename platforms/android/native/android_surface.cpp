#include "android_surface.hpp"

#include <SDL3/SDL.h>
#include <jni.h>

#include <mutex>

namespace engine::android
{
namespace
{

std::mutex surface_mutex;
bool paused = false;
bool rendering = false;

void SDLCALL suspend_on_main_thread(void* userdata)
{
    // SDL 退出后 RunOnMainThread 可能直接在调用线程执行；此时不访问 renderer。
    if (!SDL_IsMainThread())
    {
        return;
    }
    SDL_Event event{};
    event.type = SDL_EVENT_USER;
    event.user.code = suspend_rendering_event;
    *static_cast<bool*>(userdata) = SDL_PushEvent(&event);
}

bool pause_surface()
{
    {
        const std::lock_guard lock(surface_mutex);
        paused = true;
        if (!rendering)
        {
            return true;
        }
    }

    // 不持锁等待 SDL 主线程；其事件观察器会关闭 bgfx 并等待渲染线程退出。
    bool delivered = false;
    const bool dispatched = SDL_RunOnMainThread(suspend_on_main_thread, &delivered, true);
    const std::lock_guard lock(surface_mutex);
    if (!rendering)
    {
        return true;
    }
    if (!dispatched || !delivered)
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cannot suspend Android rendering: %s",
                     SDL_GetError());
    }
    return false;
}

void resume_surface()
{
    const std::lock_guard lock(surface_mutex);
    paused = false;
}

} // namespace

bool surface_paused() noexcept
{
    const std::lock_guard lock(surface_mutex);
    return paused;
}

bool begin_rendering() noexcept
{
    const std::lock_guard lock(surface_mutex);
    if (paused)
    {
        return false;
    }
    rendering = true;
    return true;
}

void end_rendering() noexcept
{
    const std::lock_guard lock(surface_mutex);
    rendering = false;
}

} // namespace engine::android

extern "C" JNIEXPORT jboolean JNICALL
Java_org_engine_minimal_EngineActivity_nativePauseRendering(JNIEnv*, jclass)
{
    return engine::android::pause_surface() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_org_engine_minimal_EngineActivity_nativeResumeRendering(JNIEnv*, jclass)
{
    engine::android::resume_surface();
}
