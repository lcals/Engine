#include "editor/editor_environment.hpp"
#include "editor/editor_system.hpp"
#include "editor/project/project_system.hpp"
#include "runtime/engine_config.hpp"
#include "runtime/system/system.hpp"

#include <imgui.h>

#include <cstdio>
#include <memory>

namespace
{

struct Calls
{
    int initialize = 0;
    int tick = 0;
    int shutdown = 0;
    bool fail_initialize = true;
};

class EditorChild final : public engine::System
{
public:
    explicit EditorChild(Calls& calls) noexcept : calls_(calls)
    {
    }

    ~EditorChild() noexcept override
    {
        shutdown();
    }

protected:
    [[nodiscard]] bool on_initialize(const engine::EngineConfig&) override
    {
        ++calls_.initialize;
        return !calls_.fail_initialize;
    }

    [[nodiscard]] bool on_tick(double) override
    {
        ++calls_.tick;
        return true;
    }

    void on_shutdown() noexcept override
    {
        ++calls_.shutdown;
    }

private:
    Calls& calls_;
};

[[nodiscard]] bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "editor system test: %s\n", message);
    }
    return condition;
}

[[nodiscard]] bool run_cases(const char* project_root, ImGuiContext* host_context)
{
    const engine::EngineConfig config;
    engine::editor::ProjectSystem project(project_root);
    engine::editor::EditorEnvironment environment(
        project, std::filesystem::path(project_root) / "cache" / "editor-system-test-persistent");
    Calls calls;
    {
        engine::editor::EditorSystem editor(environment);
        std::unique_ptr<engine::System> child = std::make_unique<EditorChild>(calls);
        if (!expect(editor.add_subsystem(child) && !child, "editor owns its child") ||
            !expect(!editor.initialize(config) && !editor.is_initialized(),
                    "uninitialized project must reject editor startup") ||
            !expect(calls.initialize == 0, "project rejection must precede child startup") ||
            !expect(ImGui::GetCurrentContext() == host_context,
                    "preserve host context on rejection") ||
            !expect(project.initialize(config), "initialize valid project") ||
            !expect(environment.initialize(), "initialize editor environment"))
        {
            return false;
        }

        if (!expect(!editor.initialize(config), "child failure must fail editor startup") ||
            !expect(!editor.is_initialized() && project.is_initialized(),
                    "editor failure must preserve borrowed project") ||
            !expect(calls.initialize == 1 && calls.shutdown == 1, "roll back failed child") ||
            !expect(ImGui::GetCurrentContext() == host_context, "rollback preserves host context"))
        {
            return false;
        }

        calls.fail_initialize = false;
        if (!expect(editor.initialize(config), "retry editor after child failure") ||
            !expect(!editor.initialize(config) && editor.is_initialized(),
                    "duplicate initialization preserves ready editor") ||
            !expect(editor.tick(0.0) && calls.tick == 1, "editor advances child") ||
            !expect(ImGui::GetCurrentContext() == host_context, "startup preserves host context"))
        {
            return false;
        }
        editor.shutdown();
        editor.shutdown();
        if (!expect(calls.initialize == 2 && calls.shutdown == 2,
                    "repeated editor shutdown closes child once") ||
            !expect(project.is_initialized(), "editor does not close borrowed project") ||
            !expect(ImGui::GetCurrentContext() == host_context, "shutdown preserves host context"))
        {
            return false;
        }

        project.shutdown();
        if (!expect(!editor.initialize(config),
                    "closed project rejects subsequent editor startup") ||
            !expect(project.initialize(config) && editor.initialize(config),
                    "host can restart project and editor"))
        {
            return false;
        }
    }
    return expect(calls.initialize == 3 && calls.shutdown == 3,
                  "editor destructor closes active child") &&
           expect(project.is_initialized(), "editor destructor preserves borrowed project") &&
           expect(ImGui::GetCurrentContext() == host_context, "destructor preserves host context");
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2 || argv[1][0] == '\0')
    {
        std::fputs("Expected a valid project directory\n", stderr);
        return 2;
    }
    ImGuiContext* host_context = ImGui::CreateContext();
    if (!expect(host_context != nullptr, "create host context"))
    {
        return 1;
    }
    const bool passed = run_cases(argv[1], host_context);
    ImGui::DestroyContext(host_context);
    if (!passed)
    {
        return 1;
    }
    std::puts("Editor project dependency, child rollback, and host context ownership passed");
    return 0;
}
