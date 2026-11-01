#include "plugins/runtime/script/bindings/input_binding.hpp"

#include "plugins/runtime/script/bindings/binding_context.hpp"
#include "runtime/input/input_system.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <type_traits>

namespace engine::plugins::script::bindings
{
namespace
{
using Json = nlohmann::json;
using Value = vm::NativeValue;

constexpr std::string_view javascript = R"js(
function inputJson(value) {
    try { return JSON.stringify(value, (_, item) => typeof item === 'bigint' ? item.toString() : item); }
    catch (_) { return null; }
}
export class Input {
    constructor() { throw new TypeError('Input is static'); }
    static GetFrameState() { return native.InputGetFrameState(); }
    static GetDevices() { return native.InputGetDevices(); }
    static GetControls(device) { return native.InputGetControls(device); }
    static ReadControl(control) { return native.InputReadControl(inputJson(control)); }
    static GetContacts() { return native.InputGetContacts(); }
    static CreateChannel() { return native.InputCreateChannel(); }
    static ConfigureChannel(channel, data) {
        return native.InputConfigureChannel(channel?.slot, channel?.generation, inputJson(data));
    }
    static SetChannelEnabled(channel, enabled, activation) {
        return native.InputSetChannelEnabled(channel?.slot, channel?.generation, enabled, activation);
    }
    static DestroyChannel(channel) {
        return native.InputDestroyChannel(channel?.slot, channel?.generation);
    }
    static ReadAction(channel, name) {
        return native.InputReadAction(channel?.slot, channel?.generation, name);
    }
    static BeginCapture(channel, options) {
        return native.InputBeginCapture(channel?.slot, channel?.generation, inputJson(options));
    }
    static ReadCapture(capture) {
        return native.InputReadCapture(capture?.slot, capture?.generation);
    }
    static CancelCapture(capture) {
        return native.InputCancelCapture(capture?.slot, capture?.generation);
    }
    static get lastError() { return native.InputLastError(); }
}
Object.freeze(Input.prototype);
Object.freeze(Input);
)js";

template <typename Enum>
void append_enum(std::string& output, std::string_view name, Enum maximum,
                 const char* (*enum_name)(Enum))
{
    output += "export const ";
    output += name;
    output += " = Object.freeze({";
    for (unsigned int index = 0; index <= static_cast<unsigned int>(maximum); ++index)
    {
        if (index != 0)
        {
            output += ',';
        }
        output += enum_name(static_cast<Enum>(index));
        output += ':';
        output += std::to_string(index);
        output += ',';
        output += std::to_string(index);
        output += ":'";
        output += enum_name(static_cast<Enum>(index));
        output += '\'';
    }
    output += "});\n";
}

bool fields(const Json& object, std::initializer_list<std::string_view> allowed)
{
    if (!object.is_object())
    {
        return false;
    }
    for (auto item = object.begin(); item != object.end(); ++item)
    {
        if (std::find(allowed.begin(), allowed.end(), item.key()) == allowed.end())
        {
            return false;
        }
    }
    return true;
}

bool text(const Json& object, const char* name, std::string& output)
{
    const auto item = object.find(name);
    if (item == object.end())
    {
        return true;
    }
    if (!item->is_string() || item->get_ref<const std::string&>().size() > 4096)
    {
        return false;
    }
    output = item->get_ref<const std::string&>();
    return true;
}

bool boolean(const Json& object, const char* name, bool& output)
{
    const auto item = object.find(name);
    if (item == object.end())
    {
        return true;
    }
    if (!item->is_boolean())
    {
        return false;
    }
    output = item->get<bool>();
    return true;
}

template <typename Number> bool number(const Json& object, const char* name, Number& output)
{
    const auto item = object.find(name);
    if (item == object.end())
    {
        return true;
    }
    if (!item->is_number())
    {
        return false;
    }
    const double value = item->get<double>();
    if (!std::isfinite(value))
    {
        return false;
    }
    if constexpr (std::is_integral_v<Number>)
    {
        if (std::trunc(value) != value || value < std::numeric_limits<Number>::lowest() ||
            value > std::numeric_limits<Number>::max())
        {
            return false;
        }
    }
    output = static_cast<Number>(value);
    return true;
}

template <typename Enum> bool enumeration(const Json& object, const char* name, Enum& output)
{
    std::uint32_t value = static_cast<std::uint32_t>(output);
    if (!number(object, name, value) || value > static_cast<std::uint32_t>(Enum::MAX))
    {
        return false;
    }
    output = static_cast<Enum>(value);
    return true;
}

bool identifier(const Json& value, std::uint64_t& output)
{
    output = 0;
    if (!value.is_string())
    {
        return false;
    }
    const auto& string = value.get_ref<const std::string&>();
    const auto parsed = std::from_chars(string.data(), string.data() + string.size(), output);
    return parsed.ec == std::errc{} && parsed.ptr == string.data() + string.size() && output != 0;
}

bool control(const Json& value, schema::InputControlRef& output)
{
    std::uint64_t device = 0;
    auto kind = schema::InputControlKind::key;
    std::uint32_t code = 0;
    const auto id = value.find("connection_id");
    if (!fields(value, {"connection_id", "kind", "code"}) || id == value.end() ||
        !identifier(*id, device) || !value.contains("kind") || !value.contains("code") ||
        !enumeration(value, "kind", kind) || !number(value, "code", code))
    {
        return false;
    }
    output = schema::InputControlRef(device, kind, code);
    return true;
}

template <typename Item, typename Parse>
bool objects(const Json& value, const char* name, std::vector<std::unique_ptr<Item>>& output,
             Parse parse)
{
    const auto array = value.find(name);
    if (array == value.end())
    {
        return true;
    }
    if (!array->is_array() || array->size() > 4096)
    {
        return false;
    }
    for (const auto& item : *array)
    {
        auto parsed = std::make_unique<Item>();
        if (!parse(item, *parsed))
        {
            return false;
        }
        output.push_back(std::move(parsed));
    }
    return true;
}

bool part(const Json& value, schema::InputBindingPartDataT& output)
{
    const auto source = value.find("source");
    return fields(value, {"source", "virtual_control", "virtual_axis", "weight_x", "weight_y"}) &&
           (source == value.end() || control(*source, output.source)) &&
           text(value, "virtual_control", output.virtual_control) &&
           number(value, "virtual_axis", output.virtual_axis) &&
           number(value, "weight_x", output.weight_x) && number(value, "weight_y", output.weight_y);
}

bool binding(const Json& value, schema::InputBindingDataT& output)
{
    if (!fields(value, {"parts", "modifiers", "scale", "deadzone", "press_threshold",
                        "release_threshold", "normalize"}) ||
        !objects(value, "parts", output.parts, part) || !number(value, "scale", output.scale) ||
        !number(value, "deadzone", output.deadzone) ||
        !number(value, "press_threshold", output.press_threshold) ||
        !number(value, "release_threshold", output.release_threshold) ||
        !boolean(value, "normalize", output.normalize))
    {
        return false;
    }
    const auto modifiers = value.find("modifiers");
    if (modifiers == value.end())
    {
        return true;
    }
    if (!modifiers->is_array() || modifiers->size() > 4096)
    {
        return false;
    }
    for (const auto& item : *modifiers)
    {
        schema::InputControlRef parsed;
        if (!control(item, parsed))
        {
            return false;
        }
        output.modifiers.push_back(parsed);
    }
    return true;
}

bool action(const Json& value, schema::InputActionDataT& output)
{
    return fields(value, {"name", "kind", "value_mode", "combine", "bindings"}) &&
           text(value, "name", output.name) && enumeration(value, "kind", output.kind) &&
           enumeration(value, "value_mode", output.value_mode) &&
           enumeration(value, "combine", output.combine) &&
           objects(value, "bindings", output.bindings, binding);
}

bool region(const Json& value, schema::InputRegionDataT& output)
{
    const auto id = value.find("connection_id");
    return fields(value, {"connection_id", "x", "y", "width", "height", "anchor_x", "anchor_y",
                          "shape", "floating", "deadzone", "cancel_on_exit"}) &&
           id != value.end() && identifier(*id, output.connection_id) &&
           number(value, "x", output.x) && number(value, "y", output.y) &&
           number(value, "width", output.width) && number(value, "height", output.height) &&
           number(value, "anchor_x", output.anchor_x) &&
           number(value, "anchor_y", output.anchor_y) &&
           enumeration(value, "shape", output.shape) &&
           boolean(value, "floating", output.floating) &&
           number(value, "deadzone", output.deadzone) &&
           boolean(value, "cancel_on_exit", output.cancel_on_exit);
}

bool virtual_control(const Json& value, schema::InputVirtualControlDataT& output)
{
    if (!fields(value, {"id", "kind", "priority", "enabled", "visible", "region"}) ||
        !text(value, "id", output.id) || !enumeration(value, "kind", output.kind) ||
        !number(value, "priority", output.priority) || !boolean(value, "enabled", output.enabled) ||
        !boolean(value, "visible", output.visible))
    {
        return false;
    }
    const auto item = value.find("region");
    if (item != value.end())
    {
        output.region = std::make_unique<schema::InputRegionDataT>();
        return region(*item, *output.region);
    }
    return true;
}

bool channel(const Json& value, schema::InputChannelDataT& output)
{
    return fields(value, {"enabled", "priority", "consume_inputs", "activation", "actions",
                          "virtual_controls"}) &&
           boolean(value, "enabled", output.enabled) &&
           number(value, "priority", output.priority) &&
           boolean(value, "consume_inputs", output.consume_inputs) &&
           enumeration(value, "activation", output.activation) &&
           objects(value, "actions", output.actions, action) &&
           objects(value, "virtual_controls", output.virtual_controls, virtual_control);
}

bool capture_options(const Json& value, schema::InputCaptureOptionsT& output)
{
    if (!fields(value,
                {"devices", "kinds", "threshold", "neutral_threshold", "include_modifiers"}) ||
        !number(value, "threshold", output.threshold) ||
        !number(value, "neutral_threshold", output.neutral_threshold) ||
        !boolean(value, "include_modifiers", output.include_modifiers))
    {
        return false;
    }
    const auto devices = value.find("devices");
    if (devices != value.end())
    {
        if (!devices->is_array() || devices->size() > 4096)
        {
            return false;
        }
        for (const auto& item : *devices)
        {
            std::uint64_t id = 0;
            if (!identifier(item, id))
            {
                return false;
            }
            output.devices.push_back(id);
        }
    }
    const auto kinds = value.find("kinds");
    if (kinds != value.end())
    {
        if (!kinds->is_array() || kinds->size() > 4096)
        {
            return false;
        }
        for (const auto& item : *kinds)
        {
            Json object = {{"kind", item}};
            auto kind = schema::InputControlKind::key;
            if (!enumeration(object, "kind", kind))
            {
                return false;
            }
            output.kinds.push_back(kind);
        }
    }
    return true;
}

BindingContext* context(const Arguments& arguments, int count)
{
    auto* result = BindingContext::current(arguments);
    if (!result)
    {
        return nullptr;
    }
    result->set_input_error({});
    if (arguments.count() != count)
    {
        result->set_input_error("Invalid input argument count");
        return nullptr;
    }
    if (!result->input() || !result->input()->is_initialized())
    {
        result->set_input_error("InputSystem is unavailable");
        return nullptr;
    }
    return result;
}

bool completed(BindingContext& context, bool success)
{
    if (!success)
    {
        const auto& error = context.input()->last_error();
        context.set_input_error(error.empty() ? "Input operation failed" : error);
    }
    return success;
}

bool json_argument(const Arguments& arguments, int index, BindingContext& context, Json& output)
{
    std::string string;
    if (BindingContext::text(arguments, index, string) && string.size() <= vm::maximum_text_size)
    {
        output = Json::parse(string, nullptr, false);
        if (output.is_object())
        {
            return true;
        }
    }
    context.set_input_error("Invalid input data object");
    return false;
}

bool channel_handle(const Arguments& arguments, BindingContext& context,
                    schema::InputChannelHandle& output)
{
    if (arguments.is_uint32(0) && arguments.is_uint32(1))
    {
        output = {arguments.unsigned_integer(0), arguments.unsigned_integer(1)};
        if (context.owns_input_channel(output))
        {
            return true;
        }
    }
    context.set_input_error("Invalid input channel or channel belongs to another run");
    return false;
}

bool capture_handle(const Arguments& arguments, BindingContext& context,
                    schema::InputCaptureHandle& output)
{
    if (arguments.is_uint32(0) && arguments.is_uint32(1))
    {
        output = {arguments.unsigned_integer(0), arguments.unsigned_integer(1)};
        if (context.owns_input_capture(output))
        {
            return true;
        }
    }
    context.set_input_error("Invalid input capture or capture belongs to another run");
    return false;
}

Value value(const schema::InputControlRef& source)
{
    return Value::Object{{"connection_id", source.connection_id()},
                         {"kind", static_cast<std::uint32_t>(source.kind())},
                         {"code", source.code()}};
}

template <typename Handle> Value handle_value(const Handle& handle)
{
    return Value::Object{{"slot", handle.slot()}, {"generation", handle.generation()}};
}

void get_frame_state(const Arguments& arguments)
{
    arguments.set_return({});
    if (auto* binding = context(arguments, 0))
    {
        const auto& state = binding->input()->frame_state();
        arguments.set_return(Value::Object{{"frame_id", state.frame_id()},
                                           {"device_revision", state.device_revision()},
                                           {"cancel_revision", state.cancel_revision()}});
    }
}

void get_devices(const Arguments& arguments)
{
    arguments.set_return({});
    if (auto* binding = context(arguments, 0))
    {
        Value::Array output;
        for (const auto& device : binding->input()->devices())
        {
            output.emplace_back(Value::Object{{"connection_id", device.connection_id},
                                              {"kind", static_cast<std::uint32_t>(device.kind)},
                                              {"name", device.name},
                                              {"layout_key", device.layout_key},
                                              {"capabilities", device.capabilities}});
        }
        arguments.set_return(std::move(output));
    }
}

void get_controls(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = context(arguments, 1);
    std::uint64_t device = 0;
    std::vector<schema::InputControlInfoT> controls;
    if (!binding)
    {
        return;
    }
    if (!BindingContext::identifier(arguments, 0, device))
    {
        binding->set_input_error("Device must be a nonzero uint64 bigint");
        return;
    }
    if (!completed(*binding, binding->input()->controls(device, controls)))
    {
        return;
    }
    Value::Array output;
    for (const auto& control : controls)
    {
        output.emplace_back(
            Value::Object{{"source", value(control.source)},
                          {"name", control.name},
                          {"value_mode", static_cast<std::uint32_t>(control.value_mode)},
                          {"unit", static_cast<std::uint32_t>(control.unit)},
                          {"minimum", control.minimum},
                          {"maximum", control.maximum}});
    }
    arguments.set_return(std::move(output));
}

void read_control(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = context(arguments, 1);
    Json object;
    schema::InputControlRef source;
    schema::InputControlState output;
    if (!binding || !json_argument(arguments, 0, *binding, object))
    {
        return;
    }
    if (!control(object, source))
    {
        binding->set_input_error("Invalid input control reference");
        return;
    }
    if (completed(*binding, binding->input()->read_control(source, output)))
    {
        arguments.set_return(Value::Object{{"source", value(output.source())},
                                           {"frame_id", output.frame_id()},
                                           {"value", output.value()},
                                           {"held", output.held()},
                                           {"pressed", output.pressed()},
                                           {"released", output.released()},
                                           {"canceled", output.canceled()},
                                           {"repeat", output.repeat()}});
    }
}

void get_contacts(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = context(arguments, 0);
    std::vector<schema::InputContactStateT> contacts;
    if (!binding || !completed(*binding, binding->input()->read_contacts(contacts)))
    {
        return;
    }
    Value::Array output;
    for (const auto& contact : contacts)
    {
        output.emplace_back(Value::Object{{"source", value(contact.source)},
                                          {"contact_id", contact.contact_id},
                                          {"normalized_x", contact.normalized_x},
                                          {"normalized_y", contact.normalized_y},
                                          {"logical_x", contact.logical_x},
                                          {"logical_y", contact.logical_y},
                                          {"pressure", contact.pressure},
                                          {"held", contact.held}});
    }
    arguments.set_return(std::move(output));
}

void create_channel(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = context(arguments, 0);
    schema::InputChannelHandle output;
    if (binding && completed(*binding, binding->input()->create_channel(output)))
    {
        binding->remember_input_channel(output);
        arguments.set_return(handle_value(output));
    }
}

void configure_channel(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* binding = context(arguments, 3);
    schema::InputChannelHandle handle;
    schema::InputChannelDataT data;
    Json object;
    if (!binding || !channel_handle(arguments, *binding, handle) ||
        !json_argument(arguments, 2, *binding, object))
    {
        return;
    }
    if (!channel(object, data))
    {
        binding->set_input_error("Invalid channel data fields or values");
        return;
    }
    arguments.set_return(completed(*binding, binding->input()->configure_channel(handle, data)));
}

void set_channel_enabled(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* binding = context(arguments, 4);
    schema::InputChannelHandle handle;
    if (!binding || !channel_handle(arguments, *binding, handle))
    {
        return;
    }
    if (!arguments.is_boolean(2) || !arguments.is_uint32(3) ||
        arguments.unsigned_integer(3) > static_cast<std::uint32_t>(schema::InputActivation::MAX))
    {
        binding->set_input_error("Invalid channel enabled flag or activation");
        return;
    }
    arguments.set_return(completed(
        *binding, binding->input()->set_channel_enabled(
                      handle, arguments.boolean(2),
                      static_cast<schema::InputActivation>(arguments.unsigned_integer(3)))));
}

void destroy_channel(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* binding = context(arguments, 2);
    schema::InputChannelHandle handle;
    if (binding && channel_handle(arguments, *binding, handle) &&
        completed(*binding, binding->input()->destroy_channel(handle)))
    {
        binding->forget_input_channel(handle);
        arguments.set_return(true);
    }
}

void read_action(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = context(arguments, 3);
    schema::InputChannelHandle handle;
    std::string name;
    schema::InputActionState output;
    if (!binding || !channel_handle(arguments, *binding, handle))
    {
        return;
    }
    if (!BindingContext::text(arguments, 2, name))
    {
        binding->set_input_error("Action name must be a string");
        return;
    }
    if (completed(*binding, binding->input()->read_action(handle, name, output)))
    {
        arguments.set_return(Value::Object{{"frame_id", output.frame_id()},
                                           {"value_x", output.value_x()},
                                           {"value_y", output.value_y()},
                                           {"held", output.held()},
                                           {"pressed", output.pressed()},
                                           {"released", output.released()},
                                           {"canceled", output.canceled()}});
    }
}

void begin_capture(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = context(arguments, 3);
    schema::InputChannelHandle channel;
    schema::InputCaptureOptionsT options;
    schema::InputCaptureHandle capture;
    Json object;
    if (!binding || !channel_handle(arguments, *binding, channel) ||
        !json_argument(arguments, 2, *binding, object))
    {
        return;
    }
    if (!capture_options(object, options))
    {
        binding->set_input_error("Invalid capture options fields or values");
        return;
    }
    if (completed(*binding, binding->input()->begin_capture(channel, options, capture)))
    {
        binding->remember_input_capture(channel, capture);
        arguments.set_return(handle_value(capture));
    }
}

void read_capture(const Arguments& arguments)
{
    arguments.set_return({});
    auto* binding = context(arguments, 2);
    schema::InputCaptureHandle handle;
    schema::InputCaptureStateT state;
    if (!binding || !capture_handle(arguments, *binding, handle) ||
        !completed(*binding, binding->input()->read_capture(handle, state)))
    {
        return;
    }
    Value::Array candidates;
    for (const auto& candidate : state.candidates)
    {
        candidates.push_back(value(candidate));
    }
    arguments.set_return(Value::Object{{"status", static_cast<std::uint32_t>(state.status)},
                                       {"frame_id", state.frame_id},
                                       {"candidates", std::move(candidates)}});
}

void cancel_capture(const Arguments& arguments)
{
    arguments.set_return(false);
    auto* binding = context(arguments, 2);
    schema::InputCaptureHandle handle;
    if (binding && capture_handle(arguments, *binding, handle))
    {
        arguments.set_return(completed(*binding, binding->input()->cancel_capture(handle)));
    }
}

void last_error(const Arguments& arguments)
{
    arguments.set_return({});
    if (auto* binding = BindingContext::current(arguments); binding && arguments.count() == 0)
    {
        arguments.set_return(binding->input_error());
    }
}
} // namespace

std::string_view InputBinding::name() const noexcept
{
    return "Input";
}

std::string_view InputBinding::source() const noexcept
{
    static const std::string source = []
    {
        std::string result(javascript);
        append_enum(result, "InputDeviceKind", schema::InputDeviceKind::MAX,
                    schema::EnumNameInputDeviceKind);
        append_enum(result, "InputControlKind", schema::InputControlKind::MAX,
                    schema::EnumNameInputControlKind);
        append_enum(result, "InputActionKind", schema::InputActionKind::MAX,
                    schema::EnumNameInputActionKind);
        append_enum(result, "InputValueMode", schema::InputValueMode::MAX,
                    schema::EnumNameInputValueMode);
        append_enum(result, "InputCombine", schema::InputCombine::MAX,
                    schema::EnumNameInputCombine);
        append_enum(result, "InputActivation", schema::InputActivation::MAX,
                    schema::EnumNameInputActivation);
        append_enum(result, "InputVirtualKind", schema::InputVirtualKind::MAX,
                    schema::EnumNameInputVirtualKind);
        append_enum(result, "InputCaptureStatus", schema::InputCaptureStatus::MAX,
                    schema::EnumNameInputCaptureStatus);
        append_enum(result, "InputUnit", schema::InputUnit::MAX, schema::EnumNameInputUnit);
        append_enum(result, "InputRegionShape", schema::InputRegionShape::MAX,
                    schema::EnumNameInputRegionShape);
        return result;
    }();
    return source;
}

bool InputBinding::register_functions(vm::ScriptVm& vm) const
{
    return vm.function("InputGetFrameState", get_frame_state) &&
           vm.function("InputGetDevices", get_devices) &&
           vm.function("InputGetControls", get_controls) &&
           vm.function("InputReadControl", read_control) &&
           vm.function("InputGetContacts", get_contacts) &&
           vm.function("InputCreateChannel", create_channel) &&
           vm.function("InputConfigureChannel", configure_channel) &&
           vm.function("InputSetChannelEnabled", set_channel_enabled) &&
           vm.function("InputDestroyChannel", destroy_channel) &&
           vm.function("InputReadAction", read_action) &&
           vm.function("InputBeginCapture", begin_capture) &&
           vm.function("InputReadCapture", read_capture) &&
           vm.function("InputCancelCapture", cancel_capture) &&
           vm.function("InputLastError", last_error);
}

} // namespace engine::plugins::script::bindings
