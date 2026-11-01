"""导出回归：来源保护、复制校验、发布回滚与备份保留。"""

from pathlib import Path
import json
import plistlib
import tempfile
import sys
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import artifact_output as output
import export_desktop as desktop
import resource_catalog as catalog
from prepare_android_assets_test import PACK as android, catalog_fixture, resource_entry


class DesktopExportTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="engine-export-")
        self.root = Path(self.temporary.name).resolve()
        self.destination = self.root / "output/macos/runtime"
        self.source = self.root / "resources"
        self.source.mkdir()
        self.entry = resource_entry(b"export default class Game {}")
        asset = self.source / self.entry["path"]
        asset.parent.mkdir()
        asset.write_bytes(self.entry["entity"])
        (self.source / "catalog.bin").write_bytes(catalog_fixture([self.entry], "macos"))

    def tearDown(self):
        self.temporary.cleanup()

    def previous(self):
        self.destination.mkdir(parents=True)
        (self.destination / "old").write_text("previous")

    def export_arguments(self, platform="macos", app_bundle=False):
        executable = self.root / "engine_app"
        executable.write_bytes(b"fixture executable")
        builder = self.root / "asset builder"
        builder.write_bytes(b"fixture builder")
        project = self.root / "project"
        project.mkdir(exist_ok=True)
        icu = self.root / "icudtl.dat"
        icu.write_bytes(b"fixture ICU")
        return SimpleNamespace(
            executable=executable,
            builder=builder,
            project=project,
            icu=icu,
            kind="runtime",
            platform=platform,
            app_bundle=app_bundle,
            product="game_runtime",
            output=self.destination,
            manifest=Path("settings/resource-build.toml"),
            support_file=[],
            support_directory=[],
            cmake="cmake",
            library_dir=[],
        )

    def test_export_uses_assets_directory_for_native_and_app_bundle(self):
        for platform, app_bundle in (("windows", False), ("macos", False), ("macos", True)):
            with self.subTest(platform=platform, app_bundle=app_bundle):
                args = self.export_arguments(platform, app_bundle)
                (self.source / "catalog.bin").write_bytes(catalog_fixture([self.entry], platform))
                with (
                    mock.patch.object(desktop, "ROOT", self.root),
                    mock.patch.object(desktop, "build_resources", return_value=self.source),
                    mock.patch.object(desktop.subprocess, "run"),
                    mock.patch("builtins.print"),
                ):
                    desktop.export(args)
                base = self.destination
                if app_bundle:
                    bundle = base / "game_runtime.app"
                    base = bundle / "Contents/Resources"
                    self.assertEqual(
                        (bundle / "Contents/MacOS/game_runtime").read_bytes(),
                        b"fixture executable",
                    )
                    with (bundle / "Contents/Info.plist").open("rb") as stream:
                        self.assertEqual(
                            plistlib.load(stream)["SDL_FILESYSTEM_BASE_DIR_TYPE"], "resource"
                        )
                else:
                    binary = "game_runtime.exe" if platform == "windows" else "game_runtime"
                    self.assertEqual((base / binary).read_bytes(), b"fixture executable")
                self.assertTrue((base / "assets/catalog.bin").is_file())
                self.assertEqual(
                    (base / "assets" / self.entry["path"]).read_bytes(), self.entry["entity"]
                )
                self.assertFalse((base / "catalog.bin").exists())
                self.assertEqual((base / "icudtl.dat").read_bytes(), b"fixture ICU")

    def test_build_resources_passes_package_target_and_optional_manifest(self):
        args = self.export_arguments()
        receipt = {"status": "ok", "targetPlatform": "macos", "output": str(self.source)}
        for manifest in (Path("settings/resource-build.toml"), None):
            with self.subTest(manifest=manifest):
                args.manifest = manifest
                with mock.patch.object(
                    desktop.subprocess,
                    "run",
                    return_value=mock.Mock(returncode=0, stdout=json.dumps(receipt)),
                ) as run:
                    self.assertEqual(desktop.build_resources(args), self.source)
                expected = [
                    str(args.builder),
                    "--project",
                    str(args.project),
                    "--package",
                    "--target",
                    "macos",
                ]
                if manifest:
                    expected += ["--manifest", manifest.as_posix()]
                self.assertEqual(run.call_args.args[0], expected)
                self.assertNotIn("shell", run.call_args.kwargs)

    def test_both_builders_reject_malformed_receipts(self):
        args = self.export_arguments()
        for platform, build in (
            ("macos", lambda: desktop.build_resources(args)),
            ("android", lambda: android.compile_catalog(args.project, args.builder)),
        ):
            valid = {"status": "ok", "targetPlatform": platform, "output": str(self.source)}
            invalid = ["invalid JSON", "null", "[]", "{}"]
            invalid += [
                json.dumps(valid | {"output": value})
                for value in (None, [], {}, 1, "", "relative/resources")
            ]
            invalid += [
                json.dumps(valid | {"status": "failed"}),
                json.dumps(valid | {"targetPlatform": "other"}),
            ]
            for receipt in invalid:
                with self.subTest(platform=platform, receipt=receipt):
                    with mock.patch.object(
                        desktop.subprocess,
                        "run",
                        return_value=mock.Mock(returncode=0, stdout=receipt),
                    ):
                        with self.assertRaises(ValueError):
                            build()

    def test_output_cannot_replace_source_ancestor_or_child(self):
        for candidate, source in (
            (self.root / "output", self.source),
            (self.destination, self.destination / "source"),
            (self.destination, self.destination.parent),
            (self.root / "outside", self.source),
        ):
            with self.subTest(candidate=candidate, source=source), self.assertRaises(ValueError):
                output.validate_output(candidate, self.root / "output", [source])

    def test_replaces_whole_snapshot_and_removes_stale_files(self):
        self.previous()
        with output.staged_output(self.destination) as stage:
            self.assertEqual(desktop.copy_resources(self.source, stage, "macos"), (1, 1))
        self.assertFalse((self.destination / "old").exists())
        self.assertTrue((self.destination / "catalog.bin").is_file())

    def test_corrupt_entity_keeps_previous_output(self):
        self.previous()
        (self.source / self.entry["path"]).write_bytes(b"changed during build")
        with self.assertRaises(ValueError), output.staged_output(self.destination) as stage:
            desktop.copy_resources(self.source, stage, "macos")
        self.assertEqual((self.destination / "old").read_text(), "previous")

    def test_copy_time_change_keeps_previous_output(self):
        self.previous()
        copy = catalog.copy_asset

        def changed(source, target, limit):
            result = copy(source, target, limit)
            data = bytearray(Path(target).read_bytes())
            data[-1] ^= 1
            Path(target).write_bytes(data)
            return result

        with mock.patch.object(catalog, "copy_asset", side_effect=changed):
            with self.assertRaises(ValueError), output.staged_output(self.destination) as stage:
                desktop.copy_resources(self.source, stage, "macos")
        self.assertTrue((self.destination / "old").exists())

    def test_source_catalog_change_keeps_the_captured_revision(self):
        snapshot = (self.source / "catalog.bin").read_bytes()
        copy = catalog.copy_asset

        def changed(source, target, limit):
            path = self.source / "catalog.bin"
            path.write_bytes(snapshot.replace(b"fixture-guid-catalog", b"changed-guid-catalog"))
            return copy(source, target, limit)

        with mock.patch.object(catalog, "copy_asset", side_effect=changed):
            with output.staged_output(self.destination) as stage:
                desktop.copy_resources(self.source, stage, "macos")
        self.assertEqual((self.destination / "catalog.bin").read_bytes(), snapshot)

    def test_desktop_preserves_custom_carrier_paths_and_ranges(self):
        self.entry["path"] = "data/shared.bin"
        prefix = b"unselected prefix"
        self.entry["offset"] = len(prefix)
        contents = prefix + self.entry["entity"] + b"unselected suffix"
        path = self.source / self.entry["path"]
        path.parent.mkdir()
        path.write_bytes(contents)
        (self.source / "catalog.bin").write_bytes(catalog_fixture([self.entry], "macos"))
        with output.staged_output(self.destination) as stage:
            self.assertEqual(desktop.copy_resources(self.source, stage, "macos"), (1, 1))
        self.assertEqual((self.destination / self.entry["path"]).read_bytes(), contents)

    def test_catalog_and_carriers_cannot_overwrite_deployed_files(self):
        for relative in ("catalog.bin", self.entry["path"]):
            with self.subTest(relative=relative), output.staged_output(self.destination) as stage:
                deployed = stage / relative
                deployed.parent.mkdir(parents=True, exist_ok=True)
                deployed.write_bytes(b"existing deployed data")
                with self.assertRaisesRegex(ValueError, "冲突"):
                    desktop.copy_resources(self.source, stage, "macos")
                self.assertEqual(deployed.read_bytes(), b"existing deployed data")

    def test_failed_publish_restores_previous(self):
        self.previous()
        replace = output.os.replace

        def failed(source, destination):
            if Path(source).name == "next":
                raise OSError("publish failed")
            return replace(source, destination)

        with mock.patch.object(output.os, "replace", side_effect=failed):
            with self.assertRaises(OSError), output.staged_output(self.destination) as stage:
                (stage / "new").write_text("next")
        self.assertEqual((self.destination / "old").read_text(), "previous")

    def test_failed_rollback_preserves_recovery_directory(self):
        self.previous()
        replace = output.os.replace

        def failed(source, destination):
            if Path(source).name in ("next", "previous"):
                raise OSError("filesystem unavailable")
            return replace(source, destination)

        with mock.patch.object(output.os, "replace", side_effect=failed):
            with (
                self.assertRaisesRegex(ValueError, "旧版保留"),
                output.staged_output(self.destination),
            ):
                pass
        backups = list(self.destination.parent.glob(".runtime-export-*/previous/old"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_text(), "previous")

    def test_interruption_after_each_rename_restores_previous(self):
        self.previous()
        replace = output.os.replace
        for interrupted_name in ("runtime", "next"):
            interrupted = False

            def interrupt(source, destination):
                nonlocal interrupted
                result = replace(source, destination)
                if Path(source).name == interrupted_name and not interrupted:
                    interrupted = True
                    raise KeyboardInterrupt()
                return result

            with self.subTest(after=interrupted_name):
                with mock.patch.object(output.os, "replace", side_effect=interrupt):
                    with (
                        self.assertRaises(KeyboardInterrupt),
                        output.staged_output(self.destination),
                    ):
                        pass
                self.assertEqual((self.destination / "old").read_text(), "previous")

    def test_concurrent_export_does_not_touch_current_output(self):
        self.previous()
        with output.staged_output(self.destination) as stage:
            with self.assertRaises(ValueError), output.staged_output(self.destination):
                pass
            (stage / "new").write_text("next")
        self.assertTrue((self.destination / "new").exists())


if __name__ == "__main__":
    unittest.main()
