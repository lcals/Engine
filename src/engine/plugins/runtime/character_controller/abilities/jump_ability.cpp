#include "plugins/runtime/character_controller/abilities/jump_ability.hpp"
#include "plugins/runtime/character_controller/abilities/ability_validation.hpp"
#include <algorithm>
namespace engine::plugins::character_controller
{
const JumpAbility::Data* JumpAbility::find(const schema::CharacterControllerDataT& d,
                                           std::string_view id)
{
    if (d.ability_data)
    {
        for (const auto& v : d.ability_data->jump)
        {
            if (v.id == id)
            {
                return &v;
            }
        }
    }
    return nullptr;
}
bool JumpAbility::validate(const Data& d, const schema::CharacterControllerDataT& definition,
                           std::string& e)
{
    return (detail::action_exists(definition, d.action) &&
            detail::bounded(d.launch_delta_speed, 0.01, 1000) &&
            detail::bounded(d.coyote_seconds, 0, 1) && detail::bounded(d.hold_seconds, 0, 10) &&
            detail::bounded(d.held_gravity_scale, 0, 16) &&
            detail::bounded(d.released_gravity_scale, 0, 16)) ||
           detail::invalid(e, "invalid jump action or parameters");
}
bool JumpAbility::query(const Data& d, const Runtime& r, const CharacterAbilityContext& c,
                        const CharacterMotionPlan& p, CharacterAbilityRequest& out)
{
    out = {};
    CharacterActionState a;
    if (!c.motor || !c.motor->settings || !c.action(d.action, a))
    {
        return false;
    }
    const auto& before = c.motor->before;
    const bool launch =
        a.has_press && !r.active &&
        (before.grounded || (r.had_ground && !r.used_coyote && r.air_seconds <= d.coyote_seconds &&
                             d.coyote_seconds > 0)) &&
        (d.allow_crouched ||
         (before.active_half_height >= c.motor->settings->half_height - 0.001F &&
          p.half_height >= c.motor->settings->half_height - 0.001F));
    if (launch || r.active)
    {
        out.requested = true;
        out.writes = channels();
        out.values.gravity_scale = a.held && r.held_seconds < d.hold_seconds
                                       ? d.held_gravity_scale
                                       : d.released_gravity_scale;
        if (launch)
        {
            out.values.vertical_impulse = d.launch_delta_speed;
            out.values.detach_support = true;
            out.consume_action = d.action;
            out.press_sequence = a.press_sequence;
        }
    }
    return true;
}
void JumpAbility::finish(const Data& d, const Runtime& r, const CharacterAbilityContext& c,
                         const CharacterAbilityDecision& decision,
                         const CharacterMotorStepResult& result, Runtime& next, bool& consume)
{
    next = r;
    consume = false;
    if (c.motor->before.grounded)
    {
        next.had_ground = true;
        next.air_seconds = 0;
        if (!r.active)
        {
            next.used_coyote = false;
        }
    }
    else
    {
        next.air_seconds += c.motor->delta_time;
    }
    if (decision.result != CharacterAbilitySelection::selected || result.status.control_error)
    {
        next.active = false;
        next.held_seconds = 0;
        next.remaining = 0;
        return;
    }
    if (result.impulse_applied)
    {
        next.active = true;
        next.used_coyote = true;
        next.held_seconds = 0;
        consume = true;
    }
    if (next.active)
    {
        next.held_seconds += c.motor->delta_time;
        CharacterActionState a;
        const bool held = c.action(d.action, a) && a.held;
        if (!held)
        {
            next.held_seconds = std::max(next.held_seconds, static_cast<double>(d.hold_seconds));
        }
        if (result.status.velocity.y() <= 0 || result.status.grounded)
        {
            next.active = false;
        }
    }
    next.remaining = next.active ? std::max(0.0, d.hold_seconds - next.held_seconds) : 0;
}
void JumpAbility::status(const Data& d, const Runtime& r, CharacterAbilityStatus& out)
{
    out = {};
    out.id = d.id;
    out.active = r.active;
    out.phase = r.active ? (r.remaining > 0 ? "holding" : "released") : "idle";
    out.remaining_seconds = r.remaining;
}
} // namespace engine::plugins::character_controller
