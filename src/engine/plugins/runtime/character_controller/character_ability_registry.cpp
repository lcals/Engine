#include "plugins/runtime/character_controller/character_ability_registry.hpp"
#include "plugins/runtime/character_controller/abilities/crouch_ability.hpp"
#include "plugins/runtime/character_controller/abilities/jump_ability.hpp"
#include "plugins/runtime/character_controller/abilities/sprint_ability.hpp"
#include "plugins/runtime/character_controller/character_camera.hpp"
#include "plugins/runtime/character_controller/character_controller_runtime.hpp"
#include "plugins/runtime/character_controller/character_visual.hpp"
namespace engine::plugins::character_controller
{
bool register_builtin_character_abilities(CharacterAbilityRegistry& registry)
{
    return registry.add<SprintAbility>() && registry.add<CrouchAbility>() &&
           registry.add<JumpAbility>();
}
bool register_character_components(WorldSystem& world, const CharacterAbilityRegistry& types)
{
    if (!types.frozen() || !world.native_world())
    {
        return false;
    }
    if (const auto* existing = character_abilities(world))
    {
        return existing == &types;
    }
    auto& r = world.component_registry();
    if (!(r.contains(CharacterController::component_type) || r.add<CharacterController>()) ||
        !(r.contains(CharacterCamera::component_type) || r.add<CharacterCamera>()) ||
        !(r.contains(CharacterVisual::component_type) || r.add<CharacterVisual>()))
    {
        return false;
    }
    flecs::world(world.native_world()).set<CharacterWorldRuntime>({&types});
    for (const auto& ops : types.operations())
    {
        ops.bind(world);
    }
    return true;
}
} // namespace engine::plugins::character_controller
