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
    "prepare_android_assets", ROOT / "scripts/prepare_android_assets.py"
)
PACK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACK)


class BufferBuilder:
    """测试用 FlatBuffers 构造器；只实现本测试所需的真实表布局。"""

    def __init__(self, identifier):
        self.data = bytearray(8)
        self.data[4:8] = identifier

    def align(self, boundary):
        self.data.extend(bytes((-len(self.data)) % boundary))

    def pointer(self, location, target):
        struct.pack_into("<I", self.data, location, target - location)

    def table(self, count):
        self.align(2)
        vtable = len(self.data)
        self.data.extend(struct.pack("<HH", 4 + count * 2, 8 + count * 8))
        self.data.extend(b"".join(struct.pack("<H", 8 + index * 8) for index in range(count)))
        self.align(8)
        start = len(self.data)
        self.data.extend(bytes(8 + count * 8))
        struct.pack_into("<i", self.data, start, start - vtable)
        return [start + 8 + index * 8 for index in range(count)], start

    def text(self, location, value):
        self.align(4)
        encoded = value.encode()
        start = len(self.data)
        self.data.extend(struct.pack("<I", len(encoded)) + encoded + b"\0")
        self.pointer(location, start)

    def number(self, location, value, fmt="<I"):
        struct.pack_into(fmt, self.data, location, value)


def checksum(contents):
    value = PACK.FNV_OFFSET
    for byte in contents:
        value = ((value ^ byte) * PACK.FNV_PRIME) & PACK.FNV_MASK
    return f"{value:016x}"


def resource_entry(
    payload, number=1, kind=1, path="project/scripts/game.ts", module="scripts/game.js"
):
    guid = f"30000000-0000-4000-8000-{number:012x}"
    header = BufferBuilder(b"ERSH")
    fields, root = header.table(5)
    header.number(0, root)
    header.text(fields[0], guid)
    header.number(fields[1], kind, "<B")
    header.number(fields[2], 1)
    header.number(fields[3], 8 + len(header.data), "<Q")
    header.number(fields[4], len(payload), "<Q")
    entity = b"ERS1" + struct.pack("<I", len(header.data)) + header.data + payload
    return {
        "guid": guid,
        "logical": path,
        "kind": kind,
        "module": module,
        "header_size": 8 + len(header.data),
        "entity": bytes(entity),
        "path": "gen/" + guid,
        "offset": 0,
        "dependencies": [],
    }


def catalog_fixture(entries, target="android"):
    builder = BufferBuilder(b"EASC")
    fields, root = builder.table(4)
    builder.number(0, root)
    builder.number(fields[0], 2)
    builder.text(fields[1], "fixture-guid-catalog")
    builder.text(fields[2], target)
    builder.align(4)
    vector = len(builder.data)
    builder.data.extend(struct.pack("<I", len(entries)) + bytes(len(entries) * 4))
    builder.pointer(fields[3], vector)
    for index, entry in enumerate(entries):
        asset, table = builder.table(10)
        builder.pointer(vector + 4 + index * 4, table)
        builder.text(asset[0], entry["guid"])
        builder.text(asset[1], entry["logical"])
        builder.number(asset[2], entry["kind"], "<B")
        builder.text(asset[3], checksum(entry["entity"]))
        builder.number(asset[4], len(entry["entity"]), "<Q")
        builder.align(4)
        deps = len(builder.data)
        builder.data.extend(
            struct.pack("<I", len(entry["dependencies"])) + bytes(len(entry["dependencies"]) * 4)
        )
        builder.pointer(asset[5], deps)
        for dep_index, dependency in enumerate(entry["dependencies"]):
            builder.text(deps + 4 + dep_index * 4, dependency)
        builder.text(asset[6], entry["module"])
        builder.number(asset[7], 1)
        builder.number(asset[8], entry["header_size"])
        location, table = builder.table(4)
        builder.pointer(asset[9], table)
        builder.text(location[0], "content")
        builder.text(location[1], entry["path"])
        builder.number(location[2], entry["offset"], "<Q")
        builder.number(location[3], len(entry["entity"]), "<Q")
    return bytes(builder.data)


class AndroidBuildArgumentTest(unittest.TestCase):
    def test_asset_builder_argument_reaches_native_configuration(self):
        spec = importlib.util.spec_from_file_location(
            "build_android", ROOT / "scripts/build_android.py"
        )
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

            with (
                mock.patch.object(build, "ROOT", root),
                mock.patch.object(build, "require_directory", return_value=sdk),
                mock.patch.object(build, "gradle_command", return_value=["fixture-gradle"]),
                mock.patch.object(build.shutil, "which", return_value="fixture-cmake"),
                mock.patch.object(build.subprocess, "check_output", return_value="Gradle 9.1.0"),
                mock.patch.object(build, "run", side_effect=record),
                mock.patch("builtins.print"),
                mock.patch.object(
                    build.sys, "argv", ["build_android.py", "--asset-builder", str(builder)]
                ),
            ):
                self.assertEqual(build.main(), 0)
            self.assertIn("-DENGINE_ANDROID_ASSET_BUILDER=" + str(builder.resolve()), commands[0])
            self.assertIn(
                "-DENGINE_ANDROID_PROJECT=" + str((root / "projects/showcase").resolve()),
                commands[0],
            )

    def test_prepare_uses_explicit_android_target_without_shell(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            builder = root / "asset builder $literal"
            project = root / "project with spaces"
            with mock.patch.object(
                PACK.subprocess, "run", return_value=mock.Mock(returncode=0)
            ) as run:
                PACK.compile_catalog(project, builder)
            self.assertEqual(
                run.call_args.args[0],
                [str(builder.resolve()), "--project", str(project), "--target", "android"],
            )
            self.assertNotIn("shell", run.call_args.kwargs)


class PrepareAndroidAssetsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="engine-android-assets-")
        self.root = Path(self.temporary.name)
        self.project = self.root / "项目 with spaces $literal"
        for name in ("settings", "assets", "cache"):
            (self.project / name).mkdir(parents=True)
        self.source = self.project / "cache"
        self.output = self.root / "android/assets"
        (self.output / "v8").mkdir(parents=True)
        (self.output / "v8/icudtl.dat").write_bytes(b"ICU data must survive")
        self.entries = [resource_entry(b"export default class Game {}\n")]
        self.write_resources()

    def tearDown(self):
        self.temporary.cleanup()

    def write_resources(self, target="android"):
        carriers = {}
        for entry in self.entries:
            carrier = carriers.setdefault(entry["path"], bytearray())
            needed = entry["offset"] + len(entry["entity"])
            carrier.extend(bytes(max(0, needed - len(carrier))))
            carrier[entry["offset"] : needed] = entry["entity"]
        for relative, contents in carriers.items():
            path = self.source / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents)
        (self.source / "catalog.bin").write_bytes(catalog_fixture(self.entries, target))

    def prepare(self):
        return PACK.prepare(self.project, self.output)

    def snapshot(self):
        return {
            path.relative_to(self.output).as_posix(): path.read_bytes()
            for path in self.output.rglob("*")
            if path.is_file()
        }

    def assert_clean(self):
        self.assertFalse(
            any(path.name.startswith(".project-prepare-") for path in self.output.iterdir())
        )

    def test_only_catalog_carriers_ship_and_output_is_deterministic(self):
        self.entries += [
            resource_entry(b"", 2, 0, "project/空文件.txt", ""),
            resource_entry(b"\0\xff\x80\n", 3, 0, "project/binary.bin", ""),
        ]
        self.write_resources()
        (self.source / "assets.sqlite").write_bytes(b"editor only")
        (self.source / "gen/stale").write_bytes(b"not referenced")
        manifest = self.prepare()
        self.assertEqual(
            {item["path"] for item in manifest["files"]},
            {"catalog.bin"} | {entry["path"] for entry in self.entries},
        )
        for item in manifest["files"]:
            data = (self.output / "project" / item["path"]).read_bytes()
            self.assertEqual(item["size"], len(data))
            self.assertEqual(item["hash"], checksum(data))
        previous = self.snapshot()
        self.assertEqual(self.prepare(), manifest)
        self.assertEqual(self.snapshot(), previous)
        self.assertEqual((self.output / "v8/icudtl.dat").read_bytes(), b"ICU data must survive")
        self.assert_clean()

    def test_shared_package_copied_once_and_all_members_verified(self):
        # The packaging layer validates the typed entity envelope without decoding its payload.
        second = resource_entry(b"texture payload", 2, 6, "project/texture.png", "")
        first = self.entries[0]
        first["path"] = second["path"] = "packs/game"
        second["offset"] = len(first["entity"])
        self.entries.append(second)
        self.write_resources()
        manifest = self.prepare()
        self.assertEqual(
            {item["path"] for item in manifest["files"]}, {"catalog.bin", "packs/game"}
        )
        previous = self.snapshot()
        path = self.source / "packs/game"
        content = bytearray(path.read_bytes())
        content[-1] ^= 1
        path.write_bytes(content)
        with self.assertRaisesRegex(ValueError, "校验不符"):
            self.prepare()
        self.assertEqual(self.snapshot(), previous)
        self.assert_clean()

    def test_header_identity_checked_even_when_entity_hash_matches(self):
        self.prepare()
        previous = self.snapshot()
        entry = self.entries[0]
        entity = bytearray(entry["entity"])
        original = entry["guid"].encode()
        position = entity.find(original)
        self.assertGreater(position, 0)
        entity[position] = ord("4")
        entry["entity"] = bytes(entity)
        self.write_resources()
        with self.assertRaisesRegex(ValueError, "实体头与 catalog 不一致"):
            self.prepare()
        self.assertEqual(self.snapshot(), previous)

    def test_unknown_resource_kind_preserves_previous_output(self):
        self.prepare()
        previous = self.snapshot()
        self.entries.append(
            resource_entry(b"unsupported resource", 2, 13, "project/future.bin", "")
        )
        self.write_resources()
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertEqual(self.snapshot(), previous)
        self.assert_clean()

    def test_audio_resource_kind_is_preserved(self):
        audio = resource_entry(b"compiled PCM audio", 2, 7, "project/audio/room.wav", "")
        self.entries.append(audio)
        self.write_resources()
        self.prepare()
        self.assertEqual(
            (self.output / "project" / "gen" / audio["guid"]).read_bytes(), audio["entity"]
        )
        self.assert_clean()

    def test_font_and_ui_resources_preserve_identity_and_dependencies(self):
        font = resource_entry(b"compiled font curves", 2, 11, "project/fonts/lato.ttf", "")
        ui = resource_entry(b"compiled UI document", 3, 12, "project/ui/menu.rml", "")
        ui["dependencies"] = [font["guid"]]
        self.entries.extend([font, ui])
        self.write_resources()
        manifest = self.prepare()
        for entry in (font, ui):
            self.assertEqual(
                (self.output / "project" / "gen" / entry["guid"]).read_bytes(), entry["entity"]
            )
        self.assertEqual(len(manifest["files"]), 4)
        self.assert_clean()

    def test_catalog_missing_does_not_fall_back_to_scanning(self):
        self.prepare()
        previous = self.snapshot()
        (self.source / "catalog.bin").unlink()
        (self.source / "scripts").mkdir()
        (self.source / "scripts/game.js").write_text("legacy source must not ship")
        with self.assertRaisesRegex(ValueError, "asset-builder"):
            self.prepare()
        self.assertEqual(self.snapshot(), previous)

    def test_platform_truncation_and_missing_entry_rejected(self):
        self.prepare()
        previous = self.snapshot()
        self.write_resources("macos")
        with self.assertRaisesRegex(ValueError, "android"):
            self.prepare()
        catalog = catalog_fixture(self.entries)
        for length in (0, 8, 24, len(catalog) - 1):
            (self.source / "catalog.bin").write_bytes(catalog[:length])
            with self.assertRaises(ValueError):
                self.prepare()
            self.assert_clean()
        self.entries[0]["module"] = "scripts/other.js"
        self.write_resources()
        with self.assertRaisesRegex(ValueError, "scripts/game.js"):
            self.prepare()
        self.assertEqual(self.snapshot(), previous)

    def test_missing_carrier_and_bad_range_rejected(self):
        self.prepare()
        previous = self.snapshot()
        path = self.source / self.entries[0]["path"]
        path.unlink()
        with self.assertRaisesRegex(ValueError, "普通文件"):
            self.prepare()
        self.write_resources()
        self.entries[0]["offset"] = 1 << 63
        (self.source / "catalog.bin").write_bytes(catalog_fixture(self.entries))
        with self.assertRaisesRegex(ValueError, "定位"):
            self.prepare()
        self.assertEqual(self.snapshot(), previous)

    def test_symlinks_are_rejected_before_publishing(self):
        self.prepare()
        previous = self.snapshot()
        path = self.source / self.entries[0]["path"]
        target = self.root / "outside"
        target.write_bytes(path.read_bytes())
        path.unlink()
        try:
            path.symlink_to(target)
        except (OSError, NotImplementedError):
            self.skipTest("当前环境不允许创建符号链接")
        with self.assertRaisesRegex(ValueError, "符号链接"):
            self.prepare()
        self.assertEqual(self.snapshot(), previous)
        self.assert_clean()

    def test_publish_failure_restores_both_outputs(self):
        self.prepare()
        previous = self.snapshot()
        self.entries = [resource_entry(b"changed JS")]
        self.write_resources()
        original = Path.rename

        def fail_manifest(path, target):
            if path.name == "project-manifest.json" and path.parent.name.startswith(
                ".project-prepare-"
            ):
                raise OSError("fixture publish failure")
            return original(path, target)

        with mock.patch.object(Path, "rename", fail_manifest):
            with self.assertRaisesRegex(ValueError, "已恢复上一份输出"):
                self.prepare()
        self.assertEqual(self.snapshot(), previous)
        self.assert_clean()

    def test_compile_failure_preserves_output(self):
        self.prepare()
        previous = self.snapshot()
        with mock.patch.object(PACK, "compile_catalog", side_effect=ValueError("编译失败")):
            with self.assertRaisesRegex(ValueError, "编译失败"):
                self.prepare()
        self.assertEqual(self.snapshot(), previous)
        self.assert_clean()

    def test_unsafe_paths_and_overlapping_outputs_rejected(self):
        for path in (
            "",
            "/file",
            "../file",
            "a/../file",
            "a/./file",
            "a//file",
            "a/",
            "a\\file",
            "C:file",
            "a\0file",
        ):
            with self.assertRaises(ValueError):
                PACK.safe_relative(path)
        for output in (self.project, self.project / "cache/android", self.root):
            with self.assertRaisesRegex(ValueError, "相互包含"):
                PACK.prepare(self.project, output)

    def test_manifest_build_uses_receipt_output_without_reading_toml(self):
        subset = self.root / "compiled subset"
        subset.mkdir()
        receipt = {"status": "ok", "targetPlatform": "android", "output": str(subset)}
        with mock.patch.object(
            PACK.subprocess, "run", return_value=mock.Mock(returncode=0, stdout=json.dumps(receipt))
        ) as run:
            output = PACK.compile_catalog(
                self.project, self.root / "builder", Path("settings/android.toml")
            )
        self.assertEqual(output, subset)
        self.assertEqual(run.call_args.args[0][-2:], ["--manifest", "settings/android.toml"])
        self.assertNotIn("--target", run.call_args.args[0])
        receipt["targetPlatform"] = "macos"
        with mock.patch.object(
            PACK.subprocess, "run", return_value=mock.Mock(returncode=0, stdout=json.dumps(receipt))
        ):
            with self.assertRaisesRegex(ValueError, "android"):
                PACK.compile_catalog(self.project, self.root / "builder", "settings/android.toml")

    def test_existing_resource_root_avoids_compilation(self):
        with mock.patch.object(PACK.subprocess, "run") as run:
            manifest = PACK.prepare(self.project, self.output, resource_root=self.source)
        run.assert_not_called()
        self.assertEqual(len(manifest["files"]), 2)

    def test_failed_builder_preserves_actionable_error(self):
        with mock.patch.object(PACK.subprocess, "run", return_value=mock.Mock(returncode=1)):
            with self.assertRaisesRegex(ValueError, "资产任务编译失败"):
                PACK.compile_catalog(self.project, self.root / "builder")


if __name__ == "__main__":
    unittest.main()
