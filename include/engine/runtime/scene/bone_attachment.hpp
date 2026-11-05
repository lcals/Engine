#pragma once

#include "runtime/scene/animator.hpp"

namespace engine
{
struct BoneAttachmentRuntime;
class BoneAttachment final : public Component
{
public:
    using Data = schema::BoneAttachmentDataT;
    using Runtime = BoneAttachmentRuntime;
    static constexpr ComponentType component_type = ComponentType::BoneAttachmentData;
    BoneAttachment() noexcept = default;
    explicit BoneAttachment(Component component) noexcept : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    [[nodiscard]] static bool valid_settings(const Data& data) noexcept;
    [[nodiscard]] Data settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const Data& data);
    [[nodiscard]] bool set_source(Animator source);
    [[nodiscard]] bool detach();
    [[nodiscard]] bool following() const noexcept;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
    [[nodiscard]] bool remap_node_references(schema::ComponentDataT& data,
                                             std::span<const schema::NodeIdMappingT> mapping) const;

private:
    template <class T> friend auto engine::ecs_detail::component_runtime(T component) noexcept;
    [[nodiscard]] Runtime* runtime() const noexcept;
};
} // namespace engine
