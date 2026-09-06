import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from generate_installer_manifest import ManifestError, generate_installer_bundle, validate_manifest


class InstallerManifestTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        (self.build / "bootloader").mkdir(parents=True)
        (self.build / "partition_table").mkdir()
        fixtures = {
            "bootloader/bootloader.bin": b"bootloader",
            "partition_table/partition-table.bin": b"partitions",
            "ota_data_initial.bin": b"ota-data",
            "windscout.bin": b"windscout-app",
        }
        for name, content in fixtures.items():
            (self.build / name).write_bytes(content)
        (self.build / "flasher_args.json").write_text(
            json.dumps(
                {
                    "flash_settings": {"flash_size": "32MB"},
                    "flash_files": {
                        "0x0": "bootloader/bootloader.bin",
                        "0x8000": "partition_table/partition-table.bin",
                        "0xf000": "ota_data_initial.bin",
                        "0x20000": "windscout.bin",
                    },
                    "extra_esptool_args": {"chip": "esp32s3"},
                }
            )
        )
        self.partitions = self.root / "partitions.csv"
        self.partitions.write_text(
            "# Name,Type,SubType,Offset,Size,Flags\n"
            "nvs,data,nvs,0x9000,0x6000,\n"
            "otadata,data,ota,0xf000,0x2000,\n"
            "ota_0,app,ota_0,0x20000,0x380000,\n"
            "ota_1,app,ota_1,0x3A0000,0x380000,\n"
            "storage,data,littlefs,0x720000,0x01800000,\n"
        )
        self.output = self.root / "release"

    def tearDown(self):
        self.temp.cleanup()

    def test_generates_reproducible_clean_and_preserving_sets(self):
        manifest_path = generate_installer_bundle(
            build_dir=self.build,
            partitions_path=self.partitions,
            output_dir=self.output,
            version="v1.2.3",
            board_id="seeedstudio_reterminal_e1002",
        )
        first = manifest_path.read_bytes()
        manifest = json.loads(first)
        self.assertEqual(manifest["firmwareLayoutVersion"], 1)
        self.assertEqual(manifest["flashSize"], 32 * 1024 * 1024)
        self.assertEqual(manifest["configuration"], {"minimum": 4, "maximum": 4})
        self.assertTrue(manifest["cleanInstall"]["eraseFlash"])
        self.assertEqual(
            [part["kind"] for part in manifest["cleanInstall"]["parts"]],
            ["bootloader", "partition-table", "boot-selection", "application"],
        )
        self.assertEqual(
            [part["kind"] for part in manifest["preservingUpdate"]["parts"]],
            ["boot-selection", "application"],
        )
        for part in manifest["parts"]:
            published = manifest_path.parent / part["file"]
            self.assertEqual(part["size"], published.stat().st_size)
            self.assertEqual(part["sha256"], hashlib.sha256(published.read_bytes()).hexdigest())
        validate_manifest(manifest, manifest_path.parent, self.partitions)

        second_path = generate_installer_bundle(
            build_dir=self.build,
            partitions_path=self.partitions,
            output_dir=self.output,
            version="v1.2.3",
            board_id="seeedstudio_reterminal_e1002",
        )
        self.assertEqual(first, second_path.read_bytes())

    def test_generates_flat_github_release_assets(self):
        manifest_path = generate_installer_bundle(
            build_dir=self.build,
            partitions_path=self.partitions,
            output_dir=self.output,
            version="v1.2.3",
            board_id="seeedstudio_reterminal_e1002",
            flat=True,
        )
        pointer = json.loads((self.output / "latest.json").read_text())
        self.assertEqual(manifest_path.parent, self.output)
        self.assertEqual(pointer["manifest"], "installer-manifest-v1.2.3.json")
        self.assertEqual(manifest_path.name, pointer["manifest"])
        manifest = json.loads(manifest_path.read_text())
        for part in manifest["parts"]:
            self.assertNotIn("/", part["file"])
            self.assertTrue((self.output / part["file"]).is_file())

    def test_generates_an_e1003_manifest_with_the_same_safe_layout(self):
        manifest_path = generate_installer_bundle(
            build_dir=self.build,
            partitions_path=self.partitions,
            output_dir=self.output,
            version="v1.2.3-e1003",
            board_id="seeedstudio_reterminal_e1003",
        )
        manifest = json.loads(manifest_path.read_text())
        self.assertEqual(manifest["boardId"], "seeedstudio_reterminal_e1003")
        validate_manifest(
            manifest,
            manifest_path.parent,
            self.partitions,
            "seeedstudio_reterminal_e1003",
        )

    def test_rejects_corruption_overlap_wrong_board_and_stale_names(self):
        with self.assertRaises(ManifestError):
            generate_installer_bundle(
                self.build, self.partitions, self.output, "v1", "unsupported_board"
            )

        args = json.loads((self.build / "flasher_args.json").read_text())
        args["flash_files"]["0x20000"] = "photoframe.bin"
        (self.build / "photoframe.bin").write_bytes(b"legacy")
        (self.build / "flasher_args.json").write_text(json.dumps(args))
        with self.assertRaises(ManifestError):
            generate_installer_bundle(
                self.build,
                self.partitions,
                self.output,
                "v1",
                "seeedstudio_reterminal_e1002",
            )

    def test_validator_detects_tampering_and_protected_partition_writes(self):
        manifest_path = generate_installer_bundle(
            self.build,
            self.partitions,
            self.output,
            "v2",
            "seeedstudio_reterminal_e1002",
        )
        manifest = json.loads(manifest_path.read_text())
        incomplete = json.loads(manifest_path.read_text())
        incomplete["cleanInstall"]["parts"] = []
        with self.assertRaises(ManifestError):
            validate_manifest(incomplete, manifest_path.parent, self.partitions)

        app = next(part for part in manifest["parts"] if part["kind"] == "application")
        (manifest_path.parent / app["file"]).write_bytes(b"tampered")
        with self.assertRaises(ManifestError):
            validate_manifest(manifest, manifest_path.parent, self.partitions)

        (manifest_path.parent / app["file"]).write_bytes(b"windscout-app")
        manifest["preservingUpdate"]["parts"][1]["offset"] = 0x9000
        with self.assertRaises(ManifestError):
            validate_manifest(manifest, manifest_path.parent, self.partitions)


if __name__ == "__main__":
    unittest.main()
