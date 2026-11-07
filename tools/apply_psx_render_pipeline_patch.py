#!/usr/bin/env python3
"""Enable PSX render/present threading and VRR Settings capabilities.

The shared launcher gates Render thread and Present thread behind
GameInfo.has_render_pipeline and VRR behind GameInfo.has_vsync_vrr. The PSX
runtime implements these settings; Ape Escape opts into both capabilities.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

PROFILE_REL = Path("src/consoles/psx/psx_profile.h")
TEST_REL = Path("tests/psx_profile_test.c")

PROFILE_OLD = "    gi->has_window_size = 1; gi->has_renderer = 1; gi->has_supersampling = 0;"
PROFILE_V1 = (
    "    gi->has_window_size = 1; gi->has_renderer = 1;\n"
    "    gi->has_render_pipeline = 1;\n"
    "    gi->has_supersampling = 0;"
)
PROFILE_NEW = (
    "    gi->has_window_size = 1; gi->has_renderer = 1;\n"
    "    gi->has_render_pipeline = 1;\n"
    "    gi->has_vsync_vrr = 1;\n"
    "    gi->has_supersampling = 0;"
)

TEST_OLD = "    assert(game.has_renderer == 1);"
TEST_V1 = TEST_OLD + "\n    assert(game.has_render_pipeline == 1);"
TEST_NEW = TEST_V1 + "\n    assert(game.has_vsync_vrr == 1);"


def update(path: Path, old_variants: tuple[str, ...], new: str, check: bool) -> None:
    if not path.is_file():
        raise RuntimeError(f"required launcher source is missing: {path}")
    source = path.read_text(encoding="utf-8")
    if new in source:
        return
    candidates = sorted(
        (old for old in old_variants if old in source),
        key=len,
        reverse=True,
    )
    if not candidates:
        raise RuntimeError(f"source anchor drift in {path}: no known prior form found")
    old = candidates[0]
    if source.count(old) != 1:
        raise RuntimeError(f"source anchor is ambiguous in {path}")
    if check:
        raise RuntimeError(f"PSX render-pipeline/VRR patch is not applied: {path}")
    path.write_text(source.replace(old, new, 1), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ui-root", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        update(args.ui_root / PROFILE_REL, (PROFILE_V1, PROFILE_OLD), PROFILE_NEW, args.check)
        update(args.ui_root / TEST_REL, (TEST_V1, TEST_OLD), TEST_NEW, args.check)
    except (OSError, RuntimeError) as exc:
        print(f"PSX render-pipeline/VRR patch: {exc}", file=sys.stderr)
        return 1
    print("PSX render-pipeline/VRR patch verified." if args.check
          else "PSX render-pipeline Settings and VRR enabled.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
