#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdint.h>

/*
 * First-stage two-Spike experiment.
 *
 * This deliberately is NOT the final local-co-op implementation:
 * - P2 follows P1's processed left-stick movement and a small shared button mask.
 * - P2 mirrors P1's active gadget selection; its right-stick input is disabled.
 * - P2's object-collision registrations are suppressed; floor/terrain
 *   movement and its own animation/draw path still run.
 * - P1 remains the camera target and retains its normal update.
 *
 * The game has one global g_Spike and one global g_Pad. We preserve the
 * second character in Expansion 1 and temporarily project that context into
 * the native globals while re-entering Spike_Update. The feature is opt-in.
 */

#define SPIKE_UPDATE_ADDRESS UINT32_C(0x80060780)
#define COLLISION_SET_ADDRESS UINT32_C(0x80071E44)

#define SPIKE_ADDRESS UINT32_C(0x800EC1E0)
#define PAD_ADDRESS UINT32_C(0x800E5320)
#define CAMERA_ADDRESS UINT32_C(0x800E68D0)
#define HUD_ADDRESS UINT32_C(0x800F4DB0)
#define GADGET_ADDRESS UINT32_C(0x800F5120)
#define GADGET_AUX_ADDRESS UINT32_C(0x800F51D0)

#define GAME_PHASE_ADDRESS UINT32_C(0x800F447C)
#define GAME_STAGE_ADDRESS UINT32_C(0x800F4476)
#define GAME_AREA_ADDRESS UINT32_C(0x800F447A)
#define GAME_PLAY_MODE_ADDRESS UINT32_C(0x800F44A0)
#define GAME_PLAY_SUBMODE_ADDRESS UINT32_C(0x800F44A2)

enum {
    SPIKE_BYTES = 0x14Cu,
    PAD_BYTES = 0x1C0u,
    CAMERA_BYTES = 0x47u,
    HUD_BYTES = 0x24u,
    GADGET_BYTES = 0x80u,
    GADGET_AUX_BYTES = 0x1Cu,

    PAD_INPUT_OFFSET = 0x158u,
    PAD_HELD_OFFSET = 0x158u,
    PAD_PRESSED_OFFSET = 0x160u,
    PAD_RIGHT_STICK_OFFSET = 0x17Au,
    PAD_RIGHT_MAGNITUDE_OFFSET = 0x182u,
    PAD_RIGHT_DIRECTION_OFFSET = 0x19Cu,

    SPIKE_COLLISION_OFFSET = 0x50u,
    SPIKE_EVENTS_OFFSET = 0xE0u,
    SPIKE_PREVIOUS_EVENTS_OFFSET = 0xE4u,
    SPIKE_GADGET_OFFSET = 0xF2u,
    SPIKE_IDLE_TIMER_OFFSET = 0x148u,

    GAMEPLAY_PHASE = 3u,
    NORMAL_PLAY_MODE = 0u,
    CUTSCENE_SUBMODE = 0x0Bu,
    P2_SHARED_BUTTON_MASK = 0x0000000Au,
    SPAWN_SEPARATION = 0x00000100u
};

#define WORD_COUNT(bytes) ((bytes) / 4u)

static uint32_t g_p2_storage;
static uint32_t g_p2_spike;
static uint32_t g_p2_pad;
static uint16_t g_p2_stage;
static uint16_t g_p2_area;
static int g_p2_initialized;
static int g_in_p2_update;

static void read_guest_words(uint32_t address, uint32_t *out, uint32_t bytes) {
    uint32_t offset;
    for (offset = 0u; offset < bytes; offset += 4u)
        out[offset / 4u] = psx_mod_read_word(address + offset);
}

static void write_guest_words(uint32_t address, const uint32_t *in,
                              uint32_t bytes) {
    uint32_t offset;
    for (offset = 0u; offset < bytes; offset += 4u)
        psx_mod_write_word(address + offset, in[offset / 4u]);
}

static void read_guest_bytes(uint32_t address, uint8_t *out, uint32_t bytes) {
    uint32_t offset;
    for (offset = 0u; offset < bytes; ++offset)
        out[offset] = psx_mod_read_byte(address + offset);
}

static void write_guest_bytes(uint32_t address, const uint8_t *in,
                              uint32_t bytes) {
    uint32_t offset;
    for (offset = 0u; offset < bytes; ++offset)
        psx_mod_write_byte(address + offset, in[offset]);
}

static void copy_guest_words(uint32_t destination, uint32_t source,
                             uint32_t bytes) {
    uint32_t offset;
    for (offset = 0u; offset < bytes; offset += 4u)
        psx_mod_write_word(destination + offset,
                           psx_mod_read_word(source + offset));
}

static void reset_host_state(void) {
    g_p2_storage = 0u;
    g_p2_spike = 0u;
    g_p2_pad = 0u;
    g_p2_stage = 0u;
    g_p2_area = 0u;
    g_p2_initialized = 0;
    g_in_p2_update = 0;
}

static void activate_two_spikes(void) {
    reset_host_state();
    g_p2_storage =
        psx_mod_alloc_guest_memory(SPIKE_BYTES + PAD_BYTES, 16u);
    if (g_p2_storage == 0u) {
        psx_mod_counter_add("ape.coop.two-spikes.allocation_failed", 1u);
        return;
    }

    g_p2_spike = g_p2_storage;
    g_p2_pad = g_p2_storage + SPIKE_BYTES;
    psx_mod_counter_add("ape.coop.two-spikes.activated", 1u);
}

static int normal_level_frame(void) {
    return psx_mod_game_started() &&
           psx_mod_read_byte(GAME_PHASE_ADDRESS) == GAMEPLAY_PHASE &&
           psx_mod_read_half(GAME_PLAY_MODE_ADDRESS) == NORMAL_PLAY_MODE &&
           psx_mod_read_half(GAME_PLAY_SUBMODE_ADDRESS) != CUTSCENE_SUBMODE;
}

static void initialize_second_spike(uint16_t stage, uint16_t area) {
    copy_guest_words(g_p2_spike, SPIKE_ADDRESS, SPIKE_BYTES);
    copy_guest_words(g_p2_pad, PAD_ADDRESS, PAD_BYTES);

    /* Keep the second model visibly beside Spike 1. World-space X is used
       rather than changing the camera or introducing a second viewport. */
    psx_mod_write_word(g_p2_spike + 0x24u,
                       psx_mod_read_word(g_p2_spike + 0x24u) +
                           SPAWN_SEPARATION);

    /* P2 uses P1's gadget selection so the game's shared gadget selector stays coherent. */
    psx_mod_write_half(g_p2_spike + SPIKE_IDLE_TIMER_OFFSET, 0u);
    psx_mod_write_byte(g_p2_spike + SPIKE_IDLE_TIMER_OFFSET + 2u, 0u);
    psx_mod_write_byte(g_p2_spike + SPIKE_IDLE_TIMER_OFFSET + 3u, 0u);

    g_p2_stage = stage;
    g_p2_area = area;
    g_p2_initialized = 1;
    psx_mod_counter_add("ape.coop.two-spikes.spawned", 1u);
}

static void prepare_second_pad(const uint32_t *p1_pad) {
    const uint32_t held =
        p1_pad[PAD_HELD_OFFSET / 4u] & P2_SHARED_BUTTON_MASK;
    const uint32_t pressed =
        p1_pad[PAD_PRESSED_OFFSET / 4u] & P2_SHARED_BUTTON_MASK;

    /* Keep P2's own history ring, but supply this frame's processed stick
       state from P1. That reuses Ape's camera-relative movement calculation. */
    write_guest_words(PAD_ADDRESS + PAD_INPUT_OFFSET,
                      p1_pad + (PAD_INPUT_OFFSET / 4u),
                      PAD_BYTES - PAD_INPUT_OFFSET);

    psx_mod_write_word(PAD_ADDRESS + PAD_HELD_OFFSET, held);
    psx_mod_write_word(PAD_ADDRESS + PAD_PRESSED_OFFSET, pressed);

    /* Do not let P1's right-stick gadget aiming leak into the P2 body. */
    psx_mod_write_half(PAD_ADDRESS + PAD_RIGHT_STICK_OFFSET, 0x8080u);
    psx_mod_write_half(PAD_ADDRESS + PAD_RIGHT_MAGNITUDE_OFFSET, 0u);
    psx_mod_write_word(PAD_ADDRESS + PAD_RIGHT_DIRECTION_OFFSET, 0u);
    psx_mod_write_word(PAD_ADDRESS + PAD_RIGHT_DIRECTION_OFFSET + 4u, 0u);
}

static void second_spike_update(struct CPUState *cpu, uint32_t address) {
    uint32_t p1_spike[WORD_COUNT(SPIKE_BYTES)];
    uint32_t p1_pad[WORD_COUNT(PAD_BYTES)];
    uint8_t p1_camera[CAMERA_BYTES];
    uint32_t p1_hud[WORD_COUNT(HUD_BYTES)];
    uint32_t p1_gadget[WORD_COUNT(GADGET_BYTES)];
    uint32_t p1_gadget_aux[WORD_COUNT(GADGET_AUX_BYTES)];
    uint32_t p1_game_mode;
    uint16_t stage;
    uint16_t area;

    (void)address;

    /* The nested native call passes through this entry hook again. */
    if (g_in_p2_update || g_p2_spike == 0u || cpu == 0 ||
        !normal_level_frame())
        return;

    stage = psx_mod_read_half(GAME_STAGE_ADDRESS);
    area = psx_mod_read_half(GAME_AREA_ADDRESS);
    if (!g_p2_initialized || stage != g_p2_stage || area != g_p2_area)
        initialize_second_spike(stage, area);

    if (!g_p2_initialized)
        return;

    /* Preserve the live P1 state and the shared systems that Spike_Update
       touches. The camera stays attached to P1 and the HUD/inventory advance
       only once in the original update that follows this hook. */
    read_guest_words(SPIKE_ADDRESS, p1_spike, SPIKE_BYTES);
    read_guest_words(PAD_ADDRESS, p1_pad, PAD_BYTES);

    /*
     * This is a follower preview, not an independently simulated player.
     * Re-seed P2 from P1 every frame so its animation/action state cannot
     * drift away while both bodies share the game's single gadget selector,
     * event system, and collision owner. The second body keeps only its own
     * lateral offset between the two native update calls.
     */
    copy_guest_words(g_p2_spike, SPIKE_ADDRESS, SPIKE_BYTES);
    psx_mod_write_word(g_p2_spike + 0x24u,
                       p1_spike[0x24u / 4u] + SPAWN_SEPARATION);
    read_guest_bytes(CAMERA_ADDRESS, p1_camera, CAMERA_BYTES);
    read_guest_words(HUD_ADDRESS, p1_hud, HUD_BYTES);
    read_guest_words(GADGET_ADDRESS, p1_gadget, GADGET_BYTES);
    read_guest_words(GADGET_AUX_ADDRESS, p1_gadget_aux, GADGET_AUX_BYTES);
    p1_game_mode = psx_mod_read_word(GAME_PLAY_MODE_ADDRESS);

    /* Project P2 into the game's single-player globals. */
    copy_guest_words(SPIKE_ADDRESS, g_p2_spike, SPIKE_BYTES);
    copy_guest_words(PAD_ADDRESS, g_p2_pad, PAD_BYTES);
    prepare_second_pad(p1_pad);

    /* Clear single-player-only events so P2 cannot switch the shared level
       state, trigger exits, or launch a CD-backed idle animation. */
    psx_mod_write_word(SPIKE_ADDRESS + SPIKE_COLLISION_OFFSET, 0u);
    psx_mod_write_word(SPIKE_ADDRESS + SPIKE_EVENTS_OFFSET, 0u);
    psx_mod_write_word(SPIKE_ADDRESS + SPIKE_PREVIOUS_EVENTS_OFFSET, 0u);
    psx_mod_write_half(SPIKE_ADDRESS + SPIKE_IDLE_TIMER_OFFSET, 0u);
    psx_mod_write_byte(SPIKE_ADDRESS + SPIKE_IDLE_TIMER_OFFSET + 2u, 0u);
    psx_mod_write_byte(SPIKE_ADDRESS + SPIKE_IDLE_TIMER_OFFSET + 3u, 0u);

    g_in_p2_update = 1;
    (void)psx_mod_call_guest(cpu, SPIKE_UPDATE_ADDRESS, cpu->gpr[31],
                             0u, 0u, 0u, 0u);
    g_in_p2_update = 0;

    /* Save P2's independent player bytes, then restore P1 and all shared
       systems before the original Spike_Update body runs. */
    copy_guest_words(g_p2_spike, SPIKE_ADDRESS, SPIKE_BYTES);
    copy_guest_words(g_p2_pad, PAD_ADDRESS, PAD_BYTES);

    write_guest_words(SPIKE_ADDRESS, p1_spike, SPIKE_BYTES);
    write_guest_words(PAD_ADDRESS, p1_pad, PAD_BYTES);
    write_guest_bytes(CAMERA_ADDRESS, p1_camera, CAMERA_BYTES);
    write_guest_words(HUD_ADDRESS, p1_hud, HUD_BYTES);
    write_guest_words(GADGET_ADDRESS, p1_gadget, GADGET_BYTES);
    write_guest_words(GADGET_AUX_ADDRESS, p1_gadget_aux, GADGET_AUX_BYTES);
    psx_mod_write_word(GAME_PLAY_MODE_ADDRESS, p1_game_mode);

    psx_mod_counter_add("ape.coop.two-spikes.p2_updates", 1u);
}

static int suppress_second_spike_collision(struct CPUState *cpu,
                                           uint32_t address) {
    (void)address;
    if (!g_in_p2_update || cpu == 0)
        return 0;

    /* Collision_Set returns an integer in v0. P2's character-level collision
       callbacks are deferred until each player has a distinct collision owner. */
    cpu->gpr[2] = 0u;
    psx_mod_counter_add("ape.coop.two-spikes.p2_collisions_suppressed", 1u);
    return 1;
}

PSX_MOD_CONSTRUCTOR(register_two_spike_prototype) {
    (void)psx_mod_register_session_reset_plugin(
        "ape.coop.two-spikes", reset_host_state);
    (void)psx_mod_register_activation_plugin(
        "ape.coop.two-spikes", activate_two_spikes);
    (void)psx_mod_register_function_entry_plugin(
        "ape.coop.two-spikes", SPIKE_UPDATE_ADDRESS, second_spike_update);
    (void)psx_mod_register_function_filter_plugin(
        "ape.coop.two-spikes-collision-filter", COLLISION_SET_ADDRESS,
        suppress_second_spike_collision);
}
