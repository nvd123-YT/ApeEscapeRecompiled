# Upstream framework integration

This project is maintained independently in `nvd123-YT/ApeEscapeRecompiled`.
Upstream repositories are reference inputs only; integration commits are made
to this repository and must never be pushed to another project.

## Qualified baseline

- PSXRecomp: `RetroPortingToolKit/psxrecomp@1b84d4fd3a1c65f64c2d97b49320f769ba2e01fc`
  (the framework pin used by mstan's Ape Escape v0.5.7 release).
- recomp-ui: `RetroPortingToolKit/recomp-ui@fc218ee9d3e2acfcc80bd45a6b2624980bec02b3`
  (our newer renderer/threading/VRR settings revision; intentionally retained
  instead of downgrading to the v0.5.7 UI pin).
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

The newer PSXRecomp master contains changes beyond this release-qualified pin.
Those commits should be evaluated separately rather than silently fast-forwarded
over this project's PGXP, renderer, platform and packaging adaptations.
