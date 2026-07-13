#!/usr/bin/env python3
"""
Apply or revert the ESP32-HUB75-MatrixPanel-DMA I2S parallel DMA divider patch.

Some Tidbyt Gen1 / Gen2 panels need the divider calculated from the requested
freq instead of the upstream 2-or-4 lookup:

    From: unsigned int _div_num = (freq > 8000000) ? 2:4;
    To:   unsigned int _div_num = 80000000L / freq / 2;

The patch is opt-in per build via CONFIG_PATCH_I2S_DIVIDER. This script is
invoked from the top-level CMakeLists.txt with --apply or --revert.

A `.orig` sidecar is saved on first apply so revert can restore the original
verbatim regardless of which line endings or trailing whitespace upstream uses.
"""

import argparse
import re
import shutil
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET_FILENAME = "esp32_i2s_parallel_dma.cpp"

PATCHED_MARKER = "unsigned int _div_num = 80000000L / freq / 2;"
ORIGINAL_PATTERN = re.compile(
    r"unsigned int _div_num = \(freq > 8000000\) \? 2:4;.*"
)


def find_target_files() -> list[Path]:
    managed = REPO_ROOT / "managed_components"
    if not managed.is_dir():
        return []
    return list(managed.rglob(TARGET_FILENAME))


def apply_patch(path: Path) -> bool:
    content = path.read_text()
    if PATCHED_MARKER in content:
        print(f"[patch_i2s_divider] already patched: {path.relative_to(REPO_ROOT)}")
        return True
    if not ORIGINAL_PATTERN.search(content):
        print(
            f"[patch_i2s_divider] WARNING: divider pattern not found in "
            f"{path.relative_to(REPO_ROOT)}; upstream may have changed"
        )
        return False
    backup = path.with_suffix(path.suffix + ".orig")
    if not backup.exists():
        shutil.copy2(path, backup)
    path.write_text(ORIGINAL_PATTERN.sub(PATCHED_MARKER, content))
    print(f"[patch_i2s_divider] applied: {path.relative_to(REPO_ROOT)}")
    return True


def revert_patch(path: Path) -> bool:
    content = path.read_text()
    if PATCHED_MARKER not in content:
        return True
    backup = path.with_suffix(path.suffix + ".orig")
    if not backup.exists():
        print(
            f"[patch_i2s_divider] WARNING: cannot revert {path.relative_to(REPO_ROOT)} "
            f"— no .orig backup found. Delete managed_components/ and reconfigure "
            f"to restore the upstream file."
        )
        return False
    shutil.copy2(backup, path)
    print(f"[patch_i2s_divider] reverted: {path.relative_to(REPO_ROOT)}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--apply", action="store_true", help="apply the patch (default)")
    group.add_argument("--revert", action="store_true", help="restore the upstream file")
    args = parser.parse_args()
    do_revert = args.revert

    targets = find_target_files()
    if not targets:
        print(
            "[patch_i2s_divider] managed_components/ not populated yet; "
            "skipping (will run again on next configure)"
        )
        return 0

    ok = True
    for path in targets:
        ok = (revert_patch(path) if do_revert else apply_patch(path)) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
