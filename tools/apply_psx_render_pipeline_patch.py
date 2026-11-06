#!/usr/bin/env python3
"""Idempotently enable the PSX render/present pipeline Settings rows.

The shared launcher intentionally gates these controls behind GameInfo's
has_render_pipeline capability. PSX has the implementation; this title opts in
so OpenGL users can configure render-thread and present-thread behavior.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

PROFILE_REL = Path("src/consoles/psx/psx_profile.h")
TEST_REL = Path("tests/psx_profile_test.c")
PROFILE_OLD = "    gi->has_window_size = 1; gi->has_renderer = 1; gi->has_supersampling = 0;"
PROFILE_NEW = (
    "    gi->has_window_size = 1; gi->has_renderer = 1;\n"
    "    gi->has_render_pipeline = 1;\n"
    "    gi->has_supersampling = 0;"
)
TEST_OLD = "    assert(game.has_renderer == 1);"
TEST_NEW = TEST_OLD + "\n    assert(game.has_render_pipeline == 1);"


def update(path: Path, old: str, new: str, check: bool) -> None:
    if not path.is_file():
        raise RuntimeError(f"required launcher source is missing: {path}")
    source = path.read_text(encoding="utf-8")
    if new in source:
        return
    count = source.count(old)
    if count != 1:
        raise RuntimeError(
            f"source anchor drift in {path}: expected one known anchor, found {count}"
        )
    if check:
        raise RuntimeError(f"render-pipeline patch is not applied: {path}")
    path.write_text(source.replace(old, new, 1), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ui-root", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        update(args.ui_root / PROFILE_REL, PROFILE_OLD, PROFILE_NEW, args.check)
        update(args.ui_root / TEST_REL, TEST_OLD, TEST_NEW, args.check)
    except (OSError, RuntimeError) as exc:
        print(f"PSX render-pipeline patch: {exc}", file=sys.stderr)
        return 1
    print("PSX render-pipeline patch verified." if args.check
          else "PSX render-pipeline Settings enabled.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
