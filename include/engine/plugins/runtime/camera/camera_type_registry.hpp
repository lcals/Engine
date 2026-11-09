#pragma once
#include "plugins/runtime/camera/camera_types.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include <algorithm>
#include <concepts>
#include <map>
#include <tuple>

namespace engine::plugins::camera
{
namespace detail
{
using HistoryKey = std::tuple<std::uint64_t, std::uint64_t, std::string, std::uint64_t>;
template <class T> struct ModuleHistory
{
    struct Record
    {
        typename T::Runtime current{}, next{};
        bool seeded = false, pending = false;
    };
    std::map<HistoryKey, Record> records;
};
} // namespace detail
// Stateless type dispatch; concrete runtime records remain in world-owned typed ECS columns.
class CameraTypeRegistry
{
public:
    struct ModuleOperations
    {
        std::string type;
        CameraModuleStage stage;
        bool (*validate)(const schema::CameraControllerDataT&, std::uint64_t, std::string&);
        void (*bind)(WorldSystem&);
        void (*reset)(WorldSystem&, Node);
        bool (*evaluate)(WorldSystem&, Component, std::string_view, std::uint64_t,
                         const schema::CameraControllerDataT&, const CameraEvaluationContext&,
                         CameraPose&);
        bool (*check)(const schema::CameraControllerDataT&, std::uint64_t,
                      const CameraEvaluationContext&, const CameraPose&, bool&);
        void (*complete)(WorldSystem&, Component, bool);
        void (*prune)(WorldSystem&);
        void (*retain)(WorldSystem&, Component, std::string_view, std::string_view);
    };
    struct ConditionOperations
    {
        std::string type;
        bool (*validate)(const schema::CameraControllerDataT&, std::uint64_t, std::string&);
        bool (*evaluate)(const schema::CameraStateMachineDataT&, std::uint64_t,
                         const CameraEvaluationContext&, CameraConditionMatch&);
    };
    template <class T> [[nodiscard]] bool add_module()
    {
        if (frozen_ || T::type().empty() || module(T::type()))
        {
            return false;
        }
        if constexpr (T::stage() == CameraModuleStage::constraint)
        {
            static_assert(requires(const typename T::Data& d, const CameraEvaluationContext& c,
                                   const CameraPose& p, bool& accepted) {
                { T::check(d, c, p, accepted) } -> std::same_as<bool>;
            });
        }
        ModuleOperations ops;
        ops.type = T::type();
        ops.stage = T::stage();
        ops.validate = [](const auto& definition, std::uint64_t id, std::string& error)
        {
            const auto* data = T::find(definition, id);
            if (!data)
            {
                error = "missing module data";
                return false;
            }
            return T::validate(*data, definition, error);
        };
        ops.bind = [](WorldSystem& world)
        { flecs::world(world.native_world()).template set<detail::ModuleHistory<T>>({}); };
        ops.reset = [](WorldSystem& world, Node node)
        {
            auto* column =
                flecs::world(world.native_world()).template try_get_mut<detail::ModuleHistory<T>>();
            if (column)
            {
                std::erase_if(column->records, [&](const auto& entry)
                              { return std::get<0>(entry.first) == node.id(); });
            }
        };
        ops.evaluate = [](WorldSystem& world, Component component, std::string_view scope,
                          std::uint64_t id, const auto& definition, const auto& context,
                          CameraPose& pose)
        {
            const auto* data = T::find(definition, id);
            auto* column =
                flecs::world(world.native_world()).template try_get_mut<detail::ModuleHistory<T>>();
            if (!data || !column)
            {
                return false;
            }
            auto& record = column->records[{component.node().id(), component.instance_id(),
                                            std::string(scope), id}];
            auto current = record.current;
            if ((!record.seeded || context.cut) && !T::seed(*data, context.previous, current))
            {
                return false;
            }
            record.next = current;
            if (!T::evaluate(*data, current, record.next, context, pose))
            {
                return false;
            }
            record.pending = true;
            return true;
        };
        ops.check = [](const auto& definition, std::uint64_t id, const auto& context,
                       const CameraPose& pose, bool& accepted)
        {
            if constexpr (T::stage() == CameraModuleStage::constraint)
            {
                const auto* data = T::find(definition, id);
                return data && T::check(*data, context, pose, accepted);
            }
            else
            {
                (void)definition;
                (void)id;
                (void)context;
                (void)pose;
                accepted = true;
                return true;
            }
        };
        ops.complete = [](WorldSystem& world, Component component, bool success)
        {
            auto* column =
                flecs::world(world.native_world()).template try_get_mut<detail::ModuleHistory<T>>();
            if (!column)
            {
                return;
            }
            for (auto& [key, record] : column->records)
            {
                if (std::get<0>(key) != component.node().id() ||
                    std::get<1>(key) != component.instance_id())
                {
                    continue;
                }
                if (record.pending && success)
                {
                    record.current = record.next;
                    record.seeded = true;
                }
                record.pending = false;
            }
            if (!success)
            {
                std::erase_if(column->records,
                              [&](const auto& entry)
                              {
                                  return std::get<0>(entry.first) == component.node().id() &&
                                         std::get<1>(entry.first) == component.instance_id() &&
                                         !entry.second.seeded;
                              });
            }
        };
        ops.retain = [](WorldSystem& world, Component component, std::string_view active,
                        std::string_view source)
        {
            auto* column =
                flecs::world(world.native_world()).template try_get_mut<detail::ModuleHistory<T>>();
            if (!column)
            {
                return;
            }
            std::erase_if(column->records,
                          [&](const auto& entry)
                          {
                              const auto& key = entry.first;
                              return std::get<0>(key) == component.node().id() &&
                                     std::get<1>(key) == component.instance_id() &&
                                     !std::get<2>(key).empty() && std::get<2>(key) != active &&
                                     std::get<2>(key) != source;
                          });
        };
        ops.prune = [](WorldSystem& world)
        {
            auto* column =
                flecs::world(world.native_world()).template try_get_mut<detail::ModuleHistory<T>>();
            if (!column)
            {
                return;
            }
            std::erase_if(
                column->records,
                [&](const auto& entry)
                {
                    auto node = world.find_node(std::get<0>(entry.first));
                    auto component = node ? node.get_component(ComponentType::CameraControllerData)
                                          : Component{};
                    return !component || component.instance_id() != std::get<1>(entry.first);
                });
        };
        module_names_.push_back(ops.type);
        modules_.push_back(std::move(ops));
        return true;
    }
    template <class T> [[nodiscard]] bool add_condition()
    {
        if (frozen_ || T::type().empty() || condition(T::type()))
        {
            return false;
        }
        ConditionOperations ops;
        ops.type = T::type();
        ops.validate = [](const auto& definition, std::uint64_t id, std::string& error)
        {
            const auto* data = T::find(definition, id);
            if (!data)
            {
                error = "missing condition data";
                return false;
            }
            return T::validate(*data, definition, error);
        };
        ops.evaluate = [](const auto& definition, std::uint64_t id, const auto& context,
                          CameraConditionMatch& output)
        {
            output = {};
            const auto* data = T::find(definition, id);
            return data && T::evaluate(*data, context, output);
        };
        condition_names_.push_back(ops.type);
        conditions_.push_back(std::move(ops));
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
    [[nodiscard]] std::span<const std::string> module_types() const noexcept
    {
        return module_names_;
    }
    [[nodiscard]] std::span<const std::string> condition_types() const noexcept
    {
        return condition_names_;
    }
    [[nodiscard]] const ModuleOperations* module(std::string_view name) const noexcept;
    [[nodiscard]] const ConditionOperations* condition(std::string_view name) const noexcept;
    [[nodiscard]] std::span<const ModuleOperations> modules() const noexcept
    {
        return modules_;
    }

private:
    bool frozen_ = false;
    std::vector<std::string> module_names_, condition_names_;
    std::vector<ModuleOperations> modules_;
    std::vector<ConditionOperations> conditions_;
};
[[nodiscard]] bool register_camera_components(WorldSystem& world, const CameraTypeRegistry& types);
[[nodiscard]] bool register_builtin_camera_types(CameraTypeRegistry& types);
} // namespace engine::plugins::camera
