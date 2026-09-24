#!/usr/bin/env python3
import argparse
import os
import shlex
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

# Add scripts to sys.path to import boards
sys.path.append(os.path.join(os.path.dirname(__file__), "scripts"))
from boards import SUPPORTED_BOARDS

BOARDS = list(SUPPORTED_BOARDS.keys())

PINNED_IDF_VERSION = "v6.0.2"
DEFAULT_IDF_PATH = os.path.expanduser(
    f"~/.espressif/frameworks/esp-idf-{PINNED_IDF_VERSION}"
)


def local_installer_version(requested, now=None):
    """Return an explicit version or a fresh version for a local device build."""
    if requested:
        return requested
    instant = now or datetime.now(timezone.utc)
    return f"dev-local-{instant.strftime('%Y%m%d-%H%M%S')}"


def with_firmware_version(extra_args, version):
    """Ensure the manifest version is also embedded in the device firmware."""
    prefix = "-DFIRMWARE_VERSION="
    configured = [argument[len(prefix):].strip("'\"") for argument in extra_args
                  if argument.startswith(prefix)]
    if configured and any(value != version for value in configured):
        raise ValueError("The installer version must match the embedded firmware version.")
    return list(extra_args) if configured else [*extra_args, f"{prefix}{version}"]


def run_idf(args):
    """Run idf.py from the project's pinned ESP-IDF installation."""
    idf_path = os.environ.get("EINKWIND_IDF_PATH", DEFAULT_IDF_PATH)
    export_script = os.path.join(idf_path, "export.sh")

    if os.path.isfile(export_script):
        environment = os.environ.copy()
        if not environment.get("IDF_PYTHON_ENV_PATH"):
            env_root = Path.home() / ".espressif" / "python_env"
            candidates = sorted(env_root.glob("idf6.0_py*_env"), reverse=True)
            for candidate in candidates:
                if candidate.joinpath("bin", "python").is_file():
                    environment["IDF_PYTHON_ENV_PATH"] = str(candidate)
                    break
        command = (
            f"source {shlex.quote(export_script)} >/dev/null && "
            f"exec idf.py {shlex.join(args)}"
        )
        # Keep the caller's PATH. A login shell may replace it and make tools
        # such as CMake disappear even though ESP-IDF was launched from a
        # correctly configured development environment.
        return subprocess.run(["/bin/zsh", "-c", command], check=True, env=environment)

    if shutil.which("idf.py"):
        return subprocess.run(["idf.py", *args], check=True)

    raise FileNotFoundError(
        f"ESP-IDF {PINNED_IDF_VERSION} is not installed at {idf_path}. "
        "Install it there or set EINKWIND_IDF_PATH."
    )


def validate_board_config(board, config_path=Path("sdkconfig")):
    """Defaults do not override a board retained in an existing sdkconfig."""
    if not config_path.exists():
        return
    selected = [line for line in config_path.read_text().splitlines()
                if line.startswith("CONFIG_BOARD_DRIVER_") and line.endswith("=y")]
    expected = f"CONFIG_BOARD_DRIVER_{board.upper()}=y"
    if selected != [expected]:
        raise ValueError(f"sdkconfig does not select {board}. Rebuild with --fullclean.")


def build_firmware(board, extra_args, debug=False):
    """Build firmware with idf.py."""
    print(f"\n=== Building firmware for {board}{' [debug]' if debug else ''} ===")
    sdkconfig_defaults = f"sdkconfig.defaults;boards/sdkconfig.defaults.{board}"
    if debug:
        # Debug-only overlay: core-dump-to-flash capture (+ the coredump partition
        # from generate_partitions.py). Changes the partition table — never used
        # for release or demo builds.
        sdkconfig_defaults += ";sdkconfig.defaults.debug"

    idf_base = [
        f"-DSDKCONFIG_DEFAULTS={sdkconfig_defaults}",
    ]

    cmake_defines = [a for a in extra_args if a.startswith("-D")]
    post_build_args = [a for a in extra_args if not a.startswith("-D")]

    build_cmd = idf_base + cmake_defines + ["build"]
    print(f"Running: {' '.join(build_cmd)}")

    try:
        validate_board_config(board)
        run_idf(build_cmd)
        validate_board_config(board)
    except ValueError as e:
        print(f"Build stopped: {e}")
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print(f"Build failed with exit code {e.returncode}")
        sys.exit(e.returncode)
    except FileNotFoundError as e:
        print(
            f"Error: ESP-IDF {PINNED_IDF_VERSION} was not found. {e}"
        )
        sys.exit(1)

    # Run post-build commands (flash, monitor, etc.)
    if post_build_args:
        post_cmd = idf_base + post_build_args
        print(f"Running: {' '.join(post_cmd)}")
        try:
            run_idf(post_cmd)
        except subprocess.CalledProcessError as e:
            print(f"Post-build command failed with exit code {e.returncode}")
            sys.exit(e.returncode)


def main():
    parser = argparse.ArgumentParser(description="Build firmware for different boards")
    parser.add_argument(
        "--board",
        choices=BOARDS,
        default="seeedstudio_reterminal_e100x",
        help="Board type to build",
    )
    parser.add_argument(
        "--fullclean",
        action="store_true",
        help="Remove sdkconfig and run idf.py fullclean before building",
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Debug build: enable core-dump-to-flash capture. Changes the "
        "partition table (adds a coredump partition) — do not ship to users.",
    )
    parser.add_argument(
        "--installer-output",
        type=Path,
        help="After the firmware build, write a validated browser-installer bundle here.",
    )
    parser.add_argument(
        "--installer-version",
        help="Immutable version used by --installer-output.",
    )
    # Allow passing extra arguments to idf.py
    args, extra_args = parser.parse_known_args()

    installer_version = None
    if args.installer_output:
        if args.board not in {"seeedstudio_reterminal_e100x", "seeedstudio_reterminal_e1003"} or args.debug:
            parser.error("installer bundles are release-only and require a Windpeek installer target")
        installer_version = local_installer_version(args.installer_version)
        try:
            extra_args = with_firmware_version(extra_args, installer_version)
        except ValueError as error:
            parser.error(str(error))
        print(f"Installer firmware version: {installer_version}")

    if args.fullclean:
        print("Performing full clean...")
        for f in ["sdkconfig", "partitions.csv"]:
            if os.path.exists(f):
                os.remove(f)
                print(f"  ✓ Removed {f}")
        if os.path.isdir("build"):
            shutil.rmtree("build")
            print("  ✓ Removed build/")

    build_firmware(args.board, extra_args, debug=args.debug)
    if args.installer_output:
        subprocess.run(
            [
                sys.executable,
                "scripts/generate_installer_manifest.py",
                "--build-dir",
                "build",
                "--partitions",
                "partitions.csv",
                "--output",
                str(args.installer_output),
                "--version",
                installer_version,
                "--board-id",
                "seeedstudio_reterminal_e1003" if args.board == "seeedstudio_reterminal_e1003"
                else "seeedstudio_reterminal_e1002",
            ],
            check=True,
        )


if __name__ == "__main__":
    main()
