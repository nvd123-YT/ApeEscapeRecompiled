# Development-only mod packages

Packages in this directory are scanned for callback/manifest consistency by
`AUDIT_MOD_DIRS`, but are not copied into player release builds. A package
must be deliberately promoted into `mods/preloaded/packages` after its
behavior and safety have been qualified.

Current development-only package:
- `ape.enhancement.frame-smoothing`: experimental interpolated rendering.
  It remains here pending hardware validation of smoothness, stage transitions,
  pause/resume, save/load and audio synchronization.
