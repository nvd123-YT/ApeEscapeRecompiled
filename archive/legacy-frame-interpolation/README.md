# Archived replay-based frame interpolation

The original replay-based Interpolated Rendering experiment is kept here for
historical reference only. It is deliberately outside `src/mods` and is not
compiled, packaged, registered, or exposed in the launcher.

The updated PSXRecomp framework retired the old render-pass replay API. Do not
move this source back into the active source glob. The supported replacement is
the default-off Geometry Interpolation package, which captures original camera,
model and terrain geometry and leaves guest simulation/audio cadence unchanged.

The archived manifest and README are retained to document the former package's
feature IDs and settings. They are not scanned by `AUDIT_MOD_DIRS`.
