#pragma once

#include "runtime/scene/component.hpp"
#include <memory>

namespace engine
{
class Resource;

class LightProbeVolumeComponent final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::LightProbeVolumeData;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;
    [[nodiscard]] const LightProbeVolumeSettings& settings() const noexcept;
    [[nodiscard]] bool set_settings(const LightProbeVolumeSettings& settings);
    // 体积变换与烘焙位置不一致时返回空；引用独立资源版本。
    [[nodiscard]] std::shared_ptr<const Resource> resource() const noexcept;

private:
    friend class Node;
    explicit LightProbeVolumeComponent(Node& node);
    std::shared_ptr<const Resource> resource_;
};
} // namespace engine
