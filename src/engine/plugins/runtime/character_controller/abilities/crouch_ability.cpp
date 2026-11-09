#include "plugins/runtime/character_controller/abilities/crouch_ability.hpp"
#include "plugins/runtime/character_controller/abilities/ability_validation.hpp"
namespace engine::plugins::character_controller
{
const CrouchAbility::Data* CrouchAbility::find(const schema::CharacterControllerDataT& d,
                                               std::string_view id)
{
    if (d.ability_data)
    {
        for (const auto& v : d.ability_data->crouch)
        {
            if (v.id == id)
            {
                return &v;
            }
        }
    }
    return nullptr;
}
bool CrouchAbility::validate(const Data& d, const schema::CharacterControllerDataT& definition,
                             std::string& e)
{
    return (detail::action_exists(definition, d.action) && detail::bounded(d.speed, 0, 1000) &&
            detail::bounded(d.half_height, 0.01, 1000)) ||
           detail::invalid(e, "invalid crouch action, speed or height");
}
bool CrouchAbility::query(const Data& d, const Runtime&, const CharacterAbilityContext& c,
                          const CharacterMotionPlan&, CharacterAbilityRequest& out)
{
    out = {};
    CharacterActionState a;
    if (!c.motor || !c.motor->settings || !c.action(d.action, a) ||
        d.half_height >= c.motor->settings->half_height)
    {
        return false;
    }
    if (a.held || c.motor->before.active_half_height < c.motor->settings->half_height - 0.001F)
    {
        out.requested = true;
        out.writes = channels();
        out.values.max_speed = d.speed;
        out.values.half_height = a.held ? d.half_height : c.motor->settings->half_height;
    }
    return true;
}
void CrouchAbility::finish(const Data&, const Runtime&, const CharacterAbilityContext& c,
                           const CharacterAbilityDecision& d, const CharacterMotorStepResult& r,
                           Runtime& next, bool& consume)
{
    consume = false;
    next.active = d.result == CharacterAbilitySelection::selected && !r.status.control_error &&
                  r.status.active_half_height < c.motor->settings->half_height - 0.001F;
    next.blocked = next.active && r.status.shape_result == CharacterShapeResult::blocked;
}
void CrouchAbility::status(const Data& d, const Runtime& r, CharacterAbilityStatus& out)
{
    out = {};
    out.id = d.id;
    out.active = r.active;
    out.phase = r.blocked ? "blocked" : r.active ? "crouched" : "standing";
}
} // namespace engine::plugins::character_controller
