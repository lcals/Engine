#pragma once

#include "runtime/performance/profiling.hpp"
#include "runtime/task/task_system.hpp"

// Jolt requires its base definitions before other headers.
// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
// clang-format on

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>

#if defined(ENGINE_ENABLE_TRACY)
#include <tracy/Tracy.hpp>

#include <string>
#include <utility>
#endif

namespace engine::physics_detail
{

// Jolt v5.4.0 JobSystemWithBarrier / JobSystemThreadPool integration pattern.
// Source: https://github.com/jrouwe/JoltPhysics (Jorrit Rouwe, MIT).
// Jolt owns dependencies and barriers; TaskSystem supplies existing oneTBB workers.
class PhysicsJobs final : public JPH::JobSystemWithBarrier
{
public:
    PhysicsJobs() = default;
    ~PhysicsJobs() noexcept override
    {
        wait_for_wrappers();
    }
    PhysicsJobs(const PhysicsJobs&) = delete;
    PhysicsJobs& operator=(const PhysicsJobs&) = delete;
    PhysicsJobs(PhysicsJobs&&) = delete;
    PhysicsJobs& operator=(PhysicsJobs&&) = delete;

    void initialize(JPH::uint max_jobs, JPH::uint max_barriers, TaskSystem* tasks)
    {
        tasks_ = tasks;
        wrapper_capacity_ = max_jobs;
        JPH::JobSystemWithBarrier::Init(max_barriers);
        // A completed job can remain referenced by a queued wrapper across updates.
        // Reserve a second official allocator page for those bounded references; keep
        // the power-of-two page size and allow the task domain to change its idle budget.
        jobs_.Init(tasks_ != nullptr ? max_jobs * 2 : max_jobs, max_jobs);
    }

    int GetMaxConcurrency() const override
    {
        // The calling thread can execute jobs while waiting on Jolt's own barrier.
        return tasks_ != nullptr ? static_cast<int>(tasks_->max_concurrency()) + 1 : 1;
    }

    JobHandle CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
                        JPH::uint32 dependencies = 0) override
    {
#if defined(ENGINE_ENABLE_TRACY)
        if (TracyIsConnected)
        {
            std::string label = "Physics.jolt.";
            label += name != nullptr ? name : "unnamed";
            return create_job(
                name, color,
                [label = std::move(label), function]()
                {
                    ZoneTransientN(zone, label.c_str(), true);
                    function();
                },
                dependencies);
        }
#endif
        return create_job(name, color, function, dependencies);
    }

    void WaitForJobs(Barrier* barrier) override
    {
        ENGINE_PROFILE_ZONE("Physics.barrier");
        JPH::JobSystemWithBarrier::WaitForJobs(barrier);
        // Real jobs are complete. Wrappers that lost Execute's race need only release
        // their references later; their queue latency must not extend each simulation step.
    }

    void wait_for_wrappers() const noexcept
    {
        ENGINE_PROFILE_ZONE("Physics.drain_wrappers");
        auto count = pending_->load(std::memory_order_acquire);
        while (count != 0)
        {
            pending_->wait(count, std::memory_order_acquire);
            count = pending_->load(std::memory_order_acquire);
        }
    }

private:
    JobHandle create_job(const char* name, JPH::ColorArg color, const JobFunction& function,
                         JPH::uint32 dependencies)
    {
        auto index = jobs_.ConstructObject(name, color, this, function, dependencies);
        while (index == JPH::FixedSizeFreeList<Job>::cInvalidObjectIndex)
        {
            // Same bounded storage as the official executor; completed jobs release slots.
            std::this_thread::yield();
            index = jobs_.ConstructObject(name, color, this, function, dependencies);
        }
        auto* job = &jobs_.Get(index);
        JobHandle handle(job);
        if (dependencies == 0)
        {
            QueueJob(job);
        }
        return handle;
    }

    void QueueJob(Job* job) override
    {
        if (tasks_ == nullptr)
        {
            // Jolt's barrier runs ready jobs on the caller when no workers are supplied.
            return;
        }
        // At most one wrapper per available worker, also bounded by the extra job storage.
        // Saturation leaves ready jobs on Jolt's barrier, without creating another queue.
        const auto limit = std::min(tasks_->max_concurrency(), wrapper_capacity_);
        auto count = pending_->load(std::memory_order_relaxed);
        do
        {
            if (count >= limit)
            {
                return;
            }
        } while (!pending_->compare_exchange_weak(count, count + 1, std::memory_order_relaxed));
        job->AddRef();
        const auto execute = [job, pending = pending_]()
        {
            job->Execute();
            job->Release();
            if (pending->fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                pending->notify_all();
            }
        };
        if (!tasks_->enqueue("Physics.jolt", execute, TaskPriority::high))
        {
            // Leave the ready job to its official barrier. Recursive Execute here would
            // nest Jolt's per-job BodyAccess scopes while the parent is still running.
            job->Release();
            if (pending_->fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                pending_->notify_all();
            }
        }
    }

    void QueueJobs(Job** jobs, JPH::uint count) override
    {
        for (JPH::uint index = 0; index < count; ++index)
        {
            QueueJob(jobs[index]);
        }
    }

    void FreeJob(Job* job) override
    {
        jobs_.DestructObject(job);
    }

    TaskSystem* tasks_ = nullptr;
    std::size_t wrapper_capacity_ = 0;
    JPH::FixedSizeFreeList<Job> jobs_;
    // The last wrapper may still notify after the waiter sees zero; it retains this counter.
    std::shared_ptr<std::atomic<std::size_t>> pending_ =
        std::make_shared<std::atomic<std::size_t>>(0);
};

} // namespace engine::physics_detail
