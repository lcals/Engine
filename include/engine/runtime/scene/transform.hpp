#pragma once

#include "runtime/scene/component.hpp"

namespace engine
{

class Transform final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::TransformData;
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;

    [[nodiscard]] const TransformSettings& settings() const noexcept;
    [[nodiscard]] bool set_settings(const TransformSettings& settings) noexcept;
    // 输出 16 个列主序 float；失败时非空输出保持单位矩阵。
    [[nodiscard]] bool world_matrix(float* output) const noexcept;

private:
    friend class Node;
    explicit Transform(Node& node);
};

} // namespace engine
