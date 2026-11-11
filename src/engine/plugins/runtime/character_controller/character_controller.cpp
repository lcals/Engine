#include "plugins/runtime/character_controller/character_controller.hpp"
#include "plugins/runtime/character_controller/abilities/ability_validation.hpp"
#include "plugins/runtime/character_controller/character_controller_runtime.hpp"
#include <numeric>
#include <set>
namespace engine::plugins::character_controller
{
bool CharacterController::register_type(ComponentRegistry& r)
{
    return register_component_storage<CharacterController, Data, Runtime>(r);
}
CharacterController::Runtime* CharacterController::runtime() const noexcept
{
    return runtime_state<Runtime>();
}
bool CharacterController::initialize()
{
    if (!writable())
    {
        return false;
    }
    auto* d = write_data<Data>();
    d->movement = std::make_unique<schema::CharacterMovementDataT>();
    d->facing = std::make_unique<schema::CharacterFacingDataT>();
    d->ability_data = std::make_unique<schema::CharacterAbilitySetDataT>();
    return true;
}
bool CharacterController::valid_settings(const Data& d, const CharacterAbilityRegistry& types,
                                         std::string& e)
{
    e.clear();
    if (!types.frozen() || !d.movement || !d.facing || !d.ability_data)
    {
        return detail::invalid(e, "missing movement, facing or ability data");
    }
    const auto& m = *d.movement;
    if (m.space < schema::CharacterMoveSpace::MIN || m.space > schema::CharacterMoveSpace::MAX ||
        d.facing->mode < schema::CharacterFacingMode::MIN ||
        d.facing->mode > schema::CharacterFacingMode::MAX ||
        !detail::bounded(m.max_speed, 0, 1000) ||
        !detail::bounded(m.ground_acceleration, 0, 10000) ||
        !detail::bounded(m.ground_braking, 0, 10000) ||
        !detail::bounded(m.air_acceleration, 0, 10000) ||
        !detail::bounded(m.air_braking, 0, 10000) || !detail::bounded(m.air_speed_limit, 0, 1000) ||
        !detail::bounded(d.facing->turn_speed, 0, 36000))
    {
        return detail::invalid(e, "invalid movement or facing parameters");
    }
    if ((m.space == schema::CharacterMoveSpace::view ||
         d.facing->mode == schema::CharacterFacingMode::view) &&
        d.camera_node_id == 0)
    {
        return detail::invalid(e, "view control requires an explicit camera");
    }
    std::set<std::string> names, ids, data_ids;
    for (const auto& a : d.actions)
    {
        if (a.name.empty() || !names.insert(a.name).second ||
            !detail::bounded(a.buffer_seconds, 0, 10))
        {
            return detail::invalid(e, "invalid or duplicate action");
        }
    }
    const auto check = [&](const auto& entries, std::string_view type)
    {
        for (const auto& v : entries)
        {
            if (v.id.empty() || !data_ids.insert(v.id).second)
            {
                return false;
            }
            if (std::none_of(d.abilities.begin(), d.abilities.end(),
                             [&](const auto& ref) { return ref.id == v.id && ref.type == type; }))
            {
                return false;
            }
        }
        return true;
    };
    if (!check(d.ability_data->sprint, "sprint") || !check(d.ability_data->jump, "jump") ||
        !check(d.ability_data->crouch, "crouch"))
    {
        return detail::invalid(e, "duplicate or unreferenced ability data");
    }
    for (const auto& ref : d.abilities)
    {
        if (ref.id.empty() || !ids.insert(ref.id).second)
        {
            return detail::invalid(e, "invalid ability identity");
        }
        const auto* ops = types.find(ref.type);
        if (!ops)
        {
            return detail::invalid(e, "unknown ability type");
        }
        if (!ops->validate(d, ref.id, e))
        {
            return false;
        }
    }
    return true;
}
bool CharacterController::get_settings(Data& out) const
{
    const auto* d = read_data<Data>();
    out = d ? *d : Data{};
    return d;
}
bool CharacterController::set_settings(const Data& d, std::string& e)
{
    if (!writable())
    {
        return detail::invalid(e, "invalid character controller");
    }
    const auto* types = character_abilities(*node().world());
    if (!types || !valid_settings(d, *types, e))
    {
        return false;
    }
    const auto& old = *read_data<Data>();
    auto& r = *runtime();
    bool input_changed =
        old.actions.size() != d.actions.size() || old.abilities.size() != d.abilities.size();
    for (std::size_t i = 0; !input_changed && i < d.actions.size(); ++i)
    {
        input_changed = old.actions[i].name != d.actions[i].name;
    }
    for (std::size_t i = 0; !input_changed && i < d.abilities.size(); ++i)
    {
        input_changed = old.abilities[i].id != d.abilities[i].id ||
                        old.abilities[i].type != d.abilities[i].type ||
                        old.abilities[i].enabled != d.abilities[i].enabled;
    }
    // Action bindings belong to the generated concrete type; compare their declaration only.
    const auto changed = [](const auto& a, const auto& b)
    {
        if (a.size() != b.size())
        {
            return true;
        }
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].id != b[i].id || a[i].action != b[i].action)
            {
                return true;
            }
        }
        return false;
    };
    if (old.ability_data)
    {
        input_changed = input_changed ||
                        changed(old.ability_data->sprint, d.ability_data->sprint) ||
                        changed(old.ability_data->jump, d.ability_data->jump) ||
                        changed(old.ability_data->crouch, d.ability_data->crouch);
    }
    if (old.camera_node_id != d.camera_node_id)
    {
        r.camera_resolved = false;
        r.camera_node = {};
        input_changed = true;
    }
    *write_data<Data>() = d;
    modified();
    if (input_changed)
    {
        if (!clear_input())
        {
            return false;
        }
        r.actions.clear();
        for (const auto& a : d.actions)
        {
            r.actions.push_back({a.name});
        }
    }
    r.ordered_abilities.resize(d.abilities.size());
    std::iota(r.ordered_abilities.begin(), r.ordered_abilities.end(), 0);
    std::stable_sort(r.ordered_abilities.begin(), r.ordered_abilities.end(), [&](auto a, auto b)
                     { return d.abilities[a].priority > d.abilities[b].priority; });
    for (const auto& ops : types->operations())
    {
        ops.prune(*node().world());
    }
    return true;
}
bool CharacterController::set_input(const CharacterInput& input)
{
    if (!writable() || !active() || node().world()->paused())
    {
        return false;
    }
    const auto finite = [](const Vec2& v) { return std::isfinite(v.x()) && std::isfinite(v.y()); };
    if (!finite(input.move) || !finite(input.look_delta) || !finite(input.look_rate) ||
        std::abs(input.move.x()) > 1 || std::abs(input.move.y()) > 1)
    {
        return false;
    }
    const auto& d = *read_data<Data>();
    std::set<std::string> names;
    for (const auto& a : input.actions)
    {
        if (!names.insert(a.name).second || !detail::action_exists(d, a.name))
        {
            return false;
        }
    }
    auto value = input;
    double length = std::hypot(value.move.x(), value.move.y());
    if (length > 1)
    {
        value.move = {value.move.x() / length, value.move.y() / length};
    }
    runtime()->pending_input = std::move(value);
    runtime()->has_input = true;
    return true;
}
bool CharacterController::clear_input() noexcept
{
    if (!writable())
    {
        return false;
    }
    auto& r = *runtime();
    r.pending_input = {};
    r.sampled_input = {};
    r.has_input = false;
    r.desired_velocity = {};
    r.candidate_velocity = {};
    r.prepared = false;
    for (auto& a : r.actions)
    {
        a.held = false;
        a.has_press = false;
        a.press_age = 0;
    }
    r.status.actions = r.actions;
    r.status.desired_planar_velocity = {};
    return true;
}
bool CharacterController::get_status(CharacterStatus& out) const
{
    out = valid() ? runtime()->status : CharacterStatus{};
    return valid();
}
bool CharacterController::restore_data(const schema::ComponentDataT& d)
{
    const auto* data = d.payload.AsCharacterControllerData();
    std::string e;
    if (!d.flags || !data || !set_settings(*data, e))
    {
        return false;
    }
    if (const auto* types = character_abilities(*node().world()))
    {
        for (const auto& ops : types->operations())
        {
            ops.reset(*node().world(), *this);
        }
    }
    auto order = std::move(runtime()->ordered_abilities);
    *runtime() = {};
    runtime()->ordered_abilities = std::move(order);
    for (const auto& a : data->actions)
    {
        runtime()->actions.push_back({a.name});
    }
    return restore_flags(d);
}
bool CharacterController::remap_node_references(
    schema::ComponentDataT& d, std::span<const schema::NodeIdMappingT> mapping) const
{
    auto* value = d.payload.AsCharacterControllerData();
    if (!value)
    {
        return false;
    }
    if (!value->camera_node_id)
    {
        return true;
    }
    for (const auto& m : mapping)
    {
        if (m.source_id == value->camera_node_id)
        {
            if (!m.target_id || m.retired)
            {
                return false;
            }
            value->camera_node_id = m.target_id;
            return true;
        }
    }
    return false;
}
} // namespace engine::plugins::character_controller
