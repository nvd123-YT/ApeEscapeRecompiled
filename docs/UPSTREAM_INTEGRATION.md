# Upstream framework integration

This project is maintained independently in `nvd123-YT/ApeEscapeRecompiled`.
Upstream repositories are reference inputs only; integration commits are made
to this repository and must never be pushed to another project.

## Qualified baseline

- PSXRecomp: `RetroPortingToolKit/psxrecomp@1b84d4fd3a1c65f64c2d97b49320f769ba2e01fc`
  (the framework pin used by mstan's Ape Escape v0.5.7 release).
- recomp-ui: `RetroPortingToolKit/recomp-ui@cf6315ad80af07d89b2ffc8656aeb5f819dd2baf`
  (current upstream master as of November 10, 2026; retains our renderer/threading/VRR
  capability patch and adds quick-save/load hotkeys, mod-option binding capture,
  controller selection fixes, memory-card slot limits, and C++20 UTF-8 fixes).
- Nested PSXRecomp dependencies: `recomp-net@7a4d330ecf995b881d167234da3c0b01675fd512`
  and `retcomm-rbengine@28a2a472986a7d692d0378a22e756b0936413507`.

## Compatibility decisions

- The former frame-replay plugin is preserved under
  `archive/legacy-frame-interpolation`. It is excluded from CMake's active
  plugin source glob because the new framework retired its replay APIs.
- The replacement Geometry Interpolation feature is opt-in and default-off.
  It captures explicit game geometry and uses native presentation for
  unsupported scenes. It does not claim display-rate scheduling.
- The upstream Prop Visibility Correction is included as a hidden visual fix.
- The project's custom PGXP projection extension is retained and its
  pin-checked patch now targets the qualified PSXRecomp revision. It preserves
  SF=0 support and LM-aware IR saturation validation alongside upstream's
  strict GTE divide-overflow guard.
- Existing widescreen, 4K baseline, PGXP depth/seam configuration, ordered HLE
  batching, native idle-wait selection, launcher updater/theme, platform
  adaptations and save-management features remain project-owned.

## Validation status

The geometry-capture white-box test and mod catalog test are part of CTest.
A real-game comparison is still required for opening-level motion, camera turns,
jumps, transitions, save/load, visual continuity and performance on each
supported backend. No new frame-rate or CPU-speed gain is claimed without
measured, repeatable package-level tests.

The newer PSXRecomp master contains changes beyond this release-qualified pin,
including changes that retire presentation-capture APIs required by the Geometry
Interpolation port. Those commits should be evaluated separately rather than
silently fast-forwarded over this project's PGXP, renderer, platform and
packaging adaptations.

The `recomp-ui` custom patch was checked against the selected UI master for its
required launcher/theme/model/keyboard anchors. Online lobby sign-in is required
by that upstream UI; LAN play remains separate. The project-specific launcher
branding, accessibility, soundtrack and updater patches remain applied at build
time and must pass CI after each UI pin change.
