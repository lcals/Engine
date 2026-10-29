#include "runtime/system/system.hpp"

#include <cmath>
#include <utility>

namespace engine
{

System::System() noexcept = default;

// 派生类析构须先 shutdown()；这里不再调用已经失去派生类型的虚钩子。
System::~System() noexcept = default;

bool System::add_subsystem(std::unique_ptr<System>& subsystem)
{
    if (subsystem == nullptr || subsystem->parent_ != nullptr)
    {
        return false;
    }

    for (const System* ancestor = this; ancestor != nullptr; ancestor = ancestor->parent_)
    {
        if (ancestor->initialized_ || ancestor->executing_)
        {
            return false;
        }
    }

    if (!subsystem->can_attach_to(*this))
    {
        return false;
    }

    System* child = subsystem.get();
    subsystems_.push_back(std::move(subsystem));
    child->parent_ = this;
    return true;
}

System* System::subsystem(std::size_t index) noexcept
{
    return index < subsystems_.size() ? subsystems_[index].get() : nullptr;
}

const System* System::subsystem(std::size_t index) const noexcept
{
    return index < subsystems_.size() ? subsystems_[index].get() : nullptr;
}

std::size_t System::subsystem_count() const noexcept
{
    return subsystems_.size();
}

const System* System::parent() const noexcept
{
    return parent_;
}

bool System::is_initialized() const noexcept
{
    return initialized_;
}

bool System::initialize(const EngineConfig& config)
{
    if (parent_ != nullptr || initialized_ || executing_)
    {
        return false;
    }

    return initialize_tree(config);
}

bool System::tick(double delta_time)
{
    if (parent_ != nullptr || !std::isfinite(delta_time) || delta_time < 0.0)
    {
        return false;
    }

    return tick_tree(delta_time);
}

void System::shutdown() noexcept
{
    if (parent_ != nullptr || executing_)
    {
        return;
    }

    shutdown_tree();
}

bool System::on_tick(double)
{
    return true;
}

bool System::can_attach_to(const System& owner) const noexcept
{
    if (this == &owner || initialized_ || executing_)
    {
        return false;
    }

    for (const auto& child : subsystems_)
    {
        if (!child->can_attach_to(owner))
        {
            return false;
        }
    }

    return true;
}

bool System::initialize_tree(const EngineConfig& config)
{
    if (initialized_ || executing_)
    {
        return false;
    }

    executing_ = true;
    if (!on_initialize(config))
    {
        on_shutdown();
        executing_ = false;
        return false;
    }

    for (const auto& child : subsystems_)
    {
        if (!child->initialize_tree(config))
        {
            for (auto iterator = subsystems_.rbegin(); iterator != subsystems_.rend(); ++iterator)
            {
                (*iterator)->shutdown_tree();
            }

            on_shutdown();
            executing_ = false;
            return false;
        }
    }

    initialized_ = true;
    executing_ = false;
    return true;
}

bool System::tick_tree(double delta_time)
{
    if (!initialized_ || executing_)
    {
        return false;
    }

    executing_ = true;
    if (!on_tick(delta_time))
    {
        executing_ = false;
        return false;
    }

    for (const auto& child : subsystems_)
    {
        if (!child->tick_tree(delta_time))
        {
            executing_ = false;
            return false;
        }
    }

    executing_ = false;
    return true;
}

void System::shutdown_tree() noexcept
{
    if (!initialized_)
    {
        return;
    }

    executing_ = true;
    for (auto iterator = subsystems_.rbegin(); iterator != subsystems_.rend(); ++iterator)
    {
        (*iterator)->shutdown_tree();
    }

    on_shutdown();
    initialized_ = false;
    executing_ = false;
}

} // namespace engine
