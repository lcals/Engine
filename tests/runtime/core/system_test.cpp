#include "runtime/engine_config.hpp"
#include "runtime/system/system.hpp"

#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

bool expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

bool expect_events(const std::vector<std::string>& actual,
                   std::initializer_list<std::string_view> expected)
{
    if (actual.size() != expected.size())
    {
        std::cerr << "event count: expected " << expected.size() << ", got " << actual.size()
                  << '\n';
        return false;
    }

    auto expected_iterator = expected.begin();
    for (const auto& event : actual)
    {
        if (event != *expected_iterator)
        {
            std::cerr << "event: expected " << *expected_iterator << ", got " << event << '\n';
            return false;
        }
        ++expected_iterator;
    }
    return true;
}

class TestSystem final : public engine::System
{
public:
    TestSystem(std::string name, std::vector<std::string>& events)
        : name_(std::move(name)), events_(events)
    {
    }

    ~TestSystem() noexcept override
    {
        shutdown();
    }

    bool fail_initialize = false;
    bool fail_tick = false;
    bool has_resources = false;
    bool reentry_rejected = true;
    engine::System* reentrant_root = nullptr;

protected:
    bool on_initialize(const engine::EngineConfig&) override
    {
        events_.push_back(name_ + ".initialize");
        has_resources = true;
        check_reentry();
        return !fail_initialize;
    }

    bool on_tick(double) override
    {
        events_.push_back(name_ + ".tick");
        check_reentry();
        return !fail_tick;
    }

    void on_shutdown() noexcept override
    {
        events_.push_back(name_ + ".shutdown");
        check_reentry();
        has_resources = false;
    }

private:
    void check_reentry()
    {
        if (reentrant_root == nullptr)
        {
            return;
        }

        const engine::EngineConfig config;
        const auto count_before = events_.size();
        const bool initialized_before = reentrant_root->is_initialized();
        reentry_rejected &= !reentrant_root->initialize(config);
        reentry_rejected &= !reentrant_root->tick(0.0);
        reentrant_root->shutdown();
        reentry_rejected &= reentrant_root->is_initialized() == initialized_before;
        reentry_rejected &= events_.size() == count_before;

        std::unique_ptr<engine::System> candidate = std::make_unique<TestSystem>("late", events_);
        const auto* original = candidate.get();
        reentry_rejected &= !reentrant_root->add_subsystem(candidate);
        reentry_rejected &= !add_subsystem(candidate);
        reentry_rejected &= candidate.get() == original;
    }

    std::string name_;
    std::vector<std::string>& events_;
};

bool ownership_and_lifecycle()
{
    std::vector<std::string> events;
    std::unique_ptr<engine::System> root = std::make_unique<TestSystem>("root", events);
    std::unique_ptr<engine::System> first = std::make_unique<TestSystem>("first", events);
    std::unique_ptr<engine::System> grandchild = std::make_unique<TestSystem>("grandchild", events);
    std::unique_ptr<engine::System> second = std::make_unique<TestSystem>("second", events);
    auto* first_borrow = first.get();
    auto* grandchild_borrow = grandchild.get();
    auto* second_borrow = second.get();
    const engine::EngineConfig config;

    if (!expect(!root->add_subsystem(root), "self attachment must fail") ||
        !expect(root != nullptr, "failed self attachment must preserve ownership") ||
        !expect(first->add_subsystem(grandchild), "attach grandchild") ||
        !expect(root->add_subsystem(first), "attach first child") ||
        !expect(!first_borrow->add_subsystem(root), "ancestor attachment must reject a cycle") ||
        !expect(root != nullptr, "cycle rejection must preserve root ownership") ||
        !expect(root->add_subsystem(second), "attach second child") ||
        !expect(first == nullptr && second == nullptr && grandchild == nullptr,
                "successful attachment must transfer ownership"))
    {
        return false;
    }

    std::unique_ptr<engine::System> empty;
    const auto& const_root = *root;
    bool success = expect(!root->add_subsystem(empty), "empty attachment must fail");
    success &= expect(root->subsystem_count() == 2, "child count must match accepted attachments");
    success &= expect(root->subsystem(0) == first_borrow, "mutable child lookup");
    success &= expect(const_root.subsystem(1) == second_borrow, "const child lookup");
    success &= expect(root->subsystem(2) == nullptr && const_root.subsystem(2) == nullptr,
                      "out of range lookup must return null");
    success &= expect(root->parent() == nullptr && first_borrow->parent() == root.get() &&
                          grandchild_borrow->parent() == first_borrow,
                      "parent links must be borrowed and correct");
    success &= expect(!root->tick(0.0), "tick before initialization must fail");
    success &= expect(!first_borrow->initialize(config), "children must use parent initialization");
    success &= expect(root->initialize(config), "tree initialization");
    success &= expect(root->is_initialized() && first_borrow->is_initialized() &&
                          grandchild_borrow->is_initialized() && second_borrow->is_initialized(),
                      "successful initialization must reach every descendant");
    success &= expect(!root->initialize(config), "duplicate initialization must fail");
    success &= expect(!first_borrow->tick(0.0), "children must use parent tick");
    first_borrow->shutdown();
    success &= expect(first_borrow->is_initialized(), "child shutdown must not bypass its parent");
    success &= expect(root->tick(0.0), "zero delta time is valid");
    root->shutdown();
    root->shutdown();
    success &= expect(!root->is_initialized() && !first_borrow->is_initialized() &&
                          !grandchild_borrow->is_initialized() && !second_borrow->is_initialized(),
                      "shutdown must close every descendant");
    success &= expect_events(
        events, {"root.initialize", "first.initialize", "grandchild.initialize",
                 "second.initialize", "root.tick", "first.tick", "grandchild.tick", "second.tick",
                 "second.shutdown", "grandchild.shutdown", "first.shutdown", "root.shutdown"});
    success &= expect(root->subsystem_count() == 2 && first_borrow->parent() == root.get(),
                      "shutdown must retain the tree structure");
    events.clear();
    success &= expect(root->initialize(config), "closed trees can initialize again");
    root.reset();
    success &=
        expect_events(events, {"root.initialize", "first.initialize", "grandchild.initialize",
                               "second.initialize", "second.shutdown", "grandchild.shutdown",
                               "first.shutdown", "root.shutdown"});
    return success;
}

bool initialization_rollback()
{
    std::vector<std::string> events;
    TestSystem root("root", events);
    std::unique_ptr<engine::System> first = std::make_unique<TestSystem>("first", events);
    std::unique_ptr<engine::System> grandchild = std::make_unique<TestSystem>("grandchild", events);
    auto failing = std::make_unique<TestSystem>("failing", events);
    auto* failing_borrow = failing.get();
    failing->fail_initialize = true;
    std::unique_ptr<engine::System> failing_owner = std::move(failing);
    std::unique_ptr<engine::System> last = std::make_unique<TestSystem>("last", events);
    const engine::EngineConfig config;
    if (!first->add_subsystem(grandchild) || !root.add_subsystem(first) ||
        !root.add_subsystem(failing_owner) || !root.add_subsystem(last))
    {
        return expect(false, "rollback test tree construction");
    }

    bool success = expect(!root.initialize(config), "child initialization failure must propagate");
    root.shutdown();
    success &=
        expect(!root.is_initialized() && !root.has_resources && !failing_borrow->has_resources,
               "failure must clear parent and failing child's partial resources");
    success &=
        expect_events(events, {"root.initialize", "first.initialize", "grandchild.initialize",
                               "failing.initialize", "failing.shutdown", "grandchild.shutdown",
                               "first.shutdown", "root.shutdown"});
    for (std::size_t index = 0; index < root.subsystem_count(); ++index)
    {
        success &= expect(!root.subsystem(index)->is_initialized(),
                          "failed initialization must leave all children stopped");
    }

    events.clear();
    failing_borrow->fail_initialize = false;
    success &= expect(root.initialize(config), "retry after child failure");
    root.shutdown();
    success &= expect_events(events, {"root.initialize", "first.initialize",
                                      "grandchild.initialize", "failing.initialize",
                                      "last.initialize", "last.shutdown", "failing.shutdown",
                                      "grandchild.shutdown", "first.shutdown", "root.shutdown"});

    events.clear();
    root.fail_initialize = true;
    success &= expect(!root.initialize(config), "root initialization failure must propagate");
    root.shutdown();
    success &= expect(!root.has_resources, "failed root must release its partial resources");
    success &= expect_events(events, {"root.initialize", "root.shutdown"});
    return success;
}

bool update_failure_and_invalid_time()
{
    std::vector<std::string> events;
    TestSystem root("root", events);
    auto failing = std::make_unique<TestSystem>("failing", events);
    auto* failing_borrow = failing.get();
    failing->fail_tick = true;
    std::unique_ptr<engine::System> failing_owner = std::move(failing);
    std::unique_ptr<engine::System> last = std::make_unique<TestSystem>("last", events);
    const engine::EngineConfig config;
    if (!root.add_subsystem(failing_owner) || !root.add_subsystem(last) || !root.initialize(config))
    {
        return expect(false, "update test tree construction");
    }

    events.clear();
    bool success = expect(!root.tick(-1.0), "negative delta time must fail");
    success &=
        expect(!root.tick(std::numeric_limits<double>::infinity()), "infinite time must fail");
    success &= expect(!root.tick(std::numeric_limits<double>::quiet_NaN()), "NaN time must fail");
    success &= expect(events.empty(), "invalid time must not reach any hooks");
    success &= expect(!root.tick(0.016), "child update failure must propagate");
    success &= expect_events(events, {"root.tick", "failing.tick"});
    success &= expect(root.is_initialized() && failing_borrow->is_initialized(),
                      "failed frame must not destroy initialized systems");

    events.clear();
    failing_borrow->fail_tick = false;
    success &= expect(root.tick(0.016), "a subsequent update may succeed");
    success &= expect_events(events, {"root.tick", "failing.tick", "last.tick"});
    events.clear();
    root.fail_tick = true;
    success &= expect(!root.tick(0.016), "root update failure must propagate");
    success &= expect_events(events, {"root.tick"});
    return success;
}

bool lifecycle_reentry_and_active_attachment()
{
    std::vector<std::string> events;
    TestSystem root("root", events);
    root.reentrant_root = &root;
    auto child = std::make_unique<TestSystem>("child", events);
    auto* child_borrow = child.get();
    child->reentrant_root = &root;
    std::unique_ptr<engine::System> child_owner = std::move(child);
    std::unique_ptr<engine::System> candidate = std::make_unique<TestSystem>("candidate", events);
    const engine::EngineConfig config;
    if (!root.add_subsystem(child_owner) || !root.initialize(config))
    {
        return expect(false, "reentry test tree construction");
    }

    const auto* original = candidate.get();
    bool success = expect(!root.add_subsystem(candidate), "running root must reject attachment");
    success &= expect(!child_borrow->add_subsystem(candidate),
                      "descendant of running root must reject attachment");
    success &= expect(candidate.get() == original, "rejected attachment must preserve ownership");
    success &= expect(root.tick(0.0), "reentrant attempts must not interrupt the outer update");
    root.shutdown();
    success &= expect(root.reentry_rejected && child_borrow->reentry_rejected,
                      "all lifecycle hooks must reject reentry and tree mutation");
    success &= expect_events(events, {"root.initialize", "child.initialize", "root.tick",
                                      "child.tick", "child.shutdown", "root.shutdown"});

    success &= expect(candidate->initialize(config), "initialize unattached candidate");
    success &= expect(!root.add_subsystem(candidate), "active candidate must reject attachment");
    success &= expect(candidate.get() == original, "candidate rejection preserves ownership");
    candidate->shutdown();
    success &= expect(root.add_subsystem(candidate), "stopped candidate can be attached");
    return success;
}

} // namespace

int main()
{
    int failures = 0;
    failures += !ownership_and_lifecycle();
    failures += !initialization_rollback();
    failures += !update_failure_and_invalid_time();
    failures += !lifecycle_reentry_and_active_attachment();
    return failures == 0 ? 0 : 1;
}
