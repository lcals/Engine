"""在隔离 PATH 中验证宿主安装脚本；不调用真实包管理器。"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "setup.sh"
BASH = shutil.which("bash")


@unittest.skipUnless(BASH and os.name != "nt", "requires a Unix bash interpreter")
class SetupTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.log = self.root / "calls"
        self.environment = dict(os.environ, PATH=str(self.bin), MOCK_ROOT=str(self.root),
                                MOCK_LOG=str(self.log), MOCK_PLATFORM="Linux")
        self.executable("uname", 'printf "%s\\n" "$MOCK_PLATFORM"')
        self.executable("id", 'printf "0\\n"')
        self.executable("cat", '/bin/cat "$@"')
        for name, version in (("cmake", "cmake version 3.25.0"), ("git", "git version 2.40"),
                              ("ninja", "1.11.0"), ("ccache", "ccache version 4.10"),
                              ("sccache", "sccache 0.10.0")):
            (self.root / ("version-" + name)).write_text(version)
            self.executable(name, f'''printf '{name} %s\\n' "$*" >> "$MOCK_LOG"
[[ -f "$MOCK_ROOT/ready-{name}" ]] || exit 1
printf '%s\\n' "$(<"$MOCK_ROOT/version-{name}")"''')
            self.ready(name)

    def executable(self, name, body):
        path = self.bin / name
        path.write_text("#!/bin/bash\n" + body + "\n")
        path.chmod(0o755)

    def ready(self, name, enabled=True):
        marker = self.root / ("ready-" + name)
        if enabled:
            marker.touch()
        else:
            marker.unlink(missing_ok=True)

    def manager(self, name):
        self.executable(name, '''printf 'manager %s\\n' "$*" >> "$MOCK_LOG"
[[ "$MOCK_FAIL" != yes ]] || exit 17
if [[ "$1" == --prefix ]]; then printf '%s\\n' "$MOCK_ROOT"; exit 0; fi
if [[ "$1" == list ]]; then exit 1; fi
[[ "$MOCK_NO_INSTALL" != yes ]] || exit 0
for argument in "$@"; do
    case "$argument" in
        ninja-build) : > "$MOCK_ROOT/ready-ninja" ;;
        cmake|git|ninja|ccache|sccache) : > "$MOCK_ROOT/ready-$argument" ;;
    esac
done''')

    def run_setup(self, *arguments):
        return subprocess.run([BASH, str(SCRIPT), *arguments], env=self.environment,
                              capture_output=True, text=True, timeout=10)

    def calls(self):
        return self.log.read_text() if self.log.exists() else ""

    def test_ready_install_does_not_require_a_package_manager(self):
        result = self.run_setup("--install")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("manager", self.calls())

    def test_default_check_never_installs_missing_tools(self):
        self.ready("ccache", False)
        self.manager("apt-get")
        result = self.run_setup()
        self.assertEqual(result.returncode, 1)
        self.assertNotIn("manager", self.calls())

    def test_install_only_requests_missing_packages_then_verifies(self):
        self.ready("ninja", False)
        self.ready("ccache", False)
        self.manager("apt-get")
        result = self.run_setup("--install")
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = self.calls()
        self.assertIn("manager install -y --no-install-recommends ninja-build ccache", calls)
        self.assertNotIn("manager install -y --no-install-recommends cmake", calls)
        self.assertGreaterEqual(calls.count("ninja --version"), 2)

    def test_missing_package_manager_reports_failure(self):
        self.ready("ninja", False)
        self.assertEqual(self.run_setup("--install").returncode, 2)

    def test_install_failure_is_not_success(self):
        self.ready("ninja", False)
        self.manager("apt-get")
        self.environment["MOCK_FAIL"] = "yes"
        self.assertNotEqual(self.run_setup("--install").returncode, 0)

    def test_successful_installer_still_requires_working_tools(self):
        self.ready("ninja", False)
        self.manager("apt-get")
        self.environment["MOCK_NO_INSTALL"] = "yes"
        self.assertEqual(self.run_setup("--install").returncode, 1)

    def test_old_cmake_is_rejected_even_after_package_installation(self):
        (self.root / "version-cmake").write_text("cmake version 3.24.4")
        self.manager("apt-get")
        self.assertEqual(self.run_setup("--install").returncode, 1)

    def test_cache_selection_does_not_require_both_tools(self):
        self.ready("ccache", False)
        self.assertEqual(self.run_setup("--cache", "sccache").returncode, 0)
        self.ready("sccache", False)
        self.assertEqual(self.run_setup("--cache", "none").returncode, 0)

    def test_macos_uses_homebrew_and_checks_result(self):
        self.environment["MOCK_PLATFORM"] = "Darwin"
        self.ready("ccache", False)
        self.manager("brew")
        result = self.run_setup("--install")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("manager install ccache", self.calls())

    def test_linux_managers_use_native_ninja_package_names(self):
        for manager, expected in (("dnf", "install -y ninja-build"),
                                  ("pacman", "-S --needed --noconfirm ninja")):
            with self.subTest(manager=manager):
                self.ready("ninja", False)
                self.manager(manager)
                result = self.run_setup("--install")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("manager " + expected, self.calls())
                (self.bin / manager).unlink()

    def test_argument_errors_do_not_install(self):
        for arguments in (("--check", "--install"), ("--cache", "unknown"), ("--cache",)):
            with self.subTest(arguments=arguments):
                self.assertEqual(self.run_setup(*arguments).returncode, 2)
        self.assertNotIn("manager", self.calls())


if __name__ == "__main__":
    unittest.main()
