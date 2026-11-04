# Changelog


All notable changes to Ape Escape Recompiled — Enhanced Edition are recorded here. Nightly release notes are generated from the **Unreleased** section; tagged releases use the section matching their version.

## [Unreleased]

### Fixed
- Bumped PSXRecomp to `4102438ab4c5aadf010878029d0324ef6382e521`, the narrow upstream revision containing the PGXP depth-buffer and seam-expansion renderer, then enabled `pgxp_depth_buffer = true` and `pgxp_seam = "wide"` for high-resolution rendering. Precise culling is enabled only when the optional PGXP mod itself is selected. These changes target remaining terrain cracks; they are not a claim that cutscene wobble, every texture warp, or shaking with PGXP disabled is fixed until matched visual captures confirm it.
- Configured the high-coverage PGXP hook build for dataflow-only vertex precision and exact GTE projection (`pgxp_position_fallback = false`, `pgxp_preserve_projection = true`, `pgxp_tolerance = -1.0`) behind the single default-off PGXP mod switch. This is a source-level correction; live visual confirmation of wobble, seams, and texture warping is still outstanding.
- Added an idempotent, pin-checked clean-room PGXP projection patch that follows DuckStation's relevant GTE math more closely: both SF modes, IR limit-mode clamping, and the H/2 near-depth floor. The upstream PGXP test is now built and run on Windows, macOS, and Linux; this targets precision loss in near/saturated projections, not a guarantee that every scene or cutscene is wobble-free.
- Clarified that Widescreen's current projection-squash path remains experimental and can show geometry/background/culling artifacts; the feature stays off by default and 4:3 remains the reference presentation.
- Restricted experimental interpolated rendering to the active gameplay phase and re-checks the phase before submitting replay passes, avoiding unsafe interpolation of title, loading, briefing, flyover, warp-in, and transition frames.

### Fixed
- Stop cached OST autoplay and manual soundtrack playback before launch/relaunch returns control to the game runtime, preventing music from spilling over the Ape Escape title screen. Launch also disables shuffle autoplay for that session.

### Performance
- Restore the upstream-tested guest-cycle scaling optimization for crowded gameplay, gated to Ape Escape phase 3 at RAM byte 0x800F447C. Recompiled CPU work is charged at scale 2 while VBlank, timers, CD, SPU, and DMA remain on the original PlayStation clock; menus, intros, and transitions keep normal timing.

### Fixed
- Fixed the data-folder button's conflicting ImGui ID and made it open the full user home folder; folder buttons now use unique IDs.

### Fixed
- Fixed the duplicate Open Data Folder ImGui ID; Quick Tools now opens the user's home folder separately from the app-specific data folder, with unique IDs for all folder buttons.

### Fixed
- Fixed accessibility settings being skipped because the helper declaration marker collided with the UI insertion check; accessibility controls now have a dedicated visible launcher page.

### Added
- Added a C/C++ lint workflow that checks formatting on changed lines and excludes generated game translations and third-party submodules.
- Fixed the OST volume slider to show a smooth 0–100% range, and added boot-time shuffle autoplay from locally cached songs only; playback advances to another random cached track when a song ends without downloading during startup. Music files remain in per-user data and are not bundled in releases.
- Mod Manager filters for all, enabled, disabled, experimental, and needs-attention features, plus confirmed Original Experience, Visual Enhancements, and Challenge Run presets.
- Confirmation dialogs for Enable All and Disable All to avoid accidentally changing the whole mod selection.
- Setup Doctor checks for the selected disc, disc identity, custom BIOS path, app-data write access, and save-backup availability, with a copyable diagnostic report and shortcuts to repair actions.
- Controller Test Lab with live gamepad button, stick, and trigger indicators plus an optional rumble test.
- Save Protection Center backup history, selected-snapshot restore, folder export, and configurable retention from 5 to 50 snapshots.
- Update & Release Center with installed build metadata, an in-app updater entry point, Nightly release access, and a changelog link.
- Automated release notes generated from this changelog for Nightly and version-tagged releases.

### Changed
- Save restoration now delays retention pruning until after the chosen backup is applied, preventing a selected old snapshot from being pruned before restore completes.
- The Mods page follows the active filter when selecting feature/package details and confirms bulk toggles before applying them.
- Removed the duplicate Nightly updater button from Update & Release Center; the single dedicated updater control prevents conflicting Dear ImGui IDs and accidental double-starts.


### Fixed
- Moved the OST player entry into Settings → Audio and opened the full player in a separate resizable window, avoiding clipping below the Settings layout.
- Closed the Nightly manifest generator’s missing Python heredoc terminator, which prevented the release-publishing shell step from parsing correctly.
- Corrected the pinned launcher’s malformed SDL3 preprocessor guard, which had stopped Windows, macOS, and Linux builds before release packaging.
- Added independent GitHub Actions workflow validation and security auditing with actionlint and zizmor; zizmor findings are advisory while existing workflows are reviewed.

## [2.0.0] - 2026-11-04

### New launcher tools
- **Ape Escape OST Player:** browse the soundtrack listing, download individual tracks on demand, play/pause, skip, seek, and save volume. Tracks are cached per user and are not bundled with releases.
- **Cached soundtrack shuffle:** optionally play and shuffle tracks already in the local cache, without downloading music during startup.
- **Setup Doctor:** diagnose the selected disc and disc identity, custom BIOS path, application-data write access, and save-backup availability; copy a diagnostic report and jump to repair actions.
- **Controller Test Lab:** inspect live gamepad buttons, sticks, and triggers, with an optional rumble test.
- **Expanded Save Protection Center:** browse timestamped backups, restore a selected snapshot, export a backup to another folder, and set retention from 5 to 50 snapshots.
- **Mod Manager improvements:** search and filter features by enabled, disabled, experimental, and needs-attention status; apply confirmed Original Experience, Visual Enhancements, and Challenge Run presets.
- **Update & Release Center:** see installed build metadata and access the Nightly updater, release downloads, and changelog in one place.
- **Mod Developer Toolkit:** open a copyable native plugin template and developer guide from the launcher. CI syntax-checks the example plugin and checks its launcher integration.
- **Dedicated accessibility page:** adjust launcher text size, high contrast, control size, focus targets, and background visual effects.

### Gameplay, graphics, and performance
- **PGXP improvements:** add renderer depth-buffer and seam-expansion support, more precise culling, and more faithful handling of relevant GTE projection math. PGXP remains optional; this targets specific precision and seam issues and does not claim to eliminate every wobble, texture warp, or visual artifact.
- **Safer interpolated rendering:** restrict experimental frame replay to active gameplay and avoid replaying title, loading, briefing, flyover, warp-in, and transition frames.
- **Crowded-gameplay performance:** restore a narrowly gated guest-cycle optimization for the active gameplay phase while keeping VBlank, timers, CD, SPU, and DMA on the original PlayStation clock. This is a targeted optimization, not a fixed-frame-rate guarantee.
- **Widescreen remains experimental:** 4:3 remains the reference presentation; widescreen can still expose geometry, background, or culling artifacts.

### Reliability and development
- Stop cached OST playback before the game launches or relaunches, preventing music from carrying over onto the title screen.
- Make save restoration safer by creating a safety snapshot first and delaying backup-retention pruning until the selected restore has completed.
- Improve Mod Manager selection behavior and require confirmation before bulk Enable All / Disable All actions.
- Fix launcher folder shortcuts and conflicting UI control IDs.
- Add changed-line C/C++ formatting checks and independent GitHub Actions workflow validation/security auditing.
- Fix release-manifest generation and a pinned launcher SDL3 guard that could stop cross-platform packaging before the build completed.

### Release notes
- Release packages are built for Windows x64, macOS Apple Silicon and Intel, and Linux x86_64 when their respective build jobs succeed.
- The original Ape Escape (USA, SCUS-94423) disc image and retail PlayStation BIOS are not included. Supply your own legally obtained game image; OpenBIOS is included as the default alternative.
- macOS packages are ad-hoc signed and are not notarized. Linux AppImage updates remain manual.
- Soundtrack audio is not bundled. Tracks are downloaded only when selected and cached in the per-user data folder.

## [1.0.0] - 2026-10-17

### Added
- Established the 1.0.0 version line using `packaging/release/VERSION` as the canonical app-version source for desktop and Android builds.
- Added the project-branded launcher, accessibility preferences, manual Monkepedia checklist, Stage Knowledge Cards, Daily Challenge, and Save Protection Center.
- Added default-off optional enhancements and gameplay challenges, including widescreen, PGXP, interpolated rendering, gameplay assists, challenge rules, and quality-of-life tools.
- Added Windows/macOS Nightly update and rollback support, with package checksum verification in the updater.

### Notes
- The base game disc and retail PlayStation BIOS are not included in releases. Users must provide their own game image.
- Linux AppImage updates are installed manually; macOS builds are ad-hoc signed and are not notarized.
