#pragma once

#include "runtime/schema/input_generated.h"
#include "runtime/system/system.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

union SDL_Event;

namespace engine
{
class WindowSystem;
namespace input_detail
{
struct State;
}

class InputSystem final : public System
{
public:
    explicit InputSystem(WindowSystem& window) noexcept;
    ~InputSystem() noexcept override;

    [[nodiscard]] bool begin_frame();
    [[nodiscard]] bool set_routing_state(const schema::InputRoutingDataT& routing);
    [[nodiscard]] bool owns_pointer(const SDL_Event& event) const noexcept;
    [[nodiscard]] bool process_event(const SDL_Event& event, bool event_claimed);
    [[nodiscard]] const schema::InputFrameState& frame_state() const noexcept;
    [[nodiscard]] std::span<const schema::InputDeviceDataT> devices() const noexcept;
    [[nodiscard]] bool controls(std::uint64_t device,
                                std::vector<schema::InputControlInfoT>& output) const;
    [[nodiscard]] bool read_control(const schema::InputControlRef& control,
                                    schema::InputControlState& output) const;
    [[nodiscard]] bool read_contacts(std::vector<schema::InputContactStateT>& output) const;
    [[nodiscard]] bool create_channel(schema::InputChannelHandle& output);
    [[nodiscard]] bool configure_channel(schema::InputChannelHandle channel,
                                         const schema::InputChannelDataT& data);
    [[nodiscard]] bool set_channel_enabled(schema::InputChannelHandle channel, bool enabled,
                                           schema::InputActivation activation);
    [[nodiscard]] bool destroy_channel(schema::InputChannelHandle channel);
    [[nodiscard]] bool read_action(schema::InputChannelHandle channel, std::string_view name,
                                   schema::InputActionState& output) const;
    [[nodiscard]] bool begin_capture(schema::InputChannelHandle channel,
                                     const schema::InputCaptureOptionsT& options,
                                     schema::InputCaptureHandle& output);
    [[nodiscard]] bool read_capture(schema::InputCaptureHandle capture,
                                    schema::InputCaptureStateT& output) const;
    [[nodiscard]] bool cancel_capture(schema::InputCaptureHandle capture);
    [[nodiscard]] const std::string& last_error() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    [[nodiscard]] bool on_tick(double delta_time) override;
    void on_shutdown() noexcept override;

private:
    WindowSystem& window_;
    std::unique_ptr<input_detail::State> state_;
    schema::InputFrameState frame_;
    std::uint64_t next_connection_ = 1;
    std::uint32_t next_generation_ = 1;
    mutable std::string last_error_;
};

} // namespace engine
