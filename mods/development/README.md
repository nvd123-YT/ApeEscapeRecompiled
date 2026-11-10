# Development-only mod packages

Packages in this directory are scanned for callback/manifest consistency by
`AUDIT_MOD_DIRS`, but are not copied into player release builds.

The previous render-pass frame-replay implementation has been retired because
the updated PSXRecomp presentation API replaces that mechanism. Its source is
archived at `src/development/legacy_frame_rate_plugin.cpp` for reference.
The supported Geometry Interpolation implementation is now a separate,
default-off package in `mods/preloaded`.
