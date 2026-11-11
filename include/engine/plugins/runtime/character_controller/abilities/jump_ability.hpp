#pragma once
#include "plugins/runtime/character_controller/character_types.hpp"
namespace engine::plugins::character_controller
{
class JumpAbility
{
public:
    using Data = schema::CharacterJumpDataT;
    struct Runtime
    {
        bool active = false;
        bool used_coyote = false;
        bool had_ground = false;
        double air_seconds = 0;
        double held_seconds = 0;
        double remaining = 0;
    };
    static constexpr std::string_view type()
    {
        return "jump";
    }
    static constexpr CharacterControlChannels channels()
    {
        return CharacterControlChannels::vertical_motion;
    }
    static const Data* find(const schema::CharacterControllerDataT& definition,
                            std::string_view id);
    [[nodiscard]] static bool validate(const Data&, const schema::CharacterControllerDataT&,
                                       std::string& error);
    static void reset(Runtime& output)
    {
        output = {};
    }
    [[nodiscard]] static bool query(const Data&, const Runtime&, const CharacterAbilityContext&,
                                    const CharacterMotionPlan&, CharacterAbilityRequest& output);
    static void finish(const Data&, const Runtime&, const CharacterAbilityContext&,
                       const CharacterAbilityDecision&, const CharacterMotorStepResult&,
                       Runtime& next, bool& consume_press);
    static void status(const Data&, const Runtime&, CharacterAbilityStatus& output);
};
} // namespace engine::plugins::character_controller
