"""在隔离目录验证模板初始化，不修改仓库中的模板。"""

import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "init_project.py"
SPEC = importlib.util.spec_from_file_location("init_project", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class InitializeProjectTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for directory in ("include/cpp_template", "src", ".github/workflows", "docs", "third_party", "scripts", "build/debug", "references"):
            (self.root / directory).mkdir(parents=True, exist_ok=True)
        self.write("CMakeLists.txt", "project(cpp_template)\noption(CPP_TEMPLATE_BUILD_APP ON)\n")
        self.write("vcpkg.json", '{"name":"cpp-template","dependencies":["fmt"]}\n')
        self.write("include/cpp_template/greeting.hpp", "namespace cpp_template {}\n")
        self.write("src/main.cpp", '#include "cpp_template/greeting.hpp"\n')
        self.write(".github/workflows/ci.yml", "run: cmake -D CPP_TEMPLATE_WARNINGS_AS_ERRORS=ON\n")
        self.write("README.md", "# cpp-template\ncpp_template_app\n")
        self.write("docs/development.md", "target_link_libraries(cpp_template_core PRIVATE example::example)\n")
        self.write("third_party/CMakeLists.txt", "# Link cpp_template_core after adding the dependency.\n")

    def write(self, relative, content):
        (self.root / relative).write_text(content, encoding="utf-8")

    def test_initializes_identifiers_headers_and_workflow(self):
        MODULE.initialize_project(self.root, "my-app2")
        self.assertEqual((self.root / "src/main.cpp").read_text(), '#include "my_app2/greeting.hpp"\n')
        self.assertTrue((self.root / "include/my_app2/greeting.hpp").is_file())
        self.assertFalse((self.root / "include/cpp_template").exists())
        self.assertIn("MY_APP2_BUILD_APP", (self.root / "CMakeLists.txt").read_text())
        self.assertIn("MY_APP2_WARNINGS_AS_ERRORS", (self.root / ".github/workflows/ci.yml").read_text())
        self.assertIn('"name":"my-app2"', (self.root / "vcpkg.json").read_text())
        self.assertEqual((self.root / "README.md").read_text(), "# my-app2\nmy_app2_app\n")
        self.assertIn("my_app2_core", (self.root / "docs/development.md").read_text())
        self.assertIn("my_app2_core", (self.root / "third_party/CMakeLists.txt").read_text())

    def test_rejects_invalid_names_before_changing_files(self):
        original = (self.root / "CMakeLists.txt").read_bytes()
        for name in ("", "MyApp", "../outside", "my_app", "my--app", "1app", "class", "co-await", "std", "con", "lpt1"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                MODULE.initialize_project(self.root, name)
        self.assertEqual((self.root / "CMakeLists.txt").read_bytes(), original)
        self.assertTrue((self.root / "include/cpp_template").is_dir())

    def test_rejects_existing_destination_before_changing_files(self):
        (self.root / "include/my_app").mkdir()
        with self.assertRaises(ValueError):
            MODULE.initialize_project(self.root, "my-app")
        self.assertIn("cpp_template", (self.root / "CMakeLists.txt").read_text())

    def test_leaves_scripts_and_build_artifacts_untouched(self):
        for relative in ("scripts/local.txt", "build/debug/cache.txt", "references/local.txt"):
            self.write(relative, "cpp_template CPP_TEMPLATE cpp-template")
        MODULE.initialize_project(self.root, "my-app")
        for relative in ("scripts/local.txt", "build/debug/cache.txt", "references/local.txt"):
            self.assertEqual((self.root / relative).read_text(), "cpp_template CPP_TEMPLATE cpp-template")

    def test_rejects_second_initialization(self):
        MODULE.initialize_project(self.root, "my-app")
        with self.assertRaises(ValueError):
            MODULE.initialize_project(self.root, "another-app")
        self.assertTrue((self.root / "include/my_app/greeting.hpp").is_file())

    def test_real_template_copy_initializes_every_build_file(self):
        template = SCRIPT.parents[1]
        project_name = json.loads((template / "vcpkg.json").read_text(encoding="utf-8"))["name"]
        if project_name != "cpp-template":
            self.assertTrue((template / "include" / project_name.replace("-", "_")).is_dir())
            self.skipTest("项目已经从模板完成初始化")

        for directory in (".github", "cmake", "docs", "include", "src", "tests", "third_party"):
            shutil.copytree(template / directory, self.root / directory, dirs_exist_ok=True)
        for filename in ("CMakeLists.txt", "CMakePresets.json", "vcpkg.json", "README.md", "AGENTS.md"):
            shutil.copy2(template / filename, self.root / filename)

        MODULE.initialize_project(self.root, "new-game")

        self.assertTrue((self.root / "include/new_game/greeting.hpp").is_file())
        self.assertIn('"name": "new-game"', (self.root / "vcpkg.json").read_text())
        for directory in ("cmake", "docs", "include", "src", "tests", "third_party", ".github"):
            for path in (self.root / directory).rglob("*"):
                if path.is_file():
                    self.assertNotIn("cpp_template", path.read_text(encoding="utf-8"), str(path))
        self.assertNotIn("cpp_template", (self.root / "CMakeLists.txt").read_text())


if __name__ == "__main__":
    unittest.main()
