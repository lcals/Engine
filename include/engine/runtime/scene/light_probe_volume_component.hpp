#pragma once

#include "runtime/resource/resource_handle.hpp"

#include "runtime/scene/component.hpp"
#include <memory>

namespace engine
{
class Resource;

struct LightProbeVolumeComponentRuntime;

class LightProbeVolumeComponent final : public Component
{
public:
    using Data = schema::LightProbeVolumeDataT;
    LightProbeVolumeComponent() noexcept = default;
    explicit LightProbeVolumeComponent(Component component) noexcept
        : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    using Runtime = LightProbeVolumeComponentRuntime;
    static constexpr ComponentType component_type = ComponentType::LightProbeVolumeData;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const LightProbeVolumeSettings& settings);
    // 体积变换与烘焙位置不一致时返回空；引用独立资源版本。
    [[nodiscard]] ResourceHandle resource() const noexcept;

private:
    template <class T> friend auto engine::ecs_detail::component_runtime(T component) noexcept;
    [[nodiscard]] Runtime* runtime() const noexcept;
};
} // namespace engine
