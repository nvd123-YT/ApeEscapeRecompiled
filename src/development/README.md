# Archived frame-rate experiment

`legacy_frame_rate_plugin.cpp` is retained as historical source only. It is
not compiled or registered by the current runtime because it depends on the
removed PSXRecomp render-pass replay API. The default-off Geometry Interpolation
package in `src/mods/ape_presentation_capture.cpp` is the replacement.
