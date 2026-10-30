"""导出回归：来源保护、复制校验、发布回滚与备份保留。"""

from pathlib import Path
import tempfile
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import artifact_output as output
import export_desktop as desktop
from prepare_android_assets_test import catalog_fixture, resource_entry


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
        copy = desktop.shutil.copy2

        def changed(source, target):
            result = copy(source, target)
            if Path(source).name != "catalog.bin":
                data = bytearray(Path(target).read_bytes())
                data[-1] ^= 1
                Path(target).write_bytes(data)
            return result

        with mock.patch.object(desktop.shutil, "copy2", side_effect=changed):
            with self.assertRaises(ValueError), output.staged_output(self.destination) as stage:
                desktop.copy_resources(self.source, stage, "macos")
        self.assertTrue((self.destination / "old").exists())

    def test_source_catalog_change_keeps_the_captured_revision(self):
        snapshot = (self.source / "catalog.bin").read_bytes()
        copy = desktop.shutil.copy2

        def changed(source, target):
            catalog = self.source / "catalog.bin"
            catalog.write_bytes(snapshot.replace(b"fixture-guid-catalog", b"changed-guid-catalog"))
            return copy(source, target)

        with mock.patch.object(desktop.shutil, "copy2", side_effect=changed):
            with output.staged_output(self.destination) as stage:
                desktop.copy_resources(self.source, stage, "macos")
        self.assertEqual((self.destination / "catalog.bin").read_bytes(), snapshot)

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
