#include "plugins/runtime/character_controller/abilities/sprint_ability.hpp"
#include "plugins/runtime/character_controller/abilities/ability_validation.hpp"
namespace engine::plugins::character_controller
{
const SprintAbility::Data* SprintAbility::find(const schema::CharacterControllerDataT& d,
                                               std::string_view id)
{
    if (d.ability_data)
    {
        for (const auto& v : d.ability_data->sprint)
        {
            if (v.id == id)
            {
                return &v;
            }
        }
    }
    return nullptr;
}
bool SprintAbility::validate(const Data& d, const schema::CharacterControllerDataT& definition,
                             std::string& e)
{
    return (detail::action_exists(definition, d.action) && detail::bounded(d.speed, 0, 1000)) ||
           detail::invalid(e, "invalid sprint action or speed");
}
bool SprintAbility::query(const Data& d, const Runtime&, const CharacterAbilityContext& c,
                          const CharacterMotionPlan& p, CharacterAbilityRequest& out)
{
    out = {};
    CharacterActionState a;
    if (!c.motor || !c.motor->settings || !c.action(d.action, a))
    {
        return false;
    }
    if (a.held && c.motor->before.grounded && c.move_amount > 0 &&
        c.motor->before.active_half_height >= c.motor->settings->half_height - 0.001F &&
        p.half_height >= c.motor->settings->half_height - 0.001F)
    {
        out.requested = true;
        out.writes = channels();
        out.values.max_speed = d.speed;
    }
    return true;
}
void SprintAbility::finish(const Data&, const Runtime&, const CharacterAbilityContext&,
                           const CharacterAbilityDecision& d, const CharacterMotorStepResult& r,
                           Runtime& next, bool& consume)
{
    consume = false;
    next.active = d.result == CharacterAbilitySelection::selected && !r.status.control_error;
}
void SprintAbility::status(const Data& d, const Runtime& r, CharacterAbilityStatus& out)
{
    out = {};
    out.id = d.id;
    out.active = r.active;
    out.phase = r.active ? "sprinting" : "idle";
}
} // namespace engine::plugins::character_controller
