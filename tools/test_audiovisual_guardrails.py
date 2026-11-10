#!/usr/bin/env python3
"""Regression checks for PGXP, native geometry presentation, and faithful defaults."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
presentation_source = (ROOT / "src/mods/ape_presentation_capture.cpp").read_text(encoding="utf-8")
for token in (
    "void activate_live()",
    "gpu_capture=active.load();live_enabled=active.load();",
    "psx_presentation_gpu_set_provider(live_present,nullptr);",
):
    if token not in presentation_source:
        raise SystemExit("Geometry Interpolation lost a required native-presentation hook: " + token)

config = (ROOT / "game.toml").read_text(encoding="utf-8")
if 'aspect_ratio = "4:3"' not in config:
    raise SystemExit("The stock 4:3 presentation default was lost.")
if "pgxp_mod_only = true" not in config:
    raise SystemExit("PGXP must be controlled by its single optional mod feature.")
if not re.search(r"(?m)^pgxp_tolerance\s*=\s*-1(?:\.0)?\s*$", config):
    raise SystemExit("The hook-flavor PGXP runtime must not clamp precise vertices back to integer pixels.")
if re.search(r"(?m)^pgxp_tolerance\s*=\s*0\.5\s*$", config):
    raise SystemExit("The stale 0.5 px tolerance clamp must not return.")
if not re.search(r"(?m)^pgxp_position_fallback\s*=\s*false\s*$", config):
    raise SystemExit("The high-coverage hook build must use dataflow-only precision.")
if not re.search(r"(?m)^pgxp_preserve_projection\s*=\s*true\s*$", config):
    raise SystemExit("The high-coverage hook build must preserve the exact GTE projection.")
if not re.search(r"(?m)^pgxp_depth_buffer\s*=\s*true\s*$", config):
    raise SystemExit("PGXP seam expansion requires the renderer depth buffer.")
if not re.search(r'(?m)^pgxp_seam\s*=\s*"wide"\s*$', config):
    raise SystemExit("Ape Escape must enable wide PGXP seam expansion above native resolution.")

pins = (ROOT / "framework_pins.txt").read_text(encoding="utf-8").splitlines()
framework_pins = [line.split("=", 1)[1] for line in pins if line.startswith("psxrecomp-v4=")]
if len(framework_pins) != 1 or not re.fullmatch(r"[0-9a-f]{40}", framework_pins[0]):
    raise SystemExit("framework_pins.txt must declare exactly one valid PSXRecomp framework pin.")
expected_framework_pin = framework_pins[0]
framework = ROOT / "psxrecomp-v4"
if (framework / ".git").exists():
    checked_out_pin = subprocess.run(
        ["git", "-C", str(framework), "rev-parse", "HEAD"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    if checked_out_pin != expected_framework_pin:
        raise SystemExit(
            f"Framework checkout ({checked_out_pin}) disagrees with framework_pins.txt ({expected_framework_pin})."
        )

geometry_manifest = ROOT / "mods/preloaded/packages/ape.enhancement.geometry-interpolation/1.0.0/manifest.toml"
if not geometry_manifest.is_file():
    raise SystemExit("Geometry Interpolation must be present in the release catalog.")
geometry_text = geometry_manifest.read_text(encoding="utf-8")
if "default_enabled = false" not in geometry_text or 'id = "ape.presentation.capture"' not in geometry_text:
    raise SystemExit("Geometry Interpolation must remain default-off and use its native presenter.")
legacy_frame_manifest = ROOT / "mods/development/packages/ape.enhancement.frame-smoothing/1.0.0/manifest.toml"
if legacy_frame_manifest.exists():
    raise SystemExit("The incompatible render-pass frame-smoothing package must be retired.")
if not (ROOT / "tests/test_presentation_capture.cpp").is_file():
    raise SystemExit("Keep the white-box geometry-capture regression test.")

manifest = (ROOT / "mods/preloaded/packages/psx.enhancement.pgxp/1.0.0/manifest.toml").read_text(encoding="utf-8")
if "default_enabled = false" not in manifest or 'default = "true"' not in manifest:
    raise SystemExit("PGXP must remain optional/off by default while CPU precision propagation is on when selected.")
option_blocks = re.findall(r"(?ms)^\[\[option\]\]\n.*?(?=^\[\[(?:option|plugin)\]\]|\Z)", manifest)
culling_blocks = [block for block in option_blocks if re.search(r'(?m)^id = "culling"$', block)]
if len(culling_blocks) != 1 or not re.search(r'(?m)^default = "true"$', culling_blocks[0]):
    raise SystemExit("Precise culling must be available and default on only when the optional PGXP mod is selected.")

for option_id in ("geometry", "perspective"):
    blocks = [block for block in option_blocks if ('id = "' + option_id + '"') in block]
    if len(blocks) != 1 or not re.search(r'(?m)^default = "true"$', blocks[0]):
        raise SystemExit("PGXP " + option_id + " must preserve the combined-correction default and remain independently configurable.")

resolution_manifest = ROOT / "mods/preloaded/packages/psx.enhancement.internal-resolution/1.0.0/manifest.toml"
if resolution_manifest.exists():
    raise SystemExit("Internal Resolution is framework-owned; remove the title-local package shadow.")
if not re.search(r'(?m)^internal_resolution\s*=\s*"4k"\s*$', config):
    raise SystemExit("Ape Escape's 4K baseline must remain the framework Internal Resolution Game default.")

ws_manifest = (ROOT / "mods/preloaded/packages/ape.enhancement.widescreen/1.0.0/manifest.toml").read_text(encoding="utf-8")
if "Experimental" not in ws_manifest or "default_enabled = false" not in ws_manifest:
    raise SystemExit("Widescreen must remain explicitly experimental and off by default.")
issues = (ROOT / "ISSUES.md").read_text(encoding="utf-8")
for title in ("Widescreen: distance (draw-distance) pop-in", "circus balls pop/cull"):
    if title not in issues:
        raise SystemExit(f"Known unverified audiovisual issue was removed: {title}")
print("AV guardrails passed: PGXP depth/seam support is configured, Geometry Interpolation is default-off with native fallback coverage, and Widescreen remains experimental/off by default.")
