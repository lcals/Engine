#pragma once

#include "runtime/scene/animator.hpp"

namespace engine
{
struct BoneAttachmentRuntime
{
    Animator source;
    std::uint64_t skeleton_generation = 0;
    std::uint64_t source_pose_revision = 0;
    std::uint32_t resolved_socket_index = 0;
    bool following = false;
    bool binding_pending = false;
};
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
    [[nodiscard]] Runtime* runtime() const noexcept;
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
};
} // namespace engine
