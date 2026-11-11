#pragma once
#include "plugins/runtime/camera/camera_types.hpp"
namespace engine::plugins::camera
{
class CameraTypeRegistry;
struct CameraControllerRuntime;
class CameraController final : public Component
{
public:
    using Data = schema::CameraControllerDataT;
    using Runtime = CameraControllerRuntime;
    static constexpr ComponentType component_type = ComponentType::CameraControllerData;
    CameraController() noexcept = default;
    explicit CameraController(Component c) noexcept : Component(c, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    [[nodiscard]] static bool valid_settings(const Data&, const CameraTypeRegistry&,
                                             std::string& error);
    [[nodiscard]] bool get_settings(Data& output) const;
    [[nodiscard]] bool set_settings(const Data&, std::string& error);
    [[nodiscard]] bool get_parameter(std::string_view name,
                                     schema::CameraParameterValueT& output) const;
    [[nodiscard]] bool set_parameters(std::span<const schema::CameraParameterValueT> values);
    [[nodiscard]] bool trigger(std::string_view name);
    [[nodiscard]] bool reset_trigger(std::string_view name);
    [[nodiscard]] bool set_target(std::string_view slot, Node target);
    [[nodiscard]] bool clear_target_override(std::string_view slot);
    [[nodiscard]] bool get_target(std::string_view slot, Node& output) const;
    [[nodiscard]] bool get_status(CameraStatus& output) const;
    [[nodiscard]] bool reset();
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
    [[nodiscard]] bool remap_node_references(schema::ComponentDataT&,
                                             std::span<const schema::NodeIdMappingT>) const;

private:
    template <class T> friend auto engine::ecs_detail::component_runtime(T component) noexcept;
    [[nodiscard]] Runtime* runtime() const noexcept;
};
} // namespace engine::plugins::camera
