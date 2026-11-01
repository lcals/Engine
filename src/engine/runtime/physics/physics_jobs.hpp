#pragma once

// Jolt requires its base definitions before other headers.
// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
// clang-format on

#if defined(ENGINE_ENABLE_TRACY)
#include <tracy/Tracy.hpp>

#include <cstdlib>
#include <string>
#include <utility>
#endif

namespace engine::physics_detail
{

#if defined(ENGINE_ENABLE_TRACY)
// Every actual Job/Barrier belongs to the official executor; this only decorates its callback.
class PhysicsJobs final : public JPH::JobSystem
{
public:
    PhysicsJobs() = default;
    ~PhysicsJobs() noexcept override = default;
    PhysicsJobs(const PhysicsJobs&) = delete;
    PhysicsJobs& operator=(const PhysicsJobs&) = delete;
    PhysicsJobs(PhysicsJobs&&) = delete;
    PhysicsJobs& operator=(PhysicsJobs&&) = delete;

    void Init(JPH::uint max_jobs)
    {
        jobs_.Init(max_jobs);
    }

    int GetMaxConcurrency() const override
    {
        return jobs_.GetMaxConcurrency();
    }

    JobHandle CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
                        JPH::uint32 dependencies = 0) override
    {
        if (!TracyIsConnected)
        {
            return jobs_.CreateJob(name, color, function, dependencies);
        }
        std::string label = "Physics.jolt.";
        label += name != nullptr ? name : "unnamed";
        return jobs_.CreateJob(
            name, color,
            [label = std::move(label), function]()
            {
                // Transient source locations copy the dynamic name; no static pointer borrows it.
                // Dependency removal can run nested jobs here; use self time for phase totals.
                ZoneTransientN(zone, label.c_str(), true);
                function();
            },
            dependencies);
    }

    Barrier* CreateBarrier() override
    {
        return jobs_.CreateBarrier();
    }

    void DestroyBarrier(Barrier* barrier) override
    {
        jobs_.DestroyBarrier(barrier);
    }

    void WaitForJobs(Barrier* barrier) override
    {
        jobs_.WaitForJobs(barrier);
    }

private:
    // Jobs are constructed by jobs_, so their protected owner callbacks can never target us.
    // Do not try to forward these protected methods or silently ignore a broken owner invariant.
    [[noreturn]] void QueueJob(Job*) override
    {
        std::abort();
    }

    [[noreturn]] void QueueJobs(Job**, JPH::uint) override
    {
        std::abort();
    }

    [[noreturn]] void FreeJob(Job*) override
    {
        std::abort();
    }

    JPH::JobSystemSingleThreaded jobs_;
};
#else
using PhysicsJobs = JPH::JobSystemSingleThreaded;
#endif

} // namespace engine::physics_detail
