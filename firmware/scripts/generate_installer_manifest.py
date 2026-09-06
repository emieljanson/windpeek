#!/usr/bin/env python3
"""Build and validate WindScout's immutable browser-installer bundles."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import shutil
from pathlib import Path


BOARD_ID = "seeedstudio_reterminal_e1002"
SUPPORTED_BOARD_IDS = {
    BOARD_ID,
    "seeedstudio_reterminal_e1003",
}
FIRMWARE_LAYOUT_VERSION = 1
FLASH_SIZE_BYTES = 32 * 1024 * 1024
PART_KINDS = {
    "bootloader/bootloader.bin": "bootloader",
    "partition_table/partition-table.bin": "partition-table",
    "ota_data_initial.bin": "boot-selection",
    "windscout.bin": "application",
}


class ManifestError(ValueError):
    pass


def _number(value: str) -> int:
    return int(value.strip(), 0)


def _flash_size(value: str) -> int:
    match = re.fullmatch(r"(\d+)(MB|KB)", value.strip(), re.IGNORECASE)
    if not match:
        raise ManifestError(f"Unsupported flash size: {value}")
    multiplier = 1024 * 1024 if match.group(2).upper() == "MB" else 1024
    return int(match.group(1)) * multiplier


def _partitions(path: Path) -> list[dict]:
    partitions = []
    with path.open(newline="") as handle:
        rows = (line for line in handle if not line.lstrip().startswith("#"))
        for row in csv.reader(rows, skipinitialspace=True):
            if not row or len(row) < 5:
                continue
            partitions.append(
                {
                    "name": row[0].strip(),
                    "type": row[1].strip(),
                    "subtype": row[2].strip(),
                    "offset": _number(row[3]),
                    "size": _number(row[4]),
                }
            )
    return partitions


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _write_immutable(path: Path, content: bytes) -> None:
    if path.exists():
        if path.read_bytes() != content:
            raise ManifestError(f"Refusing to overwrite immutable artifact: {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)


def _validate_ranges(parts: list[dict], flash_size: int, label: str) -> None:
    ranges = []
    for part in parts:
        start = int(part["offset"])
        end = start + int(part["size"])
        if start < 0 or end > flash_size or end <= start:
            raise ManifestError(f"{label} part is outside flash bounds: {part['kind']}")
        ranges.append((start, end, part["kind"]))
    ranges.sort()
    for previous, current in zip(ranges, ranges[1:]):
        if current[0] < previous[1]:
            raise ManifestError(f"{label} ranges overlap: {previous[2]} and {current[2]}")


def validate_manifest(
    manifest: dict,
    bundle_dir: Path,
    partitions_path: Path,
    expected_board_id: str | None = None,
) -> None:
    board_id = manifest.get("boardId")
    if (
        manifest.get("schemaVersion") != 1
        or board_id not in SUPPORTED_BOARD_IDS
        or (expected_board_id is not None and board_id != expected_board_id)
    ):
        raise ManifestError("Unsupported installer manifest identity")
    if (
        manifest.get("chipFamily") != "ESP32-S3"
        or manifest.get("firmwareLayoutVersion") != FIRMWARE_LAYOUT_VERSION
        or manifest.get("flashSize") != FLASH_SIZE_BYTES
    ):
        raise ManifestError("Unexpected chip family or flash size")
    parts = manifest.get("parts")
    if not isinstance(parts, list) or len(parts) != 4:
        raise ManifestError("Installer manifest must contain exactly four flash parts")
    for part in parts:
        path = bundle_dir / part["file"]
        if not path.is_file() or path.stat().st_size != part["size"] or _sha256(path) != part["sha256"]:
            raise ManifestError(f"Missing or corrupt installer part: {part.get('kind')}")
    clean = manifest.get("cleanInstall", {})
    update = manifest.get("preservingUpdate", {})
    if clean.get("eraseFlash") is not True or update.get("eraseFlash") is not False:
        raise ManifestError("Invalid erase policy")
    clean_parts = clean.get("parts", [])
    if len(clean_parts) != len(PART_KINDS) or {
        part.get("kind") for part in clean_parts
    } != set(PART_KINDS.values()):
        raise ManifestError("Clean install must contain every flash part exactly once")
    _validate_ranges(clean.get("parts", []), FLASH_SIZE_BYTES, "clean install")
    _validate_ranges(update.get("parts", []), FLASH_SIZE_BYTES, "preserving update")
    canonical = {part["kind"]: part for part in parts}
    for write_set in (clean.get("parts", []), update.get("parts", [])):
        for part in write_set:
            if canonical.get(part.get("kind")) != part:
                raise ManifestError("Write set does not match its checksummed flash part")
    if [part.get("kind") for part in update.get("parts", [])] != [
        "boot-selection",
        "application",
    ]:
        raise ManifestError("Preserving update must select and write one application")
    protected = [p for p in _partitions(partitions_path) if p["name"] in {"nvs", "storage"}]
    for part in update["parts"]:
        start, end = part["offset"], part["offset"] + part["size"]
        for partition in protected:
            protected_start = partition["offset"]
            protected_end = protected_start + partition["size"]
            if start < protected_end and end > protected_start:
                raise ManifestError(
                    f"Preserving update intersects protected {partition['name']} partition"
                )


def generate_installer_bundle(
    build_dir: Path,
    partitions_path: Path,
    output_dir: Path,
    version: str,
    board_id: str,
    flat: bool = False,
) -> Path:
    build_dir = Path(build_dir)
    partitions_path = Path(partitions_path)
    output_dir = Path(output_dir)
    if board_id not in SUPPORTED_BOARD_IDS:
        raise ManifestError(f"Unsupported installer board: {board_id}")
    safe_version = re.sub(r"[^A-Za-z0-9._-]", "-", version).strip("-")
    if not safe_version:
        raise ManifestError("Version must contain an immutable identifier")
    args_path = build_dir / "flasher_args.json"
    if not args_path.is_file() or not partitions_path.is_file():
        raise ManifestError("ESP-IDF flasher metadata or partition CSV is missing")
    args = json.loads(args_path.read_text())
    if args.get("extra_esptool_args", {}).get("chip") != "esp32s3":
        raise ManifestError("Installer bundle is not an ESP32-S3 build")
    flash_size = _flash_size(args.get("flash_settings", {}).get("flash_size", ""))
    if flash_size != FLASH_SIZE_BYTES:
        raise ManifestError("The reTerminal installer requires the real 32 MB flash layout")
    flash_files = args.get("flash_files", {})
    if set(flash_files.values()) != set(PART_KINDS):
        raise ManifestError("Unexpected or stale ESP-IDF flash part names")

    bundle_dir = output_dir if flat else output_dir / safe_version
    parts = []
    for offset_text, source_name in sorted(flash_files.items(), key=lambda item: _number(item[0])):
        source = build_dir / source_name
        if not source.is_file():
            raise ManifestError(f"Build part is missing: {source_name}")
        kind = PART_KINDS[source_name]
        suffix = source.suffix or ".bin"
        published_name = f"{kind}-{safe_version}{suffix}"
        destination = bundle_dir / published_name
        _write_immutable(destination, source.read_bytes())
        parts.append(
            {
                "kind": kind,
                "file": published_name,
                "offset": _number(offset_text),
                "size": destination.stat().st_size,
                "sha256": _sha256(destination),
            }
        )
    by_kind = {part["kind"]: part for part in parts}
    manifest = {
        "schemaVersion": 1,
        "version": version,
        "boardId": board_id,
        "chipFamily": "ESP32-S3",
        "firmwareLayoutVersion": FIRMWARE_LAYOUT_VERSION,
        "flashSize": flash_size,
        "protocol": {"minimum": 1, "maximum": 1},
        "configuration": {"minimum": 4, "maximum": 4},
        "parts": parts,
        "cleanInstall": {"eraseFlash": True, "parts": [dict(part) for part in parts]},
        "preservingUpdate": {
            "eraseFlash": False,
            "parts": [dict(by_kind["boot-selection"]), dict(by_kind["application"])],
        },
        "otaApplication": by_kind["application"]["file"],
    }
    validate_manifest(manifest, bundle_dir, partitions_path, board_id)
    manifest_bytes = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    manifest_name = f"installer-manifest-{safe_version}.json" if flat else "installer-manifest.json"
    manifest_path = bundle_dir / manifest_name
    _write_immutable(manifest_path, manifest_bytes)
    pointer = {
        "version": version,
        "manifest": manifest_name if flat else f"{safe_version}/installer-manifest.json",
        "sha256": hashlib.sha256(manifest_bytes).hexdigest(),
    }
    pointer_bytes = (json.dumps(pointer, indent=2, sort_keys=True) + "\n").encode()
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "latest.json").write_bytes(pointer_bytes)
    return manifest_path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--partitions", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--board-id", default=BOARD_ID)
    parser.add_argument("--flat", action="store_true")
    args = parser.parse_args()
    path = generate_installer_bundle(
        args.build_dir, args.partitions, args.output, args.version, args.board_id, args.flat
    )
    print(path)


if __name__ == "__main__":
    main()
