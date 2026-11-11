#include "cpu_state.h"
#include "mod_netplay.h"
#include "mod_plugins.h"
#include "sio.h"

#include <SDL3/SDL.h>
#include <stdint.h>

/*
 * Two-Spike local co-op and netplay simulation.
 *
 * P2 has its own saved Spike, Pad, and gadget state in Expansion 1. Its input
 * is sampled from logical SIO pad 1; in online play the framework supplies
 * the resolved second seat through that same SIO slot. P1 remains the camera
 * target and owns the shared HUD.
 *
 * Optional offline keyboard maps let both local players share one keyboard.
 * Netplay receives only the synchronized controller-slot samples.
 */

#define SPIKE_UPDATE_ADDRESS UINT32_C(0x80060780)
#define COLLISION_SET_ADDRESS UINT32_C(0x80071E44)

#define SPIKE_ADDRESS UINT32_C(0x800EC1E0)
#define PAD_ADDRESS UINT32_C(0x800E5320)
#define CAMERA_ADDRESS UINT32_C(0x800E68D0)
#define VIEW_MATRIX_ADDRESS UINT32_C(0x800E78C0)
#define CAMERA_YAW_MATRIX_ADDRESS UINT32_C(0x800EBFD0)
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
    MATRIX_BYTES = 0x20u,
    HUD_BYTES = 0x24u,
    GADGET_BYTES = 0x80u,
    GADGET_AUX_BYTES = 0x1Cu,

    PAD_HELD_OFFSET = 0x158u,
    PAD_PRESSED_OFFSET = 0x160u,
    PAD_RAW_STICK_X_OFFSET = 0x0A2u,
    PAD_RAW_STICK_Y_OFFSET = 0x0A3u,
    PAD_HISTORY_OFFSET = 0x0A4u,
    PAD_HISTORY_INDEX_OFFSET = 0x170u,
    PAD_HISTORY_COUNT = 40u,
    PAD_LEFT_STICK_OFFSET = 0x178u,
    PAD_RIGHT_STICK_OFFSET = 0x17Au,
    PAD_MAGL_OFFSET = 0x180u,
    PAD_MAGR_OFFSET = 0x182u,
    PAD_DIRL_OFFSET = 0x194u,
    PAD_DIRR_OFFSET = 0x19Cu,

    SPIKE_IDLE_TIMER_OFFSET = 0x148u,

    GAMEPLAY_PHASE = 3u,
    NORMAL_PLAY_MODE = 0u,
    CUTSCENE_SUBMODE = 0x0Bu,
    STICK_DEADZONE = 21u,
    STICK_MAX_MAGNITUDE = 127u,
    SPAWN_SEPARATION = 0x00000100u
};

#define WORD_COUNT(bytes) ((bytes) / 4u)
#define P2_STORAGE_BYTES (SPIKE_BYTES + PAD_BYTES + GADGET_BYTES + GADGET_AUX_BYTES)

static uint32_t g_p2_storage;
static uint32_t g_p2_spike;
static uint32_t g_p2_pad;
static uint32_t g_p2_gadget;
static uint32_t g_p2_gadget_aux;
static uint16_t g_p2_stage;
static uint16_t g_p2_area;
static int g_p2_initialized;
static int g_in_p2_update;
static int g_allow_keyboard_fallback;
static int g_p1_keyboard_enabled;
static int g_p2_keyboard_enabled;

/* The netplay profile is delay-sync only until rollback and state restore have
 * been verified with the game's separate player/gadget contexts. */
static const PSXModNetplayProfile g_two_spike_netplay_profile = {
    .plugin_id = "ape.coop.two-spikes",
    .compatibility_id = "apeescape-two-spikes-coop-v1",
    .rollback_supported = 0,
    .savestates_supported = 0,
    .skip_bios_intro = 1,
    .fixed_aspect_mask = 0u,
    .widescreen_plugin_id = 0,
    .state_digest = 0,
    .requires_retained_texture_banks = 0,
    .settings_package_id = 0,
    .settings_feature_id = 0,
    .renderer_option_id = 0,
    .renderer_option_value = 0,
    .loading_package_id = 0,
    .loading_feature_id = 0,
    .loading_plugin_id = 0
};

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
    g_p2_gadget = 0u;
    g_p2_gadget_aux = 0u;
    g_p2_stage = 0u;
    g_p2_area = 0u;
    g_p2_initialized = 0;
    g_in_p2_update = 0;
    g_allow_keyboard_fallback = 0;
    g_p1_keyboard_enabled = 0;
    g_p2_keyboard_enabled = 0;
}

static int read_keyboard_option(const char *option_id) {
    char value[8] = {0};
    if (!psx_mod_option_value("ape.enhancement.two-spikes-prototype",
                              "two-spike-preview", option_id, value,
                              sizeof(value))) {
        return 0;
    }
    return value[0] == 't' || value[0] == '1';
}

static void activate_two_spikes(void) {
    reset_host_state();
    g_p1_keyboard_enabled = read_keyboard_option("p1-keyboard");
    g_p2_keyboard_enabled = read_keyboard_option("p2-keyboard");
    g_p2_storage = psx_mod_alloc_guest_memory(P2_STORAGE_BYTES, 16u);
    if (g_p2_storage == 0u) {
        psx_mod_counter_add("ape.coop.two-spikes.allocation_failed", 1u);
        return;
    }

    g_p2_spike = g_p2_storage;
    g_p2_pad = g_p2_spike + SPIKE_BYTES;
    g_p2_gadget = g_p2_pad + PAD_BYTES;
    g_p2_gadget_aux = g_p2_gadget + GADGET_BYTES;
    psx_mod_counter_add("ape.coop.two-spikes.activated", 1u);
}

static int normal_level_frame(void) {
    return psx_mod_game_started() &&
           psx_mod_read_byte(GAME_PHASE_ADDRESS) == GAMEPLAY_PHASE &&
           psx_mod_read_half(GAME_PLAY_MODE_ADDRESS) == NORMAL_PLAY_MODE &&
           psx_mod_read_half(GAME_PLAY_SUBMODE_ADDRESS) != CUTSCENE_SUBMODE;
}

static void clear_second_pad_input(void) {
    uint32_t i;

    psx_mod_write_word(g_p2_pad + PAD_HELD_OFFSET, 0u);
    psx_mod_write_word(g_p2_pad + PAD_PRESSED_OFFSET, 0u);
    psx_mod_write_byte(g_p2_pad + PAD_RAW_STICK_X_OFFSET, 0u);
    psx_mod_write_byte(g_p2_pad + PAD_RAW_STICK_Y_OFFSET, 0u);
    psx_mod_write_half(g_p2_pad + PAD_LEFT_STICK_OFFSET, 0u);
    psx_mod_write_half(g_p2_pad + PAD_RIGHT_STICK_OFFSET, 0u);
    psx_mod_write_half(g_p2_pad + PAD_MAGL_OFFSET, 0u);
    psx_mod_write_half(g_p2_pad + PAD_MAGR_OFFSET, 0u);
    psx_mod_write_word(g_p2_pad + PAD_DIRL_OFFSET, 0u);
    psx_mod_write_word(g_p2_pad + PAD_DIRL_OFFSET + 4u, 0u);
    psx_mod_write_word(g_p2_pad + PAD_DIRR_OFFSET, 0u);
    psx_mod_write_word(g_p2_pad + PAD_DIRR_OFFSET + 4u, 0u);
    psx_mod_write_word(g_p2_pad + PAD_HISTORY_INDEX_OFFSET, 0u);

    for (i = 0u; i < PAD_HISTORY_COUNT; ++i)
        psx_mod_write_half(g_p2_pad + PAD_HISTORY_OFFSET + i * 2u, 0u);
}

static void initialize_second_spike(uint16_t stage, uint16_t area) {
    copy_guest_words(g_p2_spike, SPIKE_ADDRESS, SPIKE_BYTES);
    copy_guest_words(g_p2_pad, PAD_ADDRESS, PAD_BYTES);
    copy_guest_words(g_p2_gadget, GADGET_ADDRESS, GADGET_BYTES);
    copy_guest_words(g_p2_gadget_aux, GADGET_AUX_ADDRESS, GADGET_AUX_BYTES);

    /* Start P2 beside P1 once; do not overwrite its position/action each frame. */
    psx_mod_write_word(g_p2_spike + 0x24u,
                       psx_mod_read_word(g_p2_spike + 0x24u) +
                           SPAWN_SEPARATION);
    clear_second_pad_input();

    /* The CD-backed idle-only animation loader is still shared by the game.
       Keep that one path off until it can have its own resource context. */
    psx_mod_write_half(g_p2_spike + SPIKE_IDLE_TIMER_OFFSET, 0u);
    psx_mod_write_byte(g_p2_spike + SPIKE_IDLE_TIMER_OFFSET + 2u, 0u);
    psx_mod_write_byte(g_p2_spike + SPIKE_IDLE_TIMER_OFFSET + 3u, 0u);

    g_p2_stage = stage;
    g_p2_area = area;
    g_p2_initialized = 1;
    psx_mod_counter_add("ape.coop.two-spikes.spawned", 1u);
}

static uint32_t integer_sqrt(uint32_t value) {
    uint32_t result = 0u;
    uint32_t bit = UINT32_C(1) << 30;

    while (bit > value)
        bit >>= 2;

    while (bit != 0u) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static uint16_t pack_stick_axes(int8_t x, int8_t y) {
    return (uint16_t)(uint8_t)x |
           (uint16_t)((uint16_t)(uint8_t)y << 8);
}

static uint16_t write_stick_direction(int8_t stick_x, int8_t stick_y,
                                      uint32_t direction_address) {
    int32_t x = stick_x;
    int32_t y = -(int32_t)stick_y;
    uint32_t magnitude = integer_sqrt((uint32_t)(x * x + y * y));
    int32_t m00;
    int32_t m02;
    int32_t m20;
    int32_t m22;
    int32_t world_x;
    int32_t world_z;
    uint32_t world_magnitude;
    int16_t direction_x;
    int16_t direction_z;

    if (magnitude <= STICK_DEADZONE) {
        psx_mod_write_half(direction_address, 0u);
        psx_mod_write_half(direction_address + 2u, 0u);
        psx_mod_write_half(direction_address + 4u, 0u);
        psx_mod_write_half(direction_address + 6u, 0u);
        return 0u;
    }

    if (magnitude > STICK_MAX_MAGNITUDE)
        magnitude = STICK_MAX_MAGNITUDE;

    /* g_ViewMatrix is the live camera transform. Its transpose maps the
       player's camera-relative stick vector into the shared world plane. */
    m00 = (int16_t)psx_mod_read_half(VIEW_MATRIX_ADDRESS);
    m02 = (int16_t)psx_mod_read_half(VIEW_MATRIX_ADDRESS + 4u);
    m20 = (int16_t)psx_mod_read_half(VIEW_MATRIX_ADDRESS + 12u);
    m22 = (int16_t)psx_mod_read_half(VIEW_MATRIX_ADDRESS + 16u);

    world_x = (m00 * x + m20 * y) >> 12;
    world_z = (m02 * x + m22 * y) >> 12;
    world_magnitude =
        integer_sqrt((uint32_t)(world_x * world_x + world_z * world_z));

    if (world_magnitude == 0u) {
        world_x = x;
        world_z = y;
        world_magnitude = magnitude;
    }

    direction_x = (int16_t)((world_x * 4096) / (int32_t)world_magnitude);
    direction_z = (int16_t)((world_z * 4096) / (int32_t)world_magnitude);

    psx_mod_write_half(direction_address, (uint16_t)direction_x);
    psx_mod_write_half(direction_address + 2u, 0u);
    psx_mod_write_half(direction_address + 4u, (uint16_t)direction_z);
    psx_mod_write_half(direction_address + 6u, 0u);

    return (uint16_t)(((magnitude - STICK_DEADZONE) * STICK_MAX_MAGNITUDE) /
                      (STICK_MAX_MAGNITUDE - STICK_DEADZONE));
}

static void update_pad_history(uint32_t pad_address,
                                uint32_t newly_pressed) {
    uint32_t history_index =
        psx_mod_read_word(pad_address + PAD_HISTORY_INDEX_OFFSET);
    uint32_t button;

    /* Ape stores 40 u16 button IDs in its recent-input ring. */
    for (button = 1u; button <= UINT32_C(0x8000); button <<= 1) {
        if ((newly_pressed & button) != 0u) {
            uint32_t slot = history_index % PAD_HISTORY_COUNT;
            psx_mod_write_half(pad_address + PAD_HISTORY_OFFSET + slot * 2u,
                               (uint16_t)button);
            ++history_index;
        }
    }
    psx_mod_write_word(pad_address + PAD_HISTORY_INDEX_OFFSET, history_index);
}

static int keyboard_key_down(const bool *keys, int key_count,
                               SDL_Scancode scancode) {
    int index = (int)scancode;
    return keys != 0 && index >= 0 && index < key_count && keys[index];
}

static int8_t keyboard_axis(const bool *keys, int key_count,
                            SDL_Scancode negative, SDL_Scancode positive) {
    int value = 0;
    if (keyboard_key_down(keys, key_count, negative))
        --value;
    if (keyboard_key_down(keys, key_count, positive))
        ++value;
    return (int8_t)(value * 127);
}

static uint16_t keyboard_buttons(const bool *keys, int key_count) {
    uint16_t buttons = 0u;

    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_RCTRL))
        buttons |= 0x0040u; /* Cross */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_RSHIFT))
        buttons |= 0x0020u; /* Circle */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_RALT))
        buttons |= 0x0080u; /* Square */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_RETURN))
        buttons |= 0x0010u; /* Triangle */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_U))
        buttons |= 0x0004u; /* L1 */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_O))
        buttons |= 0x0008u; /* R1 */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_Y))
        buttons |= 0x0001u; /* L2 */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_P))
        buttons |= 0x0002u; /* R2 */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_INSERT))
        buttons |= 0x0100u; /* Select */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_BACKSPACE))
        buttons |= 0x0800u; /* Start */

    return buttons;
}

static void prepare_second_pad(void) {
    uint8_t axes[4] = {0x80u, 0x80u, 0x80u, 0x80u};
    int key_count = 0;
    const bool *keys = 0;
    if (g_allow_keyboard_fallback && g_p2_keyboard_enabled)
        keys = SDL_GetKeyboardState(&key_count);
    uint16_t active_sio_buttons =
        (uint16_t)~sio_get_pad_buttons_slot(1);
    uint16_t game_buttons =
        (uint16_t)(((active_sio_buttons & 0x00FFu) << 8) |
                   ((active_sio_buttons & 0xFF00u) >> 8));
    uint32_t previous_held =
        psx_mod_read_word(PAD_ADDRESS + PAD_HELD_OFFSET);
    uint32_t newly_pressed;
    int8_t left_x;
    int8_t left_y;
    int8_t right_x;
    int8_t right_y;
    int8_t key_left_x;
    int8_t key_left_y;
    int8_t key_right_x;
    int8_t key_right_y;
    uint16_t left_magnitude;
    uint16_t right_magnitude;

    sio_get_pad_sticks(1, axes);
    left_x = (int8_t)((int)axes[0] - 128);
    left_y = (int8_t)((int)axes[1] - 128);
    right_x = (int8_t)((int)axes[2] - 128);
    right_y = (int8_t)((int)axes[3] - 128);

    /* P2 keyboard defaults: arrows move, I/J/K/L aim, right-side modifiers
       are the face buttons, and U/O/Y/P are the four shoulder buttons. */
    key_left_x =
        keyboard_axis(keys, key_count, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
    key_left_y =
        keyboard_axis(keys, key_count, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN);
    key_right_x =
        keyboard_axis(keys, key_count, SDL_SCANCODE_J, SDL_SCANCODE_L);
    key_right_y =
        keyboard_axis(keys, key_count, SDL_SCANCODE_I, SDL_SCANCODE_K);
    if (key_left_x != 0)
        left_x = key_left_x;
    if (key_left_y != 0)
        left_y = key_left_y;
    if (key_right_x != 0)
        right_x = key_right_x;
    if (key_right_y != 0)
        right_y = key_right_y;
    game_buttons |= keyboard_buttons(keys, key_count);
    newly_pressed = (uint32_t)game_buttons & ~previous_held;

    psx_mod_write_word(PAD_ADDRESS + PAD_HELD_OFFSET, game_buttons);
    psx_mod_write_word(PAD_ADDRESS + PAD_PRESSED_OFFSET, newly_pressed);
    psx_mod_write_byte(PAD_ADDRESS + PAD_RAW_STICK_X_OFFSET,
                       (uint8_t)left_x);
    psx_mod_write_byte(PAD_ADDRESS + PAD_RAW_STICK_Y_OFFSET,
                       (uint8_t)left_y);
    psx_mod_write_half(PAD_ADDRESS + PAD_LEFT_STICK_OFFSET,
                       pack_stick_axes(left_x, left_y));
    psx_mod_write_half(PAD_ADDRESS + PAD_RIGHT_STICK_OFFSET,
                       pack_stick_axes(right_x, right_y));

    left_magnitude =
        write_stick_direction(left_x, left_y, PAD_ADDRESS + PAD_DIRL_OFFSET);
    right_magnitude =
        write_stick_direction(right_x, right_y, PAD_ADDRESS + PAD_DIRR_OFFSET);
    psx_mod_write_half(PAD_ADDRESS + PAD_MAGL_OFFSET, left_magnitude);
    psx_mod_write_half(PAD_ADDRESS + PAD_MAGR_OFFSET, right_magnitude);

    update_pad_history(PAD_ADDRESS, newly_pressed);
}

static uint16_t first_player_keyboard_buttons(const bool *keys,
                                                  int key_count) {
    uint16_t buttons = 0u;

    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_Z))
        buttons |= 0x0040u; /* Cross */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_X))
        buttons |= 0x0020u; /* Circle */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_C))
        buttons |= 0x0080u; /* Square */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_V))
        buttons |= 0x0010u; /* Triangle */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_Q))
        buttons |= 0x0004u; /* L1 */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_E))
        buttons |= 0x0008u; /* R1 */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_R))
        buttons |= 0x0001u; /* L2 */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_B))
        buttons |= 0x0002u; /* R2 */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_TAB))
        buttons |= 0x0100u; /* Select */
    if (keyboard_key_down(keys, key_count, SDL_SCANCODE_SPACE))
        buttons |= 0x0800u; /* Start */

    return buttons;
}

static void apply_first_player_keyboard(void) {
    int key_count = 0;
    const bool *keys = SDL_GetKeyboardState(&key_count);
    uint32_t held =
        psx_mod_read_word(PAD_ADDRESS + PAD_HELD_OFFSET);
    uint32_t newly_pressed;
    uint16_t keyboard_held = first_player_keyboard_buttons(keys, key_count);
    int8_t left_x =
        (int8_t)psx_mod_read_byte(PAD_ADDRESS + PAD_RAW_STICK_X_OFFSET);
    int8_t left_y =
        (int8_t)psx_mod_read_byte(PAD_ADDRESS + PAD_RAW_STICK_Y_OFFSET);
    int8_t right_x =
        (int8_t)psx_mod_read_byte(PAD_ADDRESS + PAD_RIGHT_STICK_OFFSET);
    int8_t right_y =
        (int8_t)psx_mod_read_byte(PAD_ADDRESS + PAD_RIGHT_STICK_OFFSET + 1u);
    int8_t key_left_x =
        keyboard_axis(keys, key_count, SDL_SCANCODE_A, SDL_SCANCODE_D);
    int8_t key_left_y =
        keyboard_axis(keys, key_count, SDL_SCANCODE_W, SDL_SCANCODE_S);
    int8_t key_right_x =
        keyboard_axis(keys, key_count, SDL_SCANCODE_F, SDL_SCANCODE_H);
    int8_t key_right_y =
        keyboard_axis(keys, key_count, SDL_SCANCODE_T, SDL_SCANCODE_G);
    uint32_t pressed;
    uint16_t magnitude;

    if (key_left_x != 0 || key_left_y != 0) {
        if (key_left_x != 0)
            left_x = key_left_x;
        if (key_left_y != 0)
            left_y = key_left_y;
        psx_mod_write_byte(PAD_ADDRESS + PAD_RAW_STICK_X_OFFSET,
                           (uint8_t)left_x);
        psx_mod_write_byte(PAD_ADDRESS + PAD_RAW_STICK_Y_OFFSET,
                           (uint8_t)left_y);
        psx_mod_write_half(PAD_ADDRESS + PAD_LEFT_STICK_OFFSET,
                           pack_stick_axes(left_x, left_y));
        magnitude = write_stick_direction(
            left_x, left_y, PAD_ADDRESS + PAD_DIRL_OFFSET);
        psx_mod_write_half(PAD_ADDRESS + PAD_MAGL_OFFSET, magnitude);
    }

    if (key_right_x != 0 || key_right_y != 0) {
        if (key_right_x != 0)
            right_x = key_right_x;
        if (key_right_y != 0)
            right_y = key_right_y;
        psx_mod_write_half(PAD_ADDRESS + PAD_RIGHT_STICK_OFFSET,
                           pack_stick_axes(right_x, right_y));
        magnitude = write_stick_direction(
            right_x, right_y, PAD_ADDRESS + PAD_DIRR_OFFSET);
        psx_mod_write_half(PAD_ADDRESS + PAD_MAGR_OFFSET, magnitude);
    }

    newly_pressed = (uint32_t)keyboard_held & ~held;
    pressed = psx_mod_read_word(PAD_ADDRESS + PAD_PRESSED_OFFSET);
    psx_mod_write_word(PAD_ADDRESS + PAD_HELD_OFFSET,
                       held | (uint32_t)keyboard_held);
    psx_mod_write_word(PAD_ADDRESS + PAD_PRESSED_OFFSET,
                       pressed | newly_pressed);
    update_pad_history(PAD_ADDRESS, newly_pressed);
}

static void second_spike_update_impl(struct CPUState *cpu, uint32_t address) {
    uint32_t p1_spike[WORD_COUNT(SPIKE_BYTES)];
    uint32_t p1_pad[WORD_COUNT(PAD_BYTES)];
    uint8_t p1_camera[CAMERA_BYTES];
    uint8_t p1_view_matrix[MATRIX_BYTES];
    uint8_t p1_camera_yaw_matrix[MATRIX_BYTES];
    uint32_t p1_hud[WORD_COUNT(HUD_BYTES)];
    uint32_t p1_gadget[WORD_COUNT(GADGET_BYTES)];
    uint32_t p1_gadget_aux[WORD_COUNT(GADGET_AUX_BYTES)];
    uint32_t p1_game_mode;
    uint16_t stage;
    uint16_t area;

    (void)address;

    /* The nested native call passes through this entry hook again. */
    if (g_in_p2_update || cpu == 0 || !normal_level_frame())
        return;

    /* Netplay clears the offline mod plan, so allocate lazily for the trusted
       game-owned netplay hook as well as through the offline activation hook. */
    if (g_p2_storage == 0u)
        activate_two_spikes();
    if (g_p2_spike == 0u)
        return;

    stage = psx_mod_read_half(GAME_STAGE_ADDRESS);
    area = psx_mod_read_half(GAME_AREA_ADDRESS);
    if (!g_p2_initialized || stage != g_p2_stage || area != g_p2_area)
        initialize_second_spike(stage, area);

    if (!g_p2_initialized)
        return;

    read_guest_words(SPIKE_ADDRESS, p1_spike, SPIKE_BYTES);
    read_guest_words(PAD_ADDRESS, p1_pad, PAD_BYTES);
    read_guest_bytes(CAMERA_ADDRESS, p1_camera, CAMERA_BYTES);
    read_guest_bytes(VIEW_MATRIX_ADDRESS, p1_view_matrix, MATRIX_BYTES);
    read_guest_bytes(CAMERA_YAW_MATRIX_ADDRESS, p1_camera_yaw_matrix,
                     MATRIX_BYTES);
    read_guest_words(HUD_ADDRESS, p1_hud, HUD_BYTES);
    read_guest_words(GADGET_ADDRESS, p1_gadget, GADGET_BYTES);
    read_guest_words(GADGET_AUX_ADDRESS, p1_gadget_aux, GADGET_AUX_BYTES);
    p1_game_mode = psx_mod_read_word(GAME_PLAY_MODE_ADDRESS);

    /* Project P2's persistent player, pad, and gadget contexts into the
       game's single-player globals for one native update. */
    copy_guest_words(SPIKE_ADDRESS, g_p2_spike, SPIKE_BYTES);
    copy_guest_words(PAD_ADDRESS, g_p2_pad, PAD_BYTES);
    copy_guest_words(GADGET_ADDRESS, g_p2_gadget, GADGET_BYTES);
    copy_guest_words(GADGET_AUX_ADDRESS, g_p2_gadget_aux, GADGET_AUX_BYTES);
    prepare_second_pad();

    /* P2 shares the camera target and HUD, but not their mutable camera
       matrices, gadget state, or animation-event history. */
    g_in_p2_update = 1;
    (void)psx_mod_call_guest(cpu, SPIKE_UPDATE_ADDRESS, cpu->gpr[31],
                             0u, 0u, 0u, 0u);
    g_in_p2_update = 0;

    /* Persist the independent P2 state, including gadget animation/effects. */
    copy_guest_words(g_p2_spike, SPIKE_ADDRESS, SPIKE_BYTES);
    copy_guest_words(g_p2_pad, PAD_ADDRESS, PAD_BYTES);
    copy_guest_words(g_p2_gadget, GADGET_ADDRESS, GADGET_BYTES);
    copy_guest_words(g_p2_gadget_aux, GADGET_AUX_ADDRESS, GADGET_AUX_BYTES);

    write_guest_words(SPIKE_ADDRESS, p1_spike, SPIKE_BYTES);
    write_guest_words(PAD_ADDRESS, p1_pad, PAD_BYTES);
    write_guest_bytes(CAMERA_ADDRESS, p1_camera, CAMERA_BYTES);
    write_guest_bytes(VIEW_MATRIX_ADDRESS, p1_view_matrix, MATRIX_BYTES);
    write_guest_bytes(CAMERA_YAW_MATRIX_ADDRESS, p1_camera_yaw_matrix,
                      MATRIX_BYTES);
    write_guest_words(HUD_ADDRESS, p1_hud, HUD_BYTES);
    write_guest_words(GADGET_ADDRESS, p1_gadget, GADGET_BYTES);
    write_guest_words(GADGET_AUX_ADDRESS, p1_gadget_aux, GADGET_AUX_BYTES);
    psx_mod_write_word(GAME_PLAY_MODE_ADDRESS, p1_game_mode);

    psx_mod_counter_add("ape.coop.two-spikes.p2_updates", 1u);
}

static void second_spike_update_offline(struct CPUState *cpu,
                                           uint32_t address) {
    g_allow_keyboard_fallback = 1;
    second_spike_update_impl(cpu, address);
    if (cpu != 0 && normal_level_frame() && g_p1_keyboard_enabled)
        apply_first_player_keyboard();
}

static void second_spike_update_netplay(struct CPUState *cpu,
                                        uint32_t address) {
    /* Online input must arrive through the synchronized Player 2 SIO slot;
       do not sample a local SDL keyboard here after the netplay tick latches. */
    g_allow_keyboard_fallback = 0;
    second_spike_update_impl(cpu, address);
}

static void route_second_spike_collision(struct CPUState *cpu,
                                           uint32_t address) {
    (void)address;
    if (!g_in_p2_update || cpu == 0 || g_p2_spike == 0u)
        return;

    /* The owner may be either &g_Spike or a field within its structure. */
    if (cpu->gpr[6] >= SPIKE_ADDRESS &&
        cpu->gpr[6] < SPIKE_ADDRESS + SPIKE_BYTES) {
        cpu->gpr[6] = g_p2_spike + (cpu->gpr[6] - SPIKE_ADDRESS);
        psx_mod_counter_add("ape.coop.two-spikes.p2_collisions_routed", 1u);
    }
}

PSX_MOD_CONSTRUCTOR(register_two_spike_prototype) {
    (void)psx_mod_register_session_reset_plugin(
        "ape.coop.two-spikes", reset_host_state);
    (void)psx_mod_register_activation_plugin(
        "ape.coop.two-spikes", activate_two_spikes);
    (void)psx_mod_register_function_entry_plugin(
        "ape.coop.two-spikes", SPIKE_UPDATE_ADDRESS, second_spike_update_offline);
    (void)psx_mod_register_function_entry_plugin(
        "ape.coop.two-spikes-collision-owner", COLLISION_SET_ADDRESS,
        route_second_spike_collision);

    /* Online matches clear package mods. Register the same deterministic
       simulation hook as trusted game code and advertise the shared profile. */
    (void)psx_mod_register_netplay_profile(&g_two_spike_netplay_profile);
    (void)psx_game_register_netplay_function_entry(
        SPIKE_UPDATE_ADDRESS, second_spike_update_netplay);
    (void)psx_game_register_netplay_function_entry(
        COLLISION_SET_ADDRESS, route_second_spike_collision);
}
