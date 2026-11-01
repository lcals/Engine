#pragma once

#include "runtime/input/input_system.hpp"

#include <SDL3/SDL.h>

#include <map>
#include <set>
#include <tuple>

namespace engine::input_detail
{
using Kind = schema::InputControlKind;
using DeviceKind = schema::InputDeviceKind;
using Key = std::tuple<std::uint64_t, Kind, std::uint32_t>;
using ContactKey = std::pair<std::uint64_t, std::uint64_t>;

inline Key key(const schema::InputControlRef& source)
{
    return {source.connection_id(), source.kind(), source.code()};
}

inline schema::InputControlRef source(const Key& value)
{
    return {std::get<0>(value), std::get<1>(value), std::get<2>(value)};
}

struct Control
{
    schema::InputControlInfoT info;
    double value = 0;
    double sample = 0;
    bool pressed = false;
    bool released = false;
    bool canceled = false;
    bool repeat = false;
    bool suppressed = false;
};

struct Device
{
    schema::InputDeviceDataT data;
    std::uint64_t native_id = 0;
    SDL_Gamepad* gamepad = nullptr;
    SDL_Joystick* joystick = nullptr;
};

struct Action
{
    schema::InputActionState state;
    std::vector<bool> latches;
    std::vector<bool> contributors;
    std::vector<std::set<Key>> sources;
    std::set<Key> delta_sources;
    std::set<std::string> delta_virtuals;
    std::uint64_t delta_serial = 0;
};

struct VirtualValue
{
    bool held = false;
    double x = 0;
    double y = 0;
    double dx = 0;
    double dy = 0;
    double sample_x = 0;
    double sample_y = 0;
};

struct Channel
{
    std::uint32_t generation = 0;
    bool alive = false;
    std::uint64_t order = 0;
    schema::InputChannelDataT data;
    std::unique_ptr<schema::InputChannelDataT> pending;
    std::map<std::string, Action, std::less<>> actions;
    std::map<std::string, schema::InputActionState, std::less<>> published;
    std::map<std::string, VirtualValue, std::less<>> virtuals;
    std::set<Key> blocked;
    std::map<Key, double> neutral;
    std::set<Key> neutralized;
};

struct Capture
{
    std::uint32_t generation = 0;
    bool alive = false;
    bool pending = true;
    std::uint32_t channel = 0;
    schema::InputCaptureOptionsT options;
    schema::InputCaptureStateT state;
    schema::InputCaptureStateT published;
    std::set<Key> blocked;
    std::set<Key> modifiers;
};

struct Contact
{
    schema::InputContactStateT data;
    bool suppressed = false;
};

struct PointerCapture
{
    std::uint32_t channel = 0;
    std::string control;
    double center_x = 0;
    double center_y = 0;
    double previous_x = 0;
    double previous_y = 0;
};

struct State
{
    bool frame_open = false;
    bool sdl_initialized = false;
    std::uint64_t frame_id = 0;
    std::uint64_t sample_serial = 0;
    std::uint64_t device_revision = 0;
    std::uint64_t cancel_revision = 0;
    std::uint64_t next_connection = 1;
    std::uint64_t next_order = 1;
    std::uint32_t next_generation = 1;
    std::set<std::uint32_t> unknown_keys;
    std::set<std::uint32_t> unknown_buttons;
    std::vector<Device> devices;
    std::map<Key, Control> controls;
    std::map<ContactKey, Contact> contacts;
    std::map<ContactKey, PointerCapture> pointers;
    std::vector<Channel> channels;
    std::vector<Capture> captures;
    schema::InputRoutingDataT routing;
    std::vector<schema::InputDeviceDataT> published_devices;
    std::vector<schema::InputControlInfoT> published_info;
    std::map<Key, schema::InputControlState> published_controls;
    std::vector<schema::InputContactStateT> published_contacts;
};

[[nodiscard]] bool enumerate(State& state, std::string& error);
void close_devices(State& state) noexcept;
[[nodiscard]] bool event(State& state, SDL_Window* window, const SDL_Event& event, bool claimed,
                         std::string& error);
void publish(State& state);
void evaluate(State& state, bool baseline = false, bool canceled = false);
void cancel(State& state, bool keyboard, bool pointer, bool all);
void cancel_pointers(State& state, std::uint32_t channel);
void touch(State& state, SDL_Window* window, const SDL_TouchFingerEvent& event, bool claimed);
void capture_candidates(State& state);
[[nodiscard]] bool validate(const schema::InputChannelDataT& data, std::string& error);
[[nodiscard]] bool validate_source(const schema::InputControlRef& source);
[[nodiscard]] schema::InputValueMode mode(Kind kind);
[[nodiscard]] schema::InputUnit unit(Kind kind);
} // namespace engine::input_detail
