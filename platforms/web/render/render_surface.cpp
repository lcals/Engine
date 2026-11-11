#include "runtime/render/render_surface.hpp"

#include "web/render/web_surface_state.hpp"

#include <algorithm>
#include <new>
#include <vector>

namespace engine::render_detail
{
namespace
{
using State = std::shared_ptr<WebSurfaceState>;
std::vector<State> surfaces;

std::string message(WGPUStringView value)
{
    if (value.data == nullptr)
    {
        return {};
    }
    const auto length =
        value.length == WGPU_STRLEN ? std::char_traits<char>::length(value.data) : value.length;
    return std::string(value.data, std::min<std::size_t>(length, 65536));
}

void adapter_ready(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView error,
                   void* userdata, void*)
{
    std::unique_ptr<State> held(static_cast<State*>(userdata));
    auto& state = **held;
    --state.pending_callbacks;
    if (state.cancelled || status != WGPURequestAdapterStatus_Success)
    {
        if (adapter != nullptr)
        {
            wgpuAdapterRelease(adapter);
        }
        if (!state.cancelled)
        {
            state.error = "WebGPU adapter request failed: " + message(error);
        }
        return;
    }
    state.adapter = adapter;
}

void device_lost(WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView error,
                 void* userdata, void*)
{
    std::unique_ptr<State> held(static_cast<State*>(userdata));
    auto& state = **held;
    --state.pending_callbacks;
    if (!state.cancelled && reason != WGPUDeviceLostReason_Destroyed)
    {
        state.error = "WebGPU device lost: " + message(error);
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", state.error.c_str());
    }
}

void device_error(WGPUDevice const*, WGPUErrorType, WGPUStringView error, void* userdata, void*)
{
    auto& state = *static_cast<WebSurfaceState*>(userdata);
    if (!state.cancelled)
    {
        state.error = "WebGPU validation failed: " + message(error);
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", state.error.c_str());
    }
}

void device_ready(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView error,
                  void* userdata, void*)
{
    std::unique_ptr<State> held(static_cast<State*>(userdata));
    auto& state = **held;
    --state.pending_callbacks;
    if (state.cancelled || status != WGPURequestDeviceStatus_Success)
    {
        if (device != nullptr)
        {
            wgpuDeviceDestroy(device);
            wgpuDeviceRelease(device);
        }
        if (!state.cancelled)
        {
            state.error = "WebGPU device request failed: " + message(error);
        }
        return;
    }
    state.device = device;
}
} // namespace

bool web_gpu_callbacks_pending()
{
    std::erase_if(surfaces, [](const State& state)
                  { return state->cancelled && state->pending_callbacks == 0; });
    return !surfaces.empty();
}

bool RenderSurface::prepare(bgfx::Init& init, bool& available)
{
    available = false;
    if (window_ == nullptr || current_.nwh != nullptr)
    {
        return false;
    }
    if (!web_state_)
    {
        auto candidate = std::make_shared<WebSurfaceState>();
        candidate->selector =
            SDL_GetStringProperty(SDL_GetWindowProperties(window_),
                                  SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING, "#canvas");
        const WGPUInstanceDescriptor descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
        candidate->instance = wgpuCreateInstance(&descriptor);
        if (candidate->instance == nullptr)
        {
            return false;
        }
        surfaces.push_back(candidate);
        web_state_ = std::move(candidate);
    }
    auto& state = *web_state_;
    if (state.cancelled || !state.error.empty())
    {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", state.error.c_str());
        return false;
    }
    if (!state.adapter_requested)
    {
        auto* held = new (std::nothrow) State(web_state_);
        if (held == nullptr)
        {
            return false;
        }
        WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
        options.powerPreference = WGPUPowerPreference_HighPerformance;
        WGPURequestAdapterCallbackInfo callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.callback = adapter_ready;
        callback.userdata1 = held;
        ++state.pending_callbacks;
        state.adapter_requested = true;
        (void)wgpuInstanceRequestAdapter(state.instance, &options, callback);
        return true;
    }
    if (state.adapter == nullptr)
    {
        return true;
    }
    if (!state.device_requested)
    {
        WGPULimits limits = WGPU_LIMITS_INIT;
        if (wgpuAdapterGetLimits(state.adapter, &limits) != WGPUStatus_Success)
        {
            return false;
        }
        WGPUSupportedFeatures features = WGPU_SUPPORTED_FEATURES_INIT;
        wgpuAdapterGetFeatures(state.adapter, &features);
        auto* requested = new (std::nothrow) State(web_state_);
        auto* lost = new (std::nothrow) State(web_state_);
        if (requested == nullptr || lost == nullptr)
        {
            delete requested;
            delete lost;
            wgpuSupportedFeaturesFreeMembers(features);
            return false;
        }
        WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
        descriptor.requiredLimits = &limits;
        descriptor.requiredFeatureCount = features.featureCount;
        descriptor.requiredFeatures = features.features;
        descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        descriptor.deviceLostCallbackInfo.callback = device_lost;
        descriptor.deviceLostCallbackInfo.userdata1 = lost;
        descriptor.uncapturedErrorCallbackInfo.callback = device_error;
        descriptor.uncapturedErrorCallbackInfo.userdata1 = web_state_.get();
        WGPURequestDeviceCallbackInfo callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.callback = device_ready;
        callback.userdata1 = requested;
        state.pending_callbacks += 2;
        state.device_requested = true;
        (void)wgpuAdapterRequestDevice(state.adapter, &descriptor, callback);
        wgpuSupportedFeaturesFreeMembers(features);
        return true;
    }
    if (state.device == nullptr)
    {
        return true;
    }
    std::uint32_t width = 0, height = 0;
    if (!read_size(width, height))
    {
        return false;
    }
    bgfx::PlatformData data{};
    auto swap_chain = init.swapChain;
    if (!read_platform_data(data, swap_chain))
    {
        return false;
    }
    swap_chain.width = width;
    swap_chain.height = height;
    current_ = swap_chain;
    platform_data_ = data;
    init.type = bgfx::RendererType::WebGPU;
    init.platformData = data;
    init.swapChain = swap_chain;
    available = true;
    return true;
}

bool RenderSurface::read_platform_data(bgfx::PlatformData& data, bgfx::SwapChain& swap_chain) const
{
    data = {};
    if (!web_state_ || web_state_->cancelled || !web_state_->error.empty() ||
        web_state_->device == nullptr)
    {
        return false;
    }
    data.context = web_state_->device;
    swap_chain.nwh = const_cast<char*>(web_state_->selector.c_str());
    swap_chain.ndt = nullptr;
    return true;
}

void RenderSurface::release() noexcept
{
    if (web_state_)
    {
        auto& state = *web_state_;
        state.cancelled = true;
        if (state.device != nullptr)
        {
            wgpuDeviceDestroy(state.device);
            wgpuDeviceRelease(state.device);
            state.device = nullptr;
        }
        if (state.adapter != nullptr)
        {
            wgpuAdapterRelease(state.adapter);
        }
        if (state.instance != nullptr)
        {
            wgpuInstanceRelease(state.instance);
        }
        state.adapter = nullptr;
        state.instance = nullptr;
        web_state_.reset();
    }
    current_ = {};
    platform_data_ = {};
}

bool RenderSurface::start_watching()
{
    return true;
}
void RenderSurface::stop_watching() noexcept
{
}
} // namespace engine::render_detail
