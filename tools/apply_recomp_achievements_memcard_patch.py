#!/usr/bin/env python3
"""Hook completed guest-initiated memory-card reads for title save-load events."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

SIO_REL = Path("runtime/src/sio.c")
OLD = """        card_read_summary_record(mc_slot, mc_cmd, mc_sector,
                                 mc_checksum, mc_data_idx, mc_data);
"""
NEW = OLD + """#if !defined(__EMSCRIPTEN__)
        /* The sector has now completed its guest-visible SIO read. The title
         * observer validates its own save marker before reporting a load. */
        if (mc_sector < MEMCARD_SECTORS) {
            extern void ape_recomp_achievements_memcard_read_complete(
                int card, uint16_t sector, const uint8_t *data128);
            ape_recomp_achievements_memcard_read_complete(
                (int)mc_slot, mc_sector, mc_data);
        }
#endif
"""


def update(path: Path, check: bool) -> None:
    if not path.is_file():
        raise RuntimeError(f"required framework source is missing: {path}")
    source = path.read_text(encoding="utf-8-sig")
    if NEW in source:
        return
    if source.count(OLD) != 1:
        raise RuntimeError(f"memory-card SIO completion anchor drifted in {path}")
    if check:
        raise RuntimeError(f"memory-card save-load callback patch is not applied: {path}")
    path.write_text(source.replace(OLD, NEW, 1), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--framework-root", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        update(args.framework_root / SIO_REL, args.check)
    except (OSError, RuntimeError) as exc:
        print(f"RecompLauncher memory-card callback patch: {exc}", file=sys.stderr)
        return 1
    print("RecompLauncher memory-card callback patch verified." if args.check
          else "RecompLauncher memory-card callback enabled.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
