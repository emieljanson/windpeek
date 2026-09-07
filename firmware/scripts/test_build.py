import unittest
from unittest.mock import Mock, patch
from datetime import datetime, timezone
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import build
from build import local_installer_version, with_firmware_version


class BuildInstallerVersionTest(unittest.TestCase):
    def test_generates_a_unique_local_version_and_embeds_it_in_the_firmware(self):
        version = local_installer_version(
            None, now=datetime(2026, 8, 30, 18, 55, 42, tzinfo=timezone.utc)
        )

        self.assertEqual(version, "dev-local-20260830-185542")
        self.assertEqual(with_firmware_version([], version), [f"-DFIRMWARE_VERSION={version}"])

    def test_rejects_a_manifest_version_that_differs_from_the_embedded_version(self):
        with self.assertRaisesRegex(ValueError, "must match"):
            with_firmware_version(["-DFIRMWARE_VERSION=old-ui"], "new-ui")


class BuildStepsTest(unittest.TestCase):
    def run_steps(self, *arguments):
        calls = Mock()
        with patch.object(sys, "argv", ["build.py", *arguments]), \
             patch.object(build, "build_webapp", calls.webapp), \
             patch.object(build, "generate_splash", calls.splash), \
             patch.object(build, "build_firmware", calls.firmware):
            build.main()
        return calls

    def test_windscout_defaults_do_not_require_photo_frame_tools(self):
        for board in (
            "seeedstudio_reterminal_e1002",
            "seeedstudio_reterminal_e100x",
            "seeedstudio_reterminal_e1003",
        ):
            with self.subTest(board=board):
                calls = self.run_steps("--board", board)
                calls.webapp.assert_not_called()
                calls.splash.assert_not_called()
                calls.firmware.assert_called_once_with(board, [], debug=False)

    def test_photo_frame_default_keeps_all_required_assets(self):
        calls = self.run_steps("--board", "waveshare_photopainter_73")
        self.assertEqual([call[0] for call in calls.mock_calls], ["webapp", "splash", "firmware"])

    def test_explicit_steps_override_the_board_default(self):
        calls = self.run_steps("--board", "seeedstudio_reterminal_e100x", "--step", "webapp")
        calls.webapp.assert_called_once_with()
        calls.splash.assert_not_called()
        calls.firmware.assert_not_called()

    def test_missing_idf_reports_the_build_error(self):
        with patch.object(build, "run_idf", side_effect=FileNotFoundError("missing SDK")), \
             patch("builtins.print") as output, self.assertRaises(SystemExit) as stopped:
            build.build_firmware("seeedstudio_reterminal_e100x", [])
        self.assertEqual(stopped.exception.code, 1)
        self.assertIn("missing SDK", output.call_args.args[0])


if __name__ == "__main__":
    unittest.main()
