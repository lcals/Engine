#pragma once
#include "plugins/runtime/character_controller/character_types.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include <algorithm>
#include <map>
#include <tuple>
namespace engine::plugins::character_controller
{
namespace detail
{
using AbilityKey = std::tuple<std::uint64_t, std::uint64_t, std::string>;
template <class T> struct AbilityColumn
{
    std::map<AbilityKey, typename T::Runtime> records;
};
} // namespace detail
class CharacterAbilityRegistry
{
public:
    struct Operations
    {
        std::string type;
        CharacterControlChannels channels;
        bool (*validate)(const schema::CharacterControllerDataT&, std::string_view, std::string&);
        void (*bind)(WorldSystem&);
        void (*reset)(WorldSystem&, Component);
        void (*prune)(WorldSystem&);
        bool (*query)(WorldSystem&, Component, const schema::CharacterControllerDataT&,
                      std::string_view, const CharacterAbilityContext&, const CharacterMotionPlan&,
                      CharacterAbilityRequest&);
        void (*finish)(WorldSystem&, Component, const schema::CharacterControllerDataT&,
                       std::string_view, const CharacterAbilityContext&,
                       const CharacterAbilityDecision&, const CharacterMotorStepResult&, bool&,
                       CharacterAbilityStatus&);
    };
    template <class T> [[nodiscard]] bool add()
    {
        if (frozen_ || find(T::type()) || T::type().empty())
        {
            return false;
        }
        Operations ops;
        ops.type = T::type();
        ops.channels = T::channels();
        ops.validate = [](const auto& d, std::string_view id, std::string& error)
        {
            auto* data = T::find(d, id);
            if (!data)
            {
                error = "missing ability data";
                return false;
            }
            return T::validate(*data, d, error);
        };
        ops.bind = [](WorldSystem& world)
        { flecs::world(world.native_world()).template set<detail::AbilityColumn<T>>({}); };
        ops.reset = [](WorldSystem& world, Component owner)
        {
            auto* column =
                flecs::world(world.native_world()).template try_get_mut<detail::AbilityColumn<T>>();
            if (!column)
            {
                return;
            }
            std::erase_if(column->records,
                          [&](const auto& pair)
                          {
                              return std::get<0>(pair.first) == owner.node().id() &&
                                     std::get<1>(pair.first) == owner.instance_id();
                          });
        };
        ops.prune = [](WorldSystem& world)
        {
            auto* column =
                flecs::world(world.native_world()).template try_get_mut<detail::AbilityColumn<T>>();
            if (!column)
            {
                return;
            }
            std::erase_if(
                column->records,
                [&](const auto& pair)
                {
                    auto c = world.find_node(std::get<0>(pair.first))
                                 .get_component(ComponentType::CharacterControllerData);
                    if (!c || c.instance_id() != std::get<1>(pair.first))
                    {
                        return true;
                    }
                    const auto* stored =
                        flecs::world(world.native_world())
                            .entity(c.node().id())
                            .template try_get<
                                ecs_detail::ComponentStorage<schema::CharacterControllerDataT>>();
                    return !stored || std::none_of(stored->data.abilities.begin(),
                                                   stored->data.abilities.end(),
                                                   [&](const auto& ref)
                                                   {
                                                       return ref.type == T::type() &&
                                                              ref.id == std::get<2>(pair.first) &&
                                                              ref.enabled;
                                                   });
                });
        };
        ops.query = [](WorldSystem& world, Component owner, const auto& definition,
                       std::string_view id, const auto& context, const auto& plan,
                       CharacterAbilityRequest& output)
        {
            output = {};
            const auto* data = T::find(definition, id);
            auto* column =
                flecs::world(world.native_world()).template try_get_mut<detail::AbilityColumn<T>>();
            if (!column || !data)
            {
                return false;
            }
            auto [it, created] = column->records.try_emplace(
                {owner.node().id(), owner.instance_id(), std::string(id)});
            if (created)
            {
                T::reset(it->second);
            }
            return T::query(*data, it->second, context, plan, output);
        };
        ops.finish = [](WorldSystem& world, Component owner, const auto& definition,
                        std::string_view id, const auto& context, const auto& decision,
                        const auto& result, bool& consume, CharacterAbilityStatus& status)
        {
            consume = false;
            status = {};
            const auto* data = T::find(definition, id);
            auto* column =
                flecs::world(world.native_world()).template try_get_mut<detail::AbilityColumn<T>>();
            if (!column || !data)
            {
                return;
            }
            auto& current =
                column->records[{owner.node().id(), owner.instance_id(), std::string(id)}];
            typename T::Runtime next{};
            T::finish(*data, current, context, decision, result, next, consume);
            current = std::move(next);
            T::status(*data, current, status);
            status.selection = decision.result;
        };
        names_.push_back(ops.type);
        operations_.push_back(std::move(ops));
        return true;
    }
    [[nodiscard]] bool freeze() noexcept
    {
        frozen_ = true;
        return true;
    }
    [[nodiscard]] bool frozen() const noexcept
    {
        return frozen_;
    }
    [[nodiscard]] std::span<const std::string> types() const noexcept
    {
        return names_;
    }
    [[nodiscard]] const Operations* find(std::string_view type) const noexcept
    {
        for (const auto& ops : operations_)
        {
            if (ops.type == type)
            {
                return &ops;
            }
        }
        return nullptr;
    }
    [[nodiscard]] std::span<const Operations> operations() const noexcept
    {
        return operations_;
    }

private:
    bool frozen_ = false;
    std::vector<std::string> names_;
    std::vector<Operations> operations_;
};
[[nodiscard]] bool register_builtin_character_abilities(CharacterAbilityRegistry& registry);
[[nodiscard]] bool register_character_components(WorldSystem& world,
                                                 const CharacterAbilityRegistry& registry);
} // namespace engine::plugins::character_controller
