#include "editor/task/task_system.hpp"
#include "runtime/engine_config.hpp"

#include <atomic>
#include <cstdio>

namespace
{

bool expect(bool value, const char* text)
{
    if (!value)
        std::fprintf(stderr, "task test: %s\n", text);
    return value;
}

} // namespace

int main()
{
    engine::TaskSystem tasks;
    const engine::EngineConfig config;
    if (!expect(!tasks.configure_threads(2) && !tasks.run({}),
                "uninitialized task system rejects execution") ||
        !expect(tasks.initialize(config) && tasks.thread_count() == 4,
                "default four import workers") ||
        !expect(!tasks.configure_threads(0) && !tasks.configure_threads(65),
                "reject invalid worker counts") ||
        !expect(tasks.configure_threads(2) && tasks.thread_count() == 2,
                "idle worker reconfiguration"))
        return 1;
    std::atomic<int> completed{0};
    std::vector<engine::TaskDescription> graph{
        {"first",
         [&]()
         {
             ++completed;
             return !tasks.configure_threads(3);
         },
         {}},
        {"parallel",
         [&]()
         {
             ++completed;
             return true;
         },
         {}},
        {"join", [&]() { return completed.load() == 2; }, {0, 1}}};
    if (!expect(tasks.run(graph),
                "actual Taskflow dependency join and active reconfiguration rejection") ||
        !expect(tasks.thread_count() == 2, "active batch keeps executor"))
        return 1;
    bool downstream_ran = false;
    graph = {{"failure", []() { return false; }, {}},
             {"skipped",
              [&]()
              {
                  downstream_ran = true;
                  return true;
              },
              {0}}};
    if (!expect(!tasks.run(graph) && !downstream_ran, "failure prevents dependent conversion") ||
        !expect(!tasks.run({{"a", []() { return true; }, {1}}, {"b", []() { return true; }, {0}}}),
                "reject cycle") ||
        !expect(!tasks.run({{"bad", []() { return true; }, {9}}}), "reject missing task") ||
        !expect(!tasks.run({{"missing", {}, {}}}), "reject empty function") ||
        !expect(tasks.run({}), "empty graph succeeds"))
        return 1;
    tasks.shutdown();
    tasks.shutdown();
    return expect(tasks.initialize(config) && tasks.run(graph) == false, "executor restarts safely")
               ? 0
               : 1;
}
