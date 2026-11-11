#include "plugins/runtime/character_controller/character_camera_parameter_system.hpp"
#include "plugins/runtime/character_controller/character_camera.hpp"
#include "plugins/runtime/character_controller/character_controller.hpp"
#include "runtime/scene/ecs_storage.hpp"
#include "runtime/world/world_system.hpp"
#include <algorithm>
namespace engine::plugins::character_controller
{
namespace
{
bool defaults(camera::CameraController solver, const std::vector<std::string>& names)
{
    if (!solver)
    {
        return true;
    }
    const auto node = solver.node();
    auto* world = node.world();
    if (!world || world->find_node(node.id()) != node ||
        !flecs::world(world->native_world()).entity(node.id()).has<ecs_detail::Simulating>())
    {
        return true;
    }
    camera::CameraController::Data d;
    if (!solver.get_settings(d))
    {
        return false;
    }
    std::vector<schema::CameraParameterValueT> values;
    for (const auto& p : d.parameters)
    {
        if (p.default_value && std::find(names.begin(), names.end(), p.name) != names.end())
        {
            values.push_back(*p.default_value);
        }
    }
    return values.empty() || solver.set_parameters(values);
}
} // namespace
CharacterCameraParameterSystem::CharacterCameraParameterSystem(WorldSystem& w) noexcept : world_(w)
{
}
CharacterCameraParameterSystem::~CharacterCameraParameterSystem() noexcept
{
    shutdown();
}
bool CharacterCameraParameterSystem::on_initialize(const EngineConfig&)
{
    return world_.native_world() != nullptr;
}
bool CharacterCameraParameterSystem::on_tick(double)
{
    if (!world_.refresh_scene_state())
    {
        return false;
    }
    if (world_.paused())
    {
        return true;
    }
    decltype(written_) next;
    auto query = flecs::world(world_.native_world())
                     .query_builder<const ecs_detail::ComponentStorage<CharacterCamera::Data>>()
                     .with<ecs_detail::Published>()
                     .with<ecs_detail::Simulating>()
                     .build();
    query.each(
        [&](flecs::entity e, const auto& stored)
        {
            auto n = world_.find_node(e.id());
            auto adapter = n.get_component<CharacterCamera>();
            auto solver = n.get_component<camera::CameraController>();
            const auto& d = stored.data;
            Node subject;
            CharacterStatus status;
            CharacterMotor::Data motor;
            if (!adapter.active() || !solver.active() ||
                !solver.get_target(d.subject_slot, subject))
            {
                return;
            }
            auto character = subject.get_component<CharacterController>();
            CharacterController::Data controller;
            if (!character.active() || !character.get_settings(controller) ||
                !character.get_status(status) || !controller.camera_node_id ||
                character.node().scene() != n.scene() || controller.camera_node_id != n.local_id())
            {
                return;
            }
            camera::CameraController::Data definition;
            if (!solver.get_settings(definition))
            {
                return;
            }
            std::vector<schema::CameraParameterValueT> values;
            std::vector<std::string> names;
            for (const auto& binding : d.status_bindings)
            {
                const auto p =
                    std::find_if(definition.parameters.begin(), definition.parameters.end(),
                                 [&](const auto& v) { return v.name == binding.parameter; });
                if (p == definition.parameters.end() || !p->default_value)
                {
                    return;
                }
                auto value = *p->default_value;
                bool ready = status.motor.has_simulated;
                double number = 0;
                bool boolean = false;
                switch (binding.source)
                {
                case schema::CharacterCameraSource::relative_speed:
                    number = status.relative_planar_speed;
                    break;
                case schema::CharacterCameraSource::vertical_speed:
                    number = status.motor.velocity.y();
                    break;
                case schema::CharacterCameraSource::active_height:
                    ready = ready && subject.get_component<CharacterMotor>().get_settings(motor);
                    number = 2 * (status.motor.active_half_height + motor.radius);
                    break;
                case schema::CharacterCameraSource::grounded:
                    boolean = status.motor.grounded;
                    break;
                case schema::CharacterCameraSource::action_held:
                {
                    ready = false;
                    for (const auto& a : status.actions)
                    {
                        if (a.name == binding.source_name)
                        {
                            ready = true;
                            boolean = a.held;
                            break;
                        }
                    }
                    break;
                }
                case schema::CharacterCameraSource::ability_active:
                {
                    ready = false;
                    for (const auto& a : status.abilities)
                    {
                        if (a.id == binding.source_name)
                        {
                            ready = true;
                            boolean = a.active;
                            break;
                        }
                    }
                    break;
                }
                }
                const bool numeric = binding.source < schema::CharacterCameraSource::grounded;
                if (value.type != (numeric ? schema::CameraParameterType::float_value
                                           : schema::CameraParameterType::bool_value))
                {
                    return;
                }
                if (ready)
                {
                    if (numeric)
                    {
                        value.float_value =
                            static_cast<float>(number * binding.scale + binding.offset);
                    }
                    else
                    {
                        value.bool_value = boolean;
                    }
                }
                values.push_back(value);
                names.push_back(binding.parameter);
            }
            // Restore parameters no longer owned by this adapter before publishing its current
            // batch.
            for (const auto& old : written_)
            {
                if (old.first == solver)
                {
                    std::vector<std::string> removed;
                    for (const auto& name : old.second)
                    {
                        if (std::find(names.begin(), names.end(), name) == names.end())
                        {
                            removed.push_back(name);
                        }
                    }
                    if (!defaults(solver, removed))
                    {
                        return;
                    }
                }
            }
            if (solver.set_parameters(values))
            {
                next.emplace_back(solver, std::move(names));
            }
        });
    for (const auto& old : written_)
    {
        if (std::none_of(next.begin(), next.end(),
                         [&](const auto& item) { return item.first == old.first; }))
        {
            if (!defaults(old.first, old.second))
            {
                return false;
            }
        }
    }
    written_ = std::move(next);
    return true;
}
void CharacterCameraParameterSystem::on_shutdown() noexcept
{
    for (const auto& record : written_)
    {
        static_cast<void>(defaults(record.first, record.second));
    }
    written_.clear();
}
} // namespace engine::plugins::character_controller
