#pragma once
#include "plugins/runtime/character_controller/character_types.hpp"
namespace engine::plugins::character_controller
{
class SprintAbility
{
public:
    using Data = schema::CharacterSprintDataT;
    struct Runtime
    {
        bool active = false;
    };
    static constexpr std::string_view type()
    {
        return "sprint";
    }
    static constexpr CharacterControlChannels channels()
    {
        return CharacterControlChannels::speed_policy;
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
