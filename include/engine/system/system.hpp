#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace engine
{

struct EngineConfig;

class System
{
public:
    virtual ~System() noexcept;

    System(const System&) = delete;
    System& operator=(const System&) = delete;
    System(System&&) = delete;
    System& operator=(System&&) = delete;

    // 成功后移交所有权；失败时 subsystem 及当前系统树保持不变。
    [[nodiscard]] bool add_subsystem(std::unique_ptr<System>& subsystem);

    // 返回借用，越界为空；有效期不超过所属系统树。
    [[nodiscard]] System* subsystem(std::size_t index) noexcept;
    [[nodiscard]] const System* subsystem(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t subsystem_count() const noexcept;
    [[nodiscard]] const System* parent() const noexcept;
    [[nodiscard]] bool is_initialized() const noexcept;

    // 根系统由宿主调度；附属子系统只接受父系统内部的递归调度。
    [[nodiscard]] bool initialize(const EngineConfig& config);
    [[nodiscard]] bool tick(double delta_time);
    void shutdown() noexcept;

protected:
    System() noexcept;

    [[nodiscard]] virtual bool on_initialize(const EngineConfig& config) = 0;
    [[nodiscard]] virtual bool on_tick(double delta_time);
    virtual void on_shutdown() noexcept = 0;

private:
    [[nodiscard]] bool can_attach_to(const System& owner) const noexcept;
    [[nodiscard]] bool initialize_tree(const EngineConfig& config);
    [[nodiscard]] bool tick_tree(double delta_time);
    void shutdown_tree() noexcept;

    std::vector<std::unique_ptr<System>> subsystems_;
    System* parent_ = nullptr;
    bool initialized_ = false;
    bool executing_ = false;
};

} // namespace engine
