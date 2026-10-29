"""架构规则的独立正反例，不配置或编译引擎。"""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from check_architecture import include_errors, read_targets, target_errors


class ArchitectureRulesTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="engine-architecture-")
        self.root = Path(self.temporary.name).resolve()

    def tearDown(self):
        self.temporary.cleanup()

    def target(self, name, scope=None, links=(), interface_links=(), aliases=None):
        return {
            "name": name,
            "source_directory": str(self.root),
            "sources": [f"src/engine/{scope}/implementation.cpp"] if scope else [],
            "links": list(links),
            "interface_links": list(interface_links),
            "aliases": aliases or {},
        }

    def test_downward_dependencies(self):
        targets = [
            self.target("engine_core", "runtime"),
            self.target("engine_editor", "editor", ["engine_core"]),
            self.target("engine_plugin", "plugins/editor", ["engine_editor"]),
        ]
        self.assertEqual(target_errors(targets, self.root), [])

    def test_actual_alias_and_private_edge(self):
        targets = [
            self.target(
                "engine_core",
                "runtime",
                ["engine::feature"],
                aliases={"engine::feature": "engine_feature"},
            ),
            self.target("engine_feature", "plugins/runtime"),
        ]
        self.assertTrue(
            any(
                "forbidden target dependency" in error
                for error in target_errors(targets, self.root)
            )
        )

    def test_interface_bridge_cannot_hide_plugin(self):
        targets = [
            self.target("engine_editor", "editor", ["engine_bridge"]),
            self.target("engine_bridge", interface_links=["engine_plugin"]),
            self.target("engine_plugin", "plugins/editor"),
        ]
        self.assertTrue(
            any(
                "engine_editor -> engine_bridge -> engine_plugin" in error
                for error in target_errors(targets, self.root)
            )
        )

    def test_runtime_plugin_cannot_depend_on_editor(self):
        targets = [
            self.target("engine_runtime_plugin", "plugins/runtime", ["engine_editor"]),
            self.target("engine_editor", "editor"),
        ]
        self.assertTrue(
            any(
                "forbidden target dependency" in error
                for error in target_errors(targets, self.root)
            )
        )

    def test_mixed_module_sources_fail(self):
        target = self.target("engine_mixed", "runtime")
        target["sources"].append("src/engine/plugins/editor/feature.cpp")
        self.assertTrue(
            any("mixes source scopes" in error for error in target_errors([target], self.root))
        )

    def test_cycle_and_unresolved_expression_fail(self):
        targets = [
            self.target("engine_a", "runtime", ["engine_b", "$<LINK_ONLY:engine_b>"]),
            self.target("engine_b", "runtime", interface_links=["engine_a"]),
        ]
        errors = target_errors(targets, self.root)
        self.assertTrue(any("cycle" in error for error in errors))
        self.assertTrue(any("unevaluated" in error for error in errors))

    def test_public_and_private_source_include_rules(self):
        sources = {
            "include/engine/runtime/invalid.hpp": '#include "editor/api.hpp"\n',
            "src/engine/editor/invalid.cpp": '#include "plugins/editor/feature.hpp"\n',
            "src/engine/plugins/runtime/valid.cpp": '#include "runtime/system/system.hpp"\n',
        }
        for relative, content in sources.items():
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        self.assertEqual(len(include_errors(self.root)), 2)

    def test_cmake_exports_alias_inside_link_only(self):
        for scope in ("runtime", "plugins/runtime"):
            header = self.root / f"src/engine/{scope}/fixture.hpp"
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text("#pragma once\n", encoding="utf-8")
        (self.root / "CMakeLists.txt").write_text(
            """cmake_minimum_required(VERSION 3.25)
project(architecture_fixture LANGUAGES NONE)
add_library(engine_core INTERFACE)
target_sources(engine_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/runtime/fixture.hpp")
add_library(engine_feature INTERFACE)
target_sources(engine_feature INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/plugins/runtime/fixture.hpp")
add_library(engine::feature ALIAS engine_feature)
target_link_libraries(engine_core INTERFACE "$<LINK_ONLY:engine::feature>")
include("${ENGINE_ARCHITECTURE_MODULE}")
engine_export_architecture("${CMAKE_BINARY_DIR}/architecture")
""",
            encoding="utf-8",
        )
        build = self.root / "build"
        module = Path(__file__).resolve().parents[1] / "cmake/core/architecture_checks.cmake"
        configured = subprocess.run(
            [
                "cmake",
                "-S",
                str(self.root),
                "-B",
                str(build),
                "-DCMAKE_BUILD_TYPE=Debug",
                f"-DENGINE_ARCHITECTURE_MODULE={module}",
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
        targets = read_targets(build / "architecture", "Debug")
        core = next(target for target in targets if target["name"] == "engine_core")
        self.assertEqual(core["aliases"]["engine::feature"], "engine_feature")
        self.assertEqual(core["interface_links"], ["engine::feature"])
        self.assertTrue(
            any(
                "engine_core -> engine_feature" in error
                for error in target_errors(targets, self.root)
            )
        )


if __name__ == "__main__":
    unittest.main()
