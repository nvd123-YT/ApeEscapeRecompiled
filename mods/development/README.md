# Development-only mod packages

This directory is reserved for packages that are scanned for callback/manifest
consistency by `AUDIT_MOD_DIRS` but are not copied into player release builds.

There are currently no active development packages. The former replay-based
Interpolated Rendering source, manifest and README have been preserved under
`archive/legacy-frame-interpolation` after its runtime replay API was retired.
The supported, optional 60 FPS implementation is now the default-off Geometry
Interpolation package in `mods/preloaded`.
