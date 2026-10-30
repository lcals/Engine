#include "runtime/engine.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/system/system.hpp"

#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

namespace
{

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "%s\n", message);
    }
    return condition;
}

class RecordingSystem final : public engine::System
{
public:
    RecordingSystem(int id, std::vector<int>& events) : id_(id), events_(events)
    {
    }

    ~RecordingSystem() noexcept override
    {
        shutdown();
        events_.push_back(400 + id_);
    }

    bool fail_initialize = false;
    bool fail_tick = false;
    bool check_reentrancy = false;

protected:
    bool on_initialize(const engine::EngineConfig& config) override
    {
        events_.push_back(100 + id_);
        if (check_reentrancy)
        {
            auto& framework = engine::Engine::instance();
            std::unique_ptr<engine::System> candidate =
                std::make_unique<RecordingSystem>(9, events_);
            if (framework.initialize(config) || framework.tick(0.0) ||
                framework.add_system(candidate) || candidate == nullptr)
            {
                return false;
            }
            framework.shutdown();
        }
        return !fail_initialize;
    }

    bool on_tick(double) override
    {
        events_.push_back(200 + id_);
        return !fail_tick;
    }

    void on_shutdown() noexcept override
    {
        events_.push_back(300 + id_);
    }

private:
    int id_;
    std::vector<int>& events_;
};

bool check_framework()
{
    auto& framework = engine::Engine::instance();
    const engine::EngineConfig config;
    framework.shutdown();
    std::vector<int> events;
    std::unique_ptr<engine::System> first = std::make_unique<RecordingSystem>(1, events);
    std::unique_ptr<engine::System> second = std::make_unique<RecordingSystem>(2, events);
    auto* second_system = static_cast<RecordingSystem*>(second.get());
    std::unique_ptr<engine::System> empty;
    if (!expect(!framework.initialize(config) && !framework.tick(0.0) &&
                    !framework.add_system(empty),
                "empty framework must reject initialization, update and empty ownership") ||
        !expect(framework.add_system(first) && first == nullptr && framework.add_system(second) &&
                    second == nullptr && framework.system_count() == 2,
                "registration transfers root ownership") ||
        !expect(framework.initialize(config) && framework.is_initialized() &&
                    !framework.initialize(config),
                "initialize roots once") ||
        !expect(events == std::vector<int>{101, 102}, "roots initialize in registration order"))
    {
        framework.shutdown();
        return false;
    }
    std::unique_ptr<engine::System> rejected = std::make_unique<RecordingSystem>(3, events);
    if (!expect(!framework.add_system(rejected) && rejected != nullptr && !framework.tick(-1.0) &&
                    !framework.tick(std::numeric_limits<double>::quiet_NaN()) &&
                    framework.tick(0.0),
                "reject mutation and invalid durations without breaking a healthy frame"))
    {
        framework.shutdown();
        return false;
    }
    second_system->fail_tick = true;
    const bool failed = !framework.tick(0.0);
    const auto after_failure = events;
    if (!expect(failed && !framework.tick(0.0) && events == after_failure,
                "system failure stops subsequent frames until shutdown"))
    {
        framework.shutdown();
        return false;
    }
    events.clear();
    framework.shutdown();
    framework.shutdown();
    if (!expect(events == std::vector<int>{302, 402, 301, 401} && !framework.is_initialized() &&
                    framework.system_count() == 0,
                "shutdown destroys roots in reverse order and is idempotent"))
    {
        return false;
    }
    rejected.reset();
    events.clear();
    first = std::make_unique<RecordingSystem>(1, events);
    second = std::make_unique<RecordingSystem>(2, events);
    static_cast<RecordingSystem*>(first.get())->check_reentrancy = true;
    static_cast<RecordingSystem*>(second.get())->fail_initialize = true;
    if (!expect(framework.add_system(first) && framework.add_system(second) &&
                    !framework.initialize(config) && framework.system_count() == 0,
                "initialization failure rolls back every owned root") ||
        !expect(events == std::vector<int>{101, 409, 102, 302, 402, 301, 401},
                "callbacks cannot reenter or mutate framework lifecycle"))
    {
        framework.shutdown();
        return false;
    }
    first = std::make_unique<RecordingSystem>(1, events);
    const bool recovered =
        framework.add_system(first) && framework.initialize(config) && framework.tick(0.01);
    framework.shutdown();
    return expect(recovered, "framework can be assembled again after rollback");
}

bool check_frame_rate()
{
    auto& framework = engine::Engine::instance();
    framework.shutdown();
    if (!expect(framework.target_frame_rate() == -1 && framework.set_target_frame_rate(45) &&
                    framework.target_frame_rate() == 45 && !framework.set_target_frame_rate(0) &&
                    !framework.set_target_frame_rate(-2) && framework.target_frame_rate() == 45,
                "frame limit defaults to unlimited and rejects invalid rates without mutation"))
    {
        framework.shutdown();
        return false;
    }
    std::vector<int> events;
    std::unique_ptr<engine::System> system = std::make_unique<RecordingSystem>(1, events);
    engine::EngineConfig config;
    config.target_frame_rate = 0;
    if (!expect(framework.add_system(system) && !framework.initialize(config) && events.empty() &&
                    framework.system_count() == 1 && framework.target_frame_rate() == 45,
                "invalid configured rate rejects initialization before starting systems"))
    {
        framework.shutdown();
        return false;
    }
    config.target_frame_rate = 60;
    const bool initialized = framework.initialize(config) && framework.target_frame_rate() == 60;
    framework.begin_frame();
    framework.shutdown();
    framework.end_frame();
    return expect(initialized && framework.target_frame_rate() == -1,
                  "configuration sets the frame limit and shutdown cancels pending frame pacing");
}

} // namespace

int main()
{
    return check_framework() && check_frame_rate() ? 0 : 1;
}
