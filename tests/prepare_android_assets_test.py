"""使用标准库验证 Android 项目资源快照、清单与失败回滚；不下载或安装依赖。"""

import importlib.util
import json
from pathlib import Path
import subprocess
import struct
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "prepare_android_assets", ROOT / "scripts/prepare_android_assets.py")
PACK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACK)


def catalog_fixture(contents, target="android"):
    """构造与 asset_catalog.fbs 相同布局的最小真实 FlatBuffer。"""
    data = bytearray(8)
    data[4:8] = b"EASC"

    def align(boundary):
        data.extend(bytes((-len(data)) % boundary))

    def uoffset(location, target):
        struct.pack_into("<I", data, location, target - location)

    def table(count):
        align(2)
        vtable = len(data)
        data.extend(struct.pack("<HH", 4 + count * 2, 8 + count * 8))
        data.extend(b"".join(struct.pack("<H", 8 + index * 8) for index in range(count)))
        align(8)
        start = len(data)
        data.extend(bytes(8 + count * 8))
        struct.pack_into("<i", data, start, start - vtable)
        return [start + 8 + index * 8 for index in range(count)], start

    def text(location, value):
        align(4)
        encoded = value.encode()
        start = len(data)
        data.extend(struct.pack("<I", len(encoded)) + encoded + b"\0")
        uoffset(location, start)

    fields, root = table(4)
    struct.pack_into("<I", data, 0, root)
    struct.pack_into("<I", data, fields[0], 1)
    text(fields[1], "fixture-guid-catalog")
    text(fields[2], target)
    align(4)
    vector = len(data)
    data.extend(struct.pack("<II", 1, 0))
    uoffset(fields[3], vector)
    asset, item = table(7)
    uoffset(vector + 4, item)
    guid = "30000000-0000-4000-8000-000000000001"
    text(asset[0], guid)
    text(asset[1], "project/scripts/game.ts")
    struct.pack_into("<B", data, asset[2], 1)
    checksum = PACK.FNV_OFFSET
    for byte in contents:
        checksum = ((checksum ^ byte) * PACK.FNV_PRIME) & PACK.FNV_MASK
    text(asset[3], f"{checksum:016x}")
    struct.pack_into("<Q", data, asset[4], len(contents))
    # dependencies 是空 vector；所有相对指针均指向有效缓冲区。
    align(4)
    dependencies = len(data)
    data.extend(struct.pack("<I", 0))
    uoffset(asset[5], dependencies)
    text(asset[6], "scripts/game.js")
    return bytes(data), guid


class AndroidBuildArgumentTest(unittest.TestCase):
    def test_asset_builder_argument_reaches_native_configuration(self):
        spec = importlib.util.spec_from_file_location("build_android", ROOT / "scripts/build_android.py")
        build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(build)
        with tempfile.TemporaryDirectory(prefix="engine-android-command-") as directory:
            root = Path(directory)
            sdk = root / "sdk"
            sdk.mkdir()
            (sdk / "source.properties").write_text("Pkg.Revision = 27.2.12479018\n")
            (sdk / "bin").mkdir()
            (sdk / "bin" / ("java.exe" if build.os.name == "nt" else "java")).write_text("")
            builder = root / "host builder with spaces"
            builder.write_text("fixture only")
            builder.chmod(0o755)
            commands = []

            def record(command):
                commands.append(command)
                if ":app:assembleDebug" in command:
                    apk = root / "build/android-gradle/debug/outputs/apk/debug/app-debug.apk"
                    apk.parent.mkdir(parents=True)
                    apk.write_bytes(b"fixture apk")

            with mock.patch.object(build, "ROOT", root), \
                 mock.patch.object(build, "require_directory", return_value=sdk), \
                 mock.patch.object(build, "gradle_command", return_value=["fixture-gradle"]), \
                 mock.patch.object(build.shutil, "which", return_value="fixture-cmake"), \
                 mock.patch.object(build.subprocess, "check_output", return_value="Gradle 9.1.0"), \
                 mock.patch.object(build, "run", side_effect=record), \
                 mock.patch("builtins.print"), \
                 mock.patch.object(build.sys, "argv", ["build_android.py", "--asset-builder", str(builder)]):
                self.assertEqual(build.main(), 0)
            self.assertIn("-DENGINE_ANDROID_ASSET_BUILDER=" + str(builder.resolve()), commands[0])
            self.assertIn("-DENGINE_ANDROID_PROJECT=" + str((root / "projects/minimal").resolve()), commands[0])

    def test_prepare_uses_explicit_android_target_without_shell(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            builder = root / "asset builder $literal"
            project = root / "project with spaces"
            with mock.patch.object(PACK.subprocess, "run", return_value=mock.Mock(returncode=0)) as run:
                PACK.compile_catalog(project, builder)
            self.assertEqual(run.call_args.args[0],
                             [str(builder.resolve()), "--project", str(project), "--target", "android"])
            self.assertNotIn("shell", run.call_args.kwargs)


class PrepareAndroidAssetsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="engine-android-assets-")
        self.root = Path(self.temporary.name)
        self.project = self.root / "项目 with spaces $literal"
        for name in ("settings", "assets", "cache/scripts"):
            (self.project / name).mkdir(parents=True)
        self.source = self.project / "cache"
        (self.source / "scripts/game.js").write_text("export default class Game {}\n", encoding="utf-8")
        self.output = self.root / "android/assets"
        (self.output / "v8").mkdir(parents=True)
        (self.output / "v8/icudtl.dat").write_bytes(b"ICU data must survive")
        self.compiler = mock.patch.object(PACK, "compile_catalog", return_value=None)
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
        with mock.patch.object(PACK, "compile_catalog", side_effect=ValueError("编译失败")):
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

    def test_guid_catalog_packaging_and_platform_validation(self):
        contents = b"export default class Game {}\n"
        catalog, guid = catalog_fixture(contents)
        cache = self.project / "cache"
        (cache / "catalog.bin").write_bytes(catalog)
        (cache / "gen").mkdir()
        (cache / "gen" / guid).write_bytes(contents)
        (cache / "tasks.sqlite").write_bytes(b"must not ship")
        (cache / "gen/stale").write_bytes(b"must not ship")
        manifest = self.prepare()
        self.assertEqual({item["path"] for item in manifest["files"]},
                         {"catalog.bin", "gen/" + guid})
        self.assertEqual((self.output / "project/gen" / guid).read_bytes(), contents)
        previous = self.snapshot()
        (cache / "gen" / guid).write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "校验不符"):
            self.prepare()
        self.assertEqual(self.snapshot(), previous)
        (cache / "gen" / guid).write_bytes(contents)
        catalog, _ = catalog_fixture(contents, "macos")
        (cache / "catalog.bin").write_bytes(catalog)
        with self.assertRaisesRegex(ValueError, "android"):
            self.prepare()
        self.assertEqual(self.snapshot(), previous)
        self.assert_clean()

    def test_guid_catalog_truncation_is_rejected(self):
        catalog, _ = catalog_fixture(b"export {}")
        cache = self.project / "cache"
        for length in (0, 8, 24, len(catalog) - 1):
            with self.subTest(length=length):
                (cache / "catalog.bin").write_bytes(catalog[:length])
                with self.assertRaises(ValueError):
                    self.prepare()
                self.assert_clean()

    def test_missing_catalog_requires_host_asset_builder(self):
        self.compiler.stop()
        with self.assertRaisesRegex(ValueError, "asset-builder"):
            PACK.compile_catalog(self.project)

    def test_failed_asset_builder_preserves_actionable_error(self):
        self.compiler.stop()
        with mock.patch.object(PACK.subprocess, "run", return_value=mock.Mock(returncode=1)):
            with self.assertRaisesRegex(ValueError, "资产任务编译失败"):
                PACK.compile_catalog(self.project, self.root / "host asset builder")


if __name__ == "__main__":
    unittest.main()
