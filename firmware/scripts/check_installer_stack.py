"""Check the installer configuration call chains using GCC -fstack-usage output.

The reserve covers UART/parser frames, NVS, cJSON and library calls. This is a
regression budget for these known paths, not a whole-program stack proof.
"""

import argparse
from pathlib import Path


def check(build_dir: Path) -> None:
    frames = {}
    for source in ("wind_installer_service", "installed_configuration"):
        reports = list(build_dir.rglob(f"{source}*.su"))
        if not reports:
            raise ValueError(f"Missing {source} stack report; compile with -fstack-usage")
        for report in reports:
            for line in report.read_text().splitlines():
                location, size, kind = line.split("\t")
                name = location.rsplit(":", 1)[-1].split(".")[0]
                if kind != "static":
                    raise ValueError(f"Unbounded stack usage in {name}: {kind}")
                frames[name] = max(frames.get(name, 0), int(size))

    # Exported entry points must exist; static helpers may be inlined, in which
    # case their stack is already included in the caller's report.
    for name in ("wind_installer_service_handle_json", "installed_configuration_load",
                 "installed_configuration_promote_setup"):
        if name not in frames:
            raise ValueError(f"Missing stack measurement for {name}")

    common = ["wind_installer_service_handle_json", "installed_configuration_load",
              "installed_configuration_promote_setup", "configuration_digest_unchecked",
              "single_digest"]
    for operation, helpers in (("get_state with migration", ["handle_state"]),
                               ("stage_configuration with migration", ["handle_stage_configuration"])):
        measured = sum(frames.get(name, 0) for name in common + helpers)
        budget = 16384 - 4096
        print(f"{operation}: {measured} bytes, budget {budget} (+4096 reserve)")
        if measured > budget:
            raise ValueError(f"Installer stack budget exceeded by {operation}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    try:
        check(args.build_dir)
    except ValueError as error:
        parser.exit(1, f"{error}\n")
