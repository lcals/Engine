#pragma once

#include "runtime/navigation/navigation_bounds.hpp"
#include "runtime/navigation/navigation_geometry.hpp"
#include "runtime/navigation/navigation_settings.hpp"
#include "runtime/system/system.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace engine
{

class NavigationSystem final : public System
{
public:
    NavigationSystem() noexcept;
    ~NavigationSystem() noexcept override;

    // 主线程同步安全点；几何只借用到返回，不扫描场景，不与查询/修改并发。
    // 完整构建失败清空旧网格，避免误用过期几何。
    [[nodiscard]] bool build(std::span<const NavigationGeometry> geometry,
                             const NavigationBounds& bounds, const NavigationSettings& settings);
    // 提交全部当前几何；dirty 须覆盖旧位置及新位置，只重建交叠 tile（含必要边界）。
    // 失败移除受影响 tile 并递增 revision；非法 dirty 无法定位时清空全部。
    [[nodiscard]] bool replace(std::span<const NavigationGeometry> geometry,
                               const NavigationBounds& dirty);
    // 只返回完整可达路径；失败 output 为空，端点投影范围由 settings 指定。
    [[nodiscard]] bool find_path(const Vec3& start, const Vec3& end,
                                 std::vector<Vec3>& output) const;
    void clear() noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] std::size_t rebuilt_tiles() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace engine
