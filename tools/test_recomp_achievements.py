#!/usr/bin/env python3
"""Static contract checks for the RecompLauncher achievement integration."""
import json
import tomllib
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
plugin = (ROOT / "src/mods/ape_recomp_achievements_plugin.c").read_text(encoding="utf-8")
assist = (ROOT / "src/mods/ape_gameplay_assist_plugin.c").read_text(encoding="utf-8")
manifest = tomllib.loads((ROOT / "mods/preloaded/packages/ape.enhancement.recomp-achievements/1.0.0/manifest.toml").read_text(encoding="utf-8"))
achievements = json.loads((ROOT / "recomp/launcher/recomp-achievements.json").read_text(encoding="utf-8"))
catalogue = (ROOT / "recomp/launcher/monkepedia.tsv").read_text(encoding="utf-8").splitlines()

if manifest.get("id") != "ape.enhancement.recomp-achievements":
    raise SystemExit("wrong achievement package id")
features = manifest.get("feature", [])
if len(features) != 1 or features[0].get("id") != "recomp-achievements" or features[0].get("default_enabled") is not True:
    raise SystemExit("RecompLauncher achievements must be default-enabled and non-gameplay")
plugins = manifest.get("plugin", [])
if len(plugins) != 1 or plugins[0].get("id") != "ape.recomp-achievements":
    raise SystemExit("achievement plugin is not mapped from its feature")

if achievements.get("format") != 1 or achievements.get("source") != "events" or achievements.get("console") != "psx":
    raise SystemExit("achievement list must use format 1, PS1, and code events")
if achievements.get("game") != "apeescaperecompiled":
    raise SystemExit("RecompLauncher game ID must be apeescaperecompiled")
if 'rl_ach_set_info("apeescaperecompiled", "2.0.0")' not in plugin:
    raise SystemExit("runtime game ID must match the achievement list")
if "APE_MONKEYS_PER_STAGE         24u" not in plugin:
    raise SystemExit("live catch tracking must include 24-monkey Peak Point Matrix")
entries = achievements.get("achievements", [])
if len(entries) != 34:
    raise SystemExit(f"expected 34 achievements (22 stages + 8 gadgets + 4 monkey milestones), found {len(entries)}")
ids = [entry.get("id") for entry in entries]
events = [entry.get("event") for entry in entries]
if len(set(ids)) != len(ids) or len(set(events)) != len(events):
    raise SystemExit("achievement IDs and events must be unique")

stage_events = {entry["event"] for entry in entries if entry["event"].startswith("complete_")}
gadget_events = {entry["event"] for entry in entries if entry["event"].startswith("unlock_")}
monkey_events = {entry["event"] for entry in entries if entry["event"].startswith("caught_")}
if len(stage_events) != 22 or len(gadget_events) != 8 or monkey_events != {
    "caught_one_monkey", "caught_10_monkeys", "caught_100_monkeys", "caught_all_204_monkeys"
}:
    raise SystemExit("stage, gadget, or monkey milestone coverage is incomplete")

rows = [line.split("\t") for line in catalogue if line and not line.startswith("#")]
if len(rows) != 204:
    raise SystemExit(f"expected 204 catalogue monkeys, found {len(rows)}")
stage_counts = Counter(int(row[0]) for row in rows)
expected_stage_counts = {
    0: 4, 1: 6, 2: 7, 10: 14, 11: 13, 12: 8, 20: 8, 21: 8, 22: 11,
    30: 6, 31: 9, 32: 9, 40: 12, 41: 10, 42: 20, 50: 13, 51: 10,
    52: 12, 60: 24,
}
if dict(stage_counts) != expected_stage_counts or sum(stage_counts.values()) != 204:
    raise SystemExit("monkey catalogue no longer matches the verified 19-stage / 204-monkey roster")

for token in (
    "APE_MONKEY_FLAGS_ADDRESS      0x80139368u",
    "APE_GADGET_MASK_ADDRESS       0x800F51C4u",
    "0x800DFC71u", "0x800DFC72u", "0x800DFC73u", "0x800DFC74u",
    "0x800DFC75u", "0x800DFC76u", "0x800DFC77u", "0x800DFC78u",
    "0x800DFC79u", "0x800DFC7Au", "0x800DFC7Bu", "0x800DFC7Cu",
    "0x800DFC7Du", "0x800DFC7Eu", "0x800DFC7Fu", "0x800DFC80u",
    "0x800DFC81u", "0x800DFC84u", "0x800DFC85u", "0x800DFC86u",
    "0x800DFC88u", "0x800DFC8Eu",
):
    if token not in plugin:
        raise SystemExit(f"missing Ape Escape RAM mapping: {token}")

for token in (
    "APE_CARD_DATA_SECTOR    3u",
    "APE_CARD_MARKER_OFFSET  0x16u",
    "APE_CARD_GADGET_OFFSET  0x04u",
    "APE_CARD_MONKEY_OFFSET  0x49u",
    "0x7Fu", "0x4Bu", "ape_recomp_achievements_memcard_read_complete",
    "caught_all_204_monkeys",
):
    if token not in plugin:
        raise SystemExit(f"missing memory-card progress guard: {token}")
if "ape_read_saved_monkeys" in plugin or "ape_update_saved_progress" in plugin:
    raise SystemExit("startup card scans must not be used to award progress")
if "!g_stage_previous_completed[i] && completed" not in plugin:
    raise SystemExit("stage achievements must require an in-session completion edge")
if "(g_previous_gadget_mask & mask) == 0u" not in plugin:
    raise SystemExit("gadget achievements must require an in-session unlock edge")
if "g_previous_monkey_flags[i] != 1u && current[i] == 1u" not in plugin:
    raise SystemExit("monkey milestones must count newly caught monkeys only")
callback_start = plugin.index("void ape_recomp_achievements_memcard_read_complete(")
callback_end = plugin.index("static void ape_snapshot_progress(", callback_start)
if "rl_ach_save_loaded();" not in plugin[callback_start:callback_end]:
    raise SystemExit("memory-card save loads must notify RecompLauncher")
if "if (g_pending_save_load)" not in plugin:
    raise SystemExit("pre-initialization memory-card loads must be reported after SDK init")
if "g_pending_snapshot_idle_frames < 3u" not in plugin:
    raise SystemExit("loaded progress must settle after card I/O before baselines are sampled")
if "psx_mod_read_byte(APE_PHASE_ADDRESS) == APE_PHASE_PLAYING" not in plugin:
    raise SystemExit("loaded progress must be baselined on resumed gameplay")
if assist.count('ape_recomp_achievements_mark_casual("cheats")') != 5:
    raise SystemExit("all five Gameplay Assist cheat features must mark achievements Casual")

print("RecompLauncher achievements: 22 stages, 8 gadgets, 4 monkey milestones, and 204 catalogue monkeys verified.")
