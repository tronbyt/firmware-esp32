#!/usr/bin/env python3
"""
Add a runtime switch for a true 8MHz HUB75 pixel clock to the bundled
ESP32-HUB75-MatrixPanel-DMA library.

Upstream picks the I2S divider with a 2-or-4 lookup, so the requested 8MHz
runs at 10MHz (mrcodetastic/ESP32-HUB75-MatrixPanel-DMA#816), which shows red
artifacts on some Tidbyt Gen1 / Gen2 panels. The patch keeps the upstream
divider unless the firmware sets `hub75_i2s_clock_8mhz` (from the clock_8mhz
setting) before begin():

    From: unsigned int _div_num = (freq > 8000000) ? 2:4;
    To:   extern bool hub75_i2s_clock_8mhz; unsigned int _div_num =
              hub75_i2s_clock_8mhz ? 80000000L / freq / 2 : ((freq > 8000000) ? 2:4);

Fault tolerant: the file is only touched when it matches the upstream code
this was written against. Anything else is left alone with a warning and the
build carries on with the stock divider (clock_8mhz then has no effect).
Exit code is 0 when patched, 1 when skipped; CMake only warns on 1.

A `.orig` sidecar is saved on first apply; --revert restores it.
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TARGET_FILENAME = "esp32_i2s_parallel_dma.cpp"
TAG = "[patch_i2s_divider]"

ORIGINAL_LINE = "unsigned int _div_num = (freq > 8000000) ? 2:4;"
PATCHED_LINE = (
    "extern bool hub75_i2s_clock_8mhz; unsigned int _div_num = "
    "hub75_i2s_clock_8mhz ? 80000000L / freq / 2 : ((freq > 8000000) ? 2:4);"
)
# Earlier version of this script replaced the divider unconditionally.
LEGACY_PATCHED_LINE = "unsigned int _div_num = 80000000L / freq / 2;"

# Upstream 3.0.14 has the divider line twice (ESP32-S2 and ESP32 paths), and
# the patched value only means what we expect if these are still there.
EXPECTED_COUNT = 2
REQUIRED_ANCHORS = (
    "void Bus_Parallel16::config(",
    "dev->clkm_conf.clkm_div_num = _div_num;",
    "dev->sample_rate_conf.tx_bck_div_num = 2;",
)


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(REPO_ROOT))
    except ValueError:
        return str(path)


def find_target_files() -> list[Path]:
    managed = REPO_ROOT / "managed_components"
    if not managed.is_dir():
        return []
    return list(managed.rglob(TARGET_FILENAME))


def matches_upstream(content: str) -> str | None:
    """Return None if content is the upstream code we know, else the reason."""
    found = content.count(ORIGINAL_LINE)
    if found != EXPECTED_COUNT:
        return f"expected {EXPECTED_COUNT} divider lines, found {found}"
    for anchor in REQUIRED_ANCHORS:
        if anchor not in content:
            return f"missing '{anchor}'"
    return None


def write_atomic(path: Path, content: str) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(content, encoding="utf-8")
    os.replace(tmp, path)


def apply_patch(path: Path) -> bool:
    content = path.read_text(encoding="utf-8")
    backup = path.with_suffix(path.suffix + ".orig")

    if content.count(PATCHED_LINE) == EXPECTED_COUNT and ORIGINAL_LINE not in content:
        print(f"{TAG} already patched: {rel(path)}")
        return True

    if LEGACY_PATCHED_LINE in content:
        if not backup.exists():
            print(
                f"{TAG} WARNING: {rel(path)} has the old always-8MHz patch and no "
                f".orig to restore; delete managed_components/ and reconfigure"
            )
            return False
        content = backup.read_text(encoding="utf-8")
        print(f"{TAG} replacing old always-8MHz patch in {rel(path)}")

    reason = matches_upstream(content)
    if reason:
        print(
            f"{TAG} WARNING: not patching {rel(path)} ({reason}); upstream has "
            f"changed, building with the stock divider and clock_8mhz will have "
            f"no effect"
        )
        return False

    patched = content.replace(ORIGINAL_LINE, PATCHED_LINE)
    if patched.count(PATCHED_LINE) != EXPECTED_COUNT or matches_upstream(
        patched.replace(PATCHED_LINE, ORIGINAL_LINE)
    ):
        print(f"{TAG} WARNING: patch did not produce the expected result; skipping")
        return False

    if not backup.exists():
        shutil.copy2(path, backup)
    write_atomic(path, patched)
    print(f"{TAG} applied: {rel(path)}")
    return True


def revert_patch(path: Path) -> bool:
    content = path.read_text(encoding="utf-8")
    if PATCHED_LINE not in content and LEGACY_PATCHED_LINE not in content:
        return True
    backup = path.with_suffix(path.suffix + ".orig")
    if not backup.exists():
        print(
            f"{TAG} WARNING: cannot revert {rel(path)}, no .orig backup found. "
            f"Delete managed_components/ and reconfigure to restore the upstream file."
        )
        return False
    shutil.copy2(backup, path)
    print(f"{TAG} reverted: {rel(path)}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--apply", action="store_true", help="apply the patch (default)")
    group.add_argument("--revert", action="store_true", help="restore the upstream file")
    args = parser.parse_args()

    targets = find_target_files()
    if not targets:
        print(
            f"{TAG} managed_components/ not populated yet; "
            f"skipping (will run again on next configure)"
        )
        return 0

    ok = True
    for path in targets:
        try:
            success = revert_patch(path) if args.revert else apply_patch(path)
        except Exception as exc:  # never break the build over this patch
            print(f"{TAG} WARNING: {rel(path)}: {exc}")
            success = False
        ok = ok and success
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
