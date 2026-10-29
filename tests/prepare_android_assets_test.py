"""使用标准库验证 Android 项目资源快照、清单与失败回滚；不下载或安装依赖。"""

import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "prepare_android_assets", ROOT / "scripts/prepare_android_assets.py")
PACK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACK)


class PrepareAndroidAssetsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="engine-android-assets-")
        self.root = Path(self.temporary.name)
        self.project = self.root / "项目 with spaces $literal"
        for name in ("settings", "assets", "cache/assets/scripts"):
            (self.project / name).mkdir(parents=True)
        self.source = self.project / "cache/assets"
        (self.source / "scripts/game.js").write_text("export default class Game {}\n", encoding="utf-8")
        self.output = self.root / "android/assets"
        (self.output / "v8").mkdir(parents=True)
        (self.output / "v8/icudtl.dat").write_bytes(b"ICU data must survive")
        self.compiler = mock.patch.object(PACK, "compile_project", return_value=None)
        self.compiler.start()

    def tearDown(self):
        self.compiler.stop()
        self.temporary.cleanup()

    def prepare(self):
        return PACK.prepare(self.project, self.output)

    def snapshot(self):
        return {path.relative_to(self.output).as_posix(): path.read_bytes()
                for path in self.output.rglob("*") if path.is_file()}

    def assert_clean(self):
        self.assertFalse(any(path.name.startswith(".project-prepare-")
                             for path in self.output.iterdir()))

    def test_manifest_empty_binary_unicode_and_determinism(self):
        (self.source / "空文件.txt").write_bytes(b"")
        (self.source / "binary.bin").write_bytes(b"\0\xff\x80\n")
        manifest = self.prepare()
        self.assertEqual(manifest["entry"], "scripts/game.js")
        self.assertEqual(manifest["deleted"], [])
        self.assertRegex(manifest["revision"], r"^apk-[0-9a-f]{64}$")
        entries = {item["path"]: item for item in manifest["files"]}
        self.assertEqual(entries["空文件.txt"],
                         {"path": "空文件.txt", "size": 0, "hash": "cbf29ce484222325"})
        for path, item in entries.items():
            data = (self.output / "project" / path).read_bytes()
            self.assertEqual(item["size"], len(data))
            expected = 0xCBF29CE484222325
            for byte in data:
                expected = ((expected ^ byte) * 0x100000001B3) % (1 << 64)
            self.assertEqual(item["hash"], format(expected, "016x"))
        self.assertEqual(json.loads((self.output / "project-manifest.json").read_text()), manifest)
        previous = self.snapshot()
        self.assertEqual(self.prepare(), manifest)
        self.assertEqual(self.snapshot(), previous)
        self.assertEqual((self.output / "v8/icudtl.dat").read_bytes(), b"ICU data must survive")
        self.assert_clean()

    def test_update_removes_old_resources_and_changes_revision(self):
        old = self.source / "removed.txt"
        old.write_text("previous", encoding="utf-8")
        first = self.prepare()
        old.unlink()
        (self.source / "added.txt").write_text("new", encoding="utf-8")
        second = self.prepare()
        self.assertNotEqual(first["revision"], second["revision"])
        self.assertFalse((self.output / "project/removed.txt").exists())
        self.assertEqual((self.output / "project/added.txt").read_text(), "new")
        self.assertTrue((self.output / "v8/icudtl.dat").is_file())
        self.assert_clean()

    def test_compile_failure_preserves_previous_output(self):
        self.prepare()
        previous = self.snapshot()
        with mock.patch.object(PACK, "compile_project", side_effect=ValueError("编译失败")):
            with self.assertRaisesRegex(ValueError, "编译失败"):
                self.prepare()
        self.assertEqual(self.snapshot(), previous)
        self.assert_clean()

    def test_limits_reject_before_publishing(self):
        self.prepare()
        previous = self.snapshot()
        for relative, limit in (("large.bin", PACK.MAX_ASSET_SIZE),
                                ("scripts/large.js", PACK.MAX_SCRIPT_SIZE),
                                ("scripts/large.mjs", PACK.MAX_SCRIPT_SIZE)):
            with self.subTest(relative=relative):
                path = self.source / relative
                with path.open("wb") as output:
                    output.truncate(limit + 1)
                with self.assertRaisesRegex(ValueError, "上限"):
                    self.prepare()
                self.assertEqual(self.snapshot(), previous)
                self.assert_clean()
                path.unlink()
        with mock.patch.object(PACK, "MAX_MANIFEST_SIZE", 32):
            with self.assertRaisesRegex(ValueError, "清单超过"):
                self.prepare()
        self.assertEqual(self.snapshot(), previous)
        self.assert_clean()

    def test_missing_entry_and_project_directory_are_rejected(self):
        self.prepare()
        previous = self.snapshot()
        (self.source / "scripts/game.js").unlink()
        with self.assertRaisesRegex(ValueError, "scripts/game.js"):
            self.prepare()
        self.assertEqual(self.snapshot(), previous)
        (self.project / "settings").rmdir()
        with self.assertRaisesRegex(ValueError, "现有普通目录"):
            self.prepare()
        self.assertEqual(self.snapshot(), previous)
        self.assert_clean()

    def test_unsafe_relative_paths_are_rejected(self):
        for path in ("", "/file", "../file", "a/../file", "a/./file", "a//file", "a/",
                     "a\\file", "C:file", "a\0file"):
            with self.subTest(path=path):
                with self.assertRaises(ValueError):
                    PACK.safe_relative(path)

    def test_symlinks_are_rejected_before_publishing(self):
        self.prepare()
        previous = self.snapshot()
        target = self.root / "outside.txt"
        target.write_bytes(b"outside")
        link = self.source / "linked.txt"
        try:
            link.symlink_to(target)
        except (OSError, NotImplementedError):
            self.skipTest("当前环境不允许创建符号链接")
        with self.assertRaisesRegex(ValueError, "符号链接"):
            self.prepare()
        self.assertEqual(self.snapshot(), previous)
        self.assert_clean()
        link.unlink()
        source_link = self.project / "assets/linked.txt"
        source_link.symlink_to(target)
        with self.assertRaisesRegex(ValueError, "符号链接"):
            self.prepare()
        self.assertEqual(self.snapshot(), previous)
        self.assert_clean()

    def test_publish_failure_rolls_back_both_outputs(self):
        self.prepare()
        previous = self.snapshot()
        (self.source / "new.txt").write_bytes(b"new")
        original_rename = Path.rename

        def fail_manifest_publish(path, target):
            if (path.name == "project-manifest.json"
                    and path.parent.name.startswith(".project-prepare-")):
                raise OSError("simulated manifest publication failure")
            return original_rename(path, target)

        with mock.patch.object(Path, "rename", fail_manifest_publish):
            with self.assertRaisesRegex(ValueError, "已恢复上一份输出"):
                self.prepare()
        self.assertEqual(self.snapshot(), previous)
        self.assert_clean()

    def test_output_must_not_overlap_project(self):
        for output in (self.project, self.project / "cache/android", self.root):
            with self.subTest(output=output):
                with self.assertRaisesRegex(ValueError, "相互包含"):
                    PACK.prepare(self.project, output)

    def test_missing_node_and_typescript_have_actionable_errors(self):
        self.compiler.stop()
        with mock.patch.object(PACK.shutil, "which", return_value=None):
            with self.assertRaisesRegex(ValueError, "PATH"):
                PACK.compile_project(self.project)
        with mock.patch.object(PACK.shutil, "which", return_value="node"):
            with self.assertRaisesRegex(ValueError, "npm ci"):
                PACK.compile_project(self.project)

    def test_compiler_uses_argv_without_shell_and_reports_failure(self):
        self.compiler.stop()
        (self.project / "settings/node_modules/typescript").mkdir(parents=True)
        with mock.patch.object(PACK.shutil, "which", return_value="host node"), \
                mock.patch.object(PACK.subprocess, "run",
                                  return_value=subprocess.CompletedProcess([], 1)) as run:
            with self.assertRaisesRegex(ValueError, "TypeScript 编译失败"):
                PACK.compile_project(self.project)
        self.assertEqual(run.call_args.args[0],
                         ["host node", str(ROOT / "scripts/build_scripts.mjs"),
                          "--project", str(self.project)])
        self.assertNotIn("shell", run.call_args.kwargs)


if __name__ == "__main__":
    unittest.main()
