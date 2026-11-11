cmake_minimum_required(VERSION 3.20)
project(ApeEscapeRecomp C CXX)
include(CTest)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp-v4" CACHE PATH
    "Path to psxrecomp")
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui" CACHE PATH
    "Root directory of the shared recomp-ui launcher")

# Use PSXRecomp's existing ordered HLE textured batching for fresh enhanced builds.
# Reference builds keep LLE, and an explicit -DPSX_GL_BATCH_IMPL=LLE override wins.
# Use separate build directories when changing execution profiles or batching tiers.
if(PSX_EXECUTION_PROFILE STREQUAL "REFERENCE")
    set(_ape_gl_batch_default "LLE")
else()
    set(_ape_gl_batch_default "HLE")
endif()
set(PSX_GL_BATCH_IMPL "${_ape_gl_batch_default}" CACHE STRING
    "OpenGL textured batching: LLE or HLE")
unset(_ape_gl_batch_default)

# Add the nvd123 Nightly updater action to the pinned shared launcher without
# forking its submodule. The patch is idempotent and fails closed if upstream
# changes the known source anchors.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_recomp_ui_update_patch.py"
            --ui-root "${RECOMP_UI_ROOT}"
            --project-root "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE APE_UPDATER_PATCH_RESULT
)
if(NOT APE_UPDATER_PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not apply the nvd123 Nightly updater launcher patch.")
endif()

# The shared PSX profile hides render/present threading unless a host opts in.
# Ape Escape uses OpenGL by default and exposes these advanced latency controls.
execute_process(
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_psx_render_pipeline_patch.py"
            --ui-root "${RECOMP_UI_ROOT}"
    RESULT_VARIABLE APE_PSX_RENDER_PIPELINE_PATCH_RESULT
)
if(NOT APE_PSX_RENDER_PIPELINE_PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not enable the PSX render-pipeline Settings controls.")
endif()

# Apply the audited PGXP projection correction to the pinned framework itself.
# The patch is idempotent, validates the framework pin, and fails closed if its
# source anchors change, keeping local CMake builds in sync with CI.
execute_process(
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_pgxp_projection_patch.py"
            --framework-root "${PSXRECOMP_ROOT}"
    RESULT_VARIABLE APE_PGXP_PROJECTION_PATCH_RESULT
)
if(NOT APE_PGXP_PROJECTION_PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not apply the PGXP projection correction to the pinned framework.")
endif()

# Android loads a shared SDL entry-point library and needs writable app-private
# storage. The patch is guarded and does not touch source on desktop builds.
if(ANDROID)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_android_runtime_patch.py"
                --framework-root "${PSXRECOMP_ROOT}"
        RESULT_VARIABLE APE_ANDROID_RUNTIME_PATCH_RESULT
    )
    if(NOT APE_ANDROID_RUNTIME_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not apply the Android runtime portability patch.")
    endif()
endif()


# Experimental WebAssembly requires browser-specific fibers, graphics and filesystem support.
# This mutates the checked-out submodule only during Emscripten configure; desktop builds are unchanged.
if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".js" CACHE INTERNAL "Emscripten JavaScript loader output" FORCE)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_webassembly_runtime_patch.py"
                --framework-root "${PSXRECOMP_ROOT}"
                --project-root "${CMAKE_CURRENT_SOURCE_DIR}"
                --build-root "${CMAKE_BINARY_DIR}"
        RESULT_VARIABLE APE_WEBASSEMBLY_PATCH_RESULT
    )
    if(NOT APE_WEBASSEMBLY_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not apply the experimental WebAssembly runtime patch.")
    endif()
endif()

# Ape Escape uses two independent controller seats. Enable the shared
# recomp-net transport by default; pass -DAPE_ENABLE_NETPLAY=OFF for LAN-free
# reference builds. PSX_MAX_PLAYERS also exposes the second SIO pad to P2.
option(APE_ENABLE_NETPLAY "Build recomp-net online co-op support" ON)
set(PSX_MAX_PLAYERS 2 CACHE STRING "Ape Escape controller seat count" FORCE)
set(PSX_NETPLAY ${APE_ENABLE_NETPLAY} CACHE BOOL
    "Enable the framework recomp-net integration" FORCE)

include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

# Build the framework's PGXP white-box regression suite against the patched
# runtime sources. The framework's own pgxp_test target lives only in its
# standalone runtime project, so this makes the test available in this build.
add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/pgxp_projection_test"
    "${CMAKE_CURRENT_BINARY_DIR}/pgxp-projection-test"
)

# Current psxrecomp emits the large game translation unit as numbered shards
# (see psxrecomp-v4/SPLITGEN_MIGRATION.md). Keep the monolithic fallback for
# older/generated source drops.
file(GLOB GAME_GENERATED_FULL CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/SCUS_944.23_full_[0-9][0-9].c")
list(SORT GAME_GENERATED_FULL)
if(NOT GAME_GENERATED_FULL)
    set(GAME_GENERATED_FULL
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/SCUS_944.23_full.c")
endif()
set(GAME_GENERATED_DISPATCH
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/SCUS_944.23_dispatch.c")

file(GLOB APE_MOD_PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mods/*.cpp")

# Ape Escape's trusted, game-owned mod catalog. Every feature stays disabled
# until selected on the launcher's Mods page.
#
# The FRAMEWORK stages this: PRELOADED_MODS_DIR below hands it to
# psxrecomp_add_runtime_target(), which wipes <exe-dir>/mods/bundled and copies
# both its own mods/builtin/packages and these packages into it, plus
# mods/preloaded/README.md -> mods/README.md. This repo must NOT copy the tree
# itself. It used to, with a hand-written POST_BUILD copy_directory into
# <exe-dir>/mods, and because that named the destination as a STRING, framework
# commit 4cc04be3's rename of the staged catalog from mods/packages to
# mods/bundled broke it without breaking anything a configure, compile or link
# step can see: the four ape.* packages went on landing in mods/packages, which
# neither the launcher nor Add-ModCatalog reads, and the release packager threw
# "Mod catalog is missing package(s) the sources define". Bead beads-eio.3.101.
set(APE_PRELOADED_MODS "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded")

# Only WebAssembly builds generate and link a bounded static AOT overlay tier.
# Desktop builds retain their existing overlay cache/runtime compiler behavior.
set(APE_BROWSER_STATIC_OVERLAY_ARGS)
if(EMSCRIPTEN)
    set(APE_BROWSER_STATIC_OVERLAY_ARGS
        GAME_OVERLAY_STATIC_C "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c")
endif()

# Select the audited Ape Escape idle-wait sites only when the framework exposes
# the native wait-policy helper. Disable with -DAPE_NATIVE_IDLE_WAIT=OFF for
# reference/baseline comparisons.
option(APE_NATIVE_IDLE_WAIT "Select the audited native frame wait" ON)
set(APE_NATIVE_IDLE_WAIT_SITES "")
if(APE_NATIVE_IDLE_WAIT)
    if(NOT COMMAND psxrecomp_native_idle_wait)
        message(FATAL_ERROR "APE_NATIVE_IDLE_WAIT needs a framework with native wait-policy support")
    endif()
    # SCUS-94423 v_wait uses the target counter at 0x800A9BB0; both sites retain
    # its stack timeout decrement and interrupt edges.
    set(APE_NATIVE_IDLE_WAIT_SITES 0x8002E6C0 0x8002E708)
endif()

psxrecomp_add_runtime_target(psx-runtime
    PGXP
    NATIVE_IDLE_WAIT_SITES ${APE_NATIVE_IDLE_WAIT_SITES}
    GAME_GENERATED_FULL_C "${GAME_GENERATED_FULL}"
    GAME_GENERATED_DISPATCH_C "${GAME_GENERATED_DISPATCH}"
    ${APE_BROWSER_STATIC_OVERLAY_ARGS}
    EXTRAS_SOURCES ${APE_MOD_PLUGIN_SOURCES}
    DEBUG_PORT 4480
    WINDOW_TITLE "ApeEscapeRecomp"
    # No DEFAULT_BIOS_PATH: never bake an absolute build-machine path into a release.
    # The framework bundles OpenBIOS as its default and supports a player-selected
    # retail SCPH-1001 BIOS. The browser launcher explicitly passes its session-local
    # BIOS path with --bios; desktop users can choose the retail image in settings.
    DEFAULT_GAME_CONFIG_PATH "game.toml"
    LAUNCHER_BOXART "${CMAKE_SOURCE_DIR}/recomp/launcher/boxart.tga"
    # Per-game executable name so each title's process is distinct (killing/
    # launching one title by name never hits another's running instance).
    EXE_NAME "ApeEscapeRecomp"
    EXCLUDE_BUILTIN_MODS psx.enhancement.cd-speed psx.enhancement.fast-loading
    # Development packages are audited for callback/manifest consistency but
    # are not copied into the player-facing bundled catalog.
    AUDIT_MOD_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/mods/development"
    PRELOADED_MODS_DIR "${APE_PRELOADED_MODS}"
)


# Settings OST player playback (native desktop only; browser/Android remain unchanged).
# Only the decoder is fetched at build time. Actual soundtrack tracks are downloaded
# individually at runtime and kept in the user's writable data folder.
if(NOT EMSCRIPTEN AND NOT ANDROID)
    include(FetchContent)
    FetchContent_Declare(ape_escape_miniaudio
        URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.22.tar.gz
    )
    FetchContent_GetProperties(ape_escape_miniaudio)
    if(NOT ape_escape_miniaudio_POPULATED)
        FetchContent_Populate(ape_escape_miniaudio)
    endif()
    target_sources(psx-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tools/ape_escape_audio_backend.cpp")
    target_include_directories(psx-runtime PRIVATE "${ape_escape_miniaudio_SOURCE_DIR}")
    if(WIN32 OR MINGW)
        target_link_libraries(psx-runtime PRIVATE ole32 avrt winmm)
    elseif(APPLE)
        target_link_libraries(psx-runtime PRIVATE
            "-framework CoreAudio" "-framework AudioToolbox" "-framework CoreFoundation")
    else()
        find_package(Threads REQUIRED)
        target_link_libraries(psx-runtime PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
    endif()
    add_custom_command(TARGET psx-runtime POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:psx-runtime>/update"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/ape_escape_ost.py"
            "$<TARGET_FILE_DIR:psx-runtime>/update/ape_escape_ost.py"
        VERBATIM
    )
endif()

if(BUILD_TESTING)
    add_executable(ape_stick_response_test
        tests/test_stick_response.c
        "${PSXRECOMP_ROOT}/runtime/src/psx_stick.c"
    )
    target_include_directories(ape_stick_response_test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include"
    )
    if(NOT MSVC)
        target_link_libraries(ape_stick_response_test PRIVATE m)
    endif()
    add_test(
        NAME ape_stick_response_test
        COMMAND "$<TARGET_FILE:ape_stick_response_test>"
    )

    add_executable(ape_preloaded_mods_test
        tests/test_preloaded_mods.cpp
        "${PSXRECOMP_ROOT}/runtime/src/mod_packages.cpp"
        "${PSXRECOMP_ROOT}/runtime/src/crc32.c"
        "${PSXRECOMP_ROOT}/runtime/src/psx_sha256.c"
    )
    target_include_directories(ape_preloaded_mods_test PRIVATE
        "${PSXRECOMP_ROOT}/runtime/include"
        "${PSXRECOMP_ROOT}/recompiler/lib/toml11"
    )
    add_test(
        NAME ape_preloaded_mods_test
        COMMAND "$<TARGET_FILE:ape_preloaded_mods_test>"
                "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded"
                "${CMAKE_CURRENT_SOURCE_DIR}/mods/development"
    )
    # Exercise the actual regenerated PsyQ v_wait caller against the shared
    # original/native induction harness. A clean checkout without generated
    # retail-derived C skips with code 77; release builds generate it first.
    add_test(
        NAME ape_vwait_contract_test
        COMMAND "${Python3_EXECUTABLE}"
                "${PSXRECOMP_ROOT}/runtime/tests/native_wait_caller.py"
                --framework "${PSXRECOMP_ROOT}"
                --cc "${CMAKE_C_COMPILER}"
                --generated "${CMAKE_CURRENT_SOURCE_DIR}/generated"
                --contract "${CMAKE_CURRENT_SOURCE_DIR}/tests/vwait_contract.json"
    )
    set_tests_properties(ape_vwait_contract_test PROPERTIES
        SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
    )

    # Release overlay staging: the shard cache and the overlay toolchain.
    #
    # Two things must reach a release for this title's overlays to run native on
    # a player's machine -- cache/<id>/<compiler>/<arch-abi>/<tag>/*.dll, and
    # overlay_toolchain/python/python.exe, which is the exact file the runtime
    # gates autocompile on (main.cpp tk_present). Omitting either fails NOTHING
    # at runtime: the loader falls back to the dirty-RAM interpreter and reports
    # itself healthy. That is how this title shipped its entire release history
    # with neither while game.toml promised an AOT cache.
    #
    # The test has two halves. The CONTRACT half always runs and needs no build
    # products: it asserts tools/package_release.ps1 delegates to the framework's
    # shared staging module and keeps no private copy of anything the module owns
    # (a locally formatted cache tag, an unpinned toolchain download, a warning
    # where a throw belongs, an AllowNoCache escape hatch). Every drift defect
    # this campaign found is a contract violation, so this is the half that
    # catches them cheaply. The ARTIFACT half validates a real staged tree or
    # release zip when one is present -- set PSX_RELEASE_STAGE or PSX_RELEASE_ZIP,
    # or let it discover release-stage/ and *-windows-x64.zip -- and is skipped
    # (loudly, in the log) otherwise, because producing a package needs the
    # game's disc image and a full Release build and so cannot run in a bare
    # checkout.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(
        NAME ape_release_overlay_staging_test
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_release_overlay_staging.py"
                "${CMAKE_CURRENT_SOURCE_DIR}"
                "${PSXRECOMP_ROOT}"
    )
endif()
