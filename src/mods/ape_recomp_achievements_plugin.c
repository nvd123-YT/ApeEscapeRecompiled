#include "mod_plugins.h"
#include "ape_recomp_achievements.h"

#if !defined(__EMSCRIPTEN__)
#include "memcard.h"
#include "rl_achievements.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define APE_PHASE_ADDRESS             0x800F447Cu
#define APE_PHASE_PLAYING             3u
#define APE_MONKEY_FLAGS_ADDRESS      0x80139368u
#define APE_MONKEY_FLAG_STRIDE        8u
#define APE_MONKEYS_PER_STAGE         20u
#define APE_GADGET_MASK_ADDRESS       0x800F51C4u
#define APE_STAGE_TIMER_ADDRESS       0x800F4490u

/* Value 2 is the game's completed/100% state; value 1 only unlocks a stage. */
typedef struct ApeStageAchievement {
    uint32_t address;
    const char *event;
} ApeStageAchievement;

static const ApeStageAchievement APE_STAGES[] = {
    {0x800DFC71u, "complete_fossil_field"},
    {0x800DFC72u, "complete_primordial_ooze"},
    {0x800DFC73u, "complete_molten_lava"},
    {0x800DFC74u, "complete_thick_jungle"},
    {0x800DFC75u, "complete_dark_ruins"},
    {0x800DFC76u, "complete_cryptic_relics"},
    {0x800DFC77u, "complete_stadium_attack"},
    {0x800DFC78u, "complete_crabby_beach"},
    {0x800DFC79u, "complete_coral_cave"},
    {0x800DFC7Au, "complete_dexters_island"},
    {0x800DFC7Bu, "complete_snowy_mammoth"},
    {0x800DFC7Cu, "complete_frosty_retreat"},
    {0x800DFC7Du, "complete_hot_springs"},
    {0x800DFC7Eu, "complete_gladiator_attack"},
    {0x800DFC7Fu, "complete_sushi_temple"},
    {0x800DFC80u, "complete_wabi_sabi_wall"},
    {0x800DFC81u, "complete_crumbling_castle"},
    {0x800DFC84u, "complete_city_park"},
    {0x800DFC85u, "complete_specters_factory"},
    {0x800DFC86u, "complete_tv_tower"},
    {0x800DFC88u, "complete_monkey_madness"},
    {0x800DFC8Eu, "complete_peak_point_matrix"}
};

typedef struct ApeGadgetAchievement {
    uint8_t mask;
    const char *event;
} ApeGadgetAchievement;

static const ApeGadgetAchievement APE_GADGETS[] = {
    {0x01u, "unlock_stun_club"},
    {0x02u, "unlock_time_net"},
    {0x04u, "unlock_monkey_radar"},
    {0x08u, "unlock_slingshot"},
    {0x10u, "unlock_super_hoop"},
    {0x20u, "unlock_magic_punch"},
    {0x40u, "unlock_sky_flyer"},
    {0x80u, "unlock_rc_car"}
};

/*
 * Community save-format notes for SCUS-94423 identify a 7F 4B marker at
 * card offset 0x2196, the gadget mask at 0x2184, and per-stage monkey totals
 * beginning at 0x21C9. A PS1 card data block is 0x2000 bytes; block 1 begins
 * at 0x2000, so the offsets below are relative to a candidate data block.
 * We accept only a block with the marker and all 19 known counts in range.
 */
#define APE_CARD_BLOCK_SECTORS  64u
#define APE_CARD_DATA_SECTOR    3u
#define APE_CARD_MARKER_OFFSET  0x16u
#define APE_CARD_GADGET_OFFSET  0x04u
#define APE_CARD_MONKEY_OFFSET  0x49u

static const uint8_t APE_SAVE_STAGE_INDEX[19] = {
    0u, 1u, 2u, 3u, 4u, 5u, 7u, 8u, 9u, 10u,
    11u, 12u, 14u, 15u, 16u, 19u, 20u, 21u, 23u
};

static const uint8_t APE_SAVE_STAGE_MAX[19] = {
    4u, 6u, 7u, 14u, 13u, 8u, 8u, 8u, 11u, 6u,
    9u, 9u, 12u, 10u, 20u, 13u, 10u, 12u, 24u
};

static uint8_t g_initialized;
static uint8_t g_pending_casual;
static uint8_t g_casual_sent;
static char g_pending_casual_reason[32];
static uint8_t g_stage_sent[sizeof(APE_STAGES) / sizeof(APE_STAGES[0])];
static uint8_t g_gadget_sent[sizeof(APE_GADGETS) / sizeof(APE_GADGETS[0])];
static uint8_t g_monkey_one_sent;
static uint8_t g_monkey_ten_sent;
static uint8_t g_monkey_hundred_sent;
static uint8_t g_monkey_all_sent;
static uint8_t g_monkey_flags_valid;
static uint8_t g_previous_monkey_flags[APE_MONKEYS_PER_STAGE];
static uint8_t g_previous_timer_valid;
static uint16_t g_previous_timer;
static uint32_t g_session_new_catches;
static uint32_t g_saved_monkeys;
static uint32_t g_saved_monkeys_at_start;
static uint32_t g_last_saved_monkeys;
static uint32_t g_save_poll_frames;
static uint8_t g_save_data_valid;

static void ape_recomp_achievements_mark_casual_internal(const char *reason) {
    if (g_casual_sent)
        return;
    g_casual_sent = 1u;
    if (g_initialized) {
        rl_ach_casual(reason != NULL ? reason : "cheats");
    } else {
        g_pending_casual = 1u;
        (void)snprintf(g_pending_casual_reason,
                       sizeof(g_pending_casual_reason), "%s",
                       reason != NULL ? reason : "cheats");
    }
}

void ape_recomp_achievements_mark_casual(const char *reason) {
    ape_recomp_achievements_mark_casual_internal(reason);
}

static int ape_read_saved_monkeys(uint32_t *best_total) {
    uint32_t best = 0u;
    int found = 0;
    int card;

    if (best_total == NULL)
        return 0;

    for (card = 0; card < 2; ++card) {
        unsigned block;
        if (!memcard_is_present(card))
            continue;

        for (block = 1u; block < 16u; ++block) {
            uint8_t sector[MEMCARD_SECTOR_SIZE];
            uint32_t total = 0u;
            unsigned i;
            int valid = 1;

            if (memcard_read_sector(
                    card,
                    (int)(block * APE_CARD_BLOCK_SECTORS +
                          APE_CARD_DATA_SECTOR),
                    sector) != 0)
                continue;

            if (sector[APE_CARD_MARKER_OFFSET] != 0x7Fu ||
                sector[APE_CARD_MARKER_OFFSET + 1u] != 0x4Bu)
                continue;

            for (i = 0u; i < 19u; ++i) {
                const uint8_t count =
                    sector[APE_CARD_MONKEY_OFFSET + APE_SAVE_STAGE_INDEX[i]];
                if (count > APE_SAVE_STAGE_MAX[i]) {
                    valid = 0;
                    break;
                }
                total += count;
            }

            if (!valid || total > 204u)
                continue;

            found = 1;
            if (total > best)
                best = total;
        }
    }

    if (found)
        *best_total = best;
    return found;
}

static void ape_update_saved_progress(void) {
    uint32_t saved_total = 0u;

    ++g_save_poll_frames;
    if (g_save_poll_frames < 120u)
        return;
    g_save_poll_frames = 0u;

    if (!ape_read_saved_monkeys(&saved_total))
        return;

    if (!g_save_data_valid) {
        /* A new card save may be created after this game session has started. */
        g_saved_monkeys = saved_total;
        g_saved_monkeys_at_start =
            saved_total > g_session_new_catches
                ? saved_total - g_session_new_catches
                : 0u;
        g_last_saved_monkeys = saved_total;
        g_save_data_valid = 1u;
        return;
    }

    if (saved_total < g_last_saved_monkeys) {
        /* A lower saved total strongly suggests an older save was loaded. */
        rl_ach_save_loaded();
        g_saved_monkeys_at_start = saved_total;
        g_session_new_catches = 0u;
        g_monkey_flags_valid = 0u;
    }

    g_saved_monkeys = saved_total;
    g_last_saved_monkeys = saved_total;
}

static uint32_t ape_total_monkeys(void) {
    const uint32_t session_total =
        g_saved_monkeys_at_start + g_session_new_catches;
    return g_saved_monkeys > session_total ? g_saved_monkeys : session_total;
}

static void ape_check_monkey_milestones(uint32_t total) {
    if (total >= 1u && !g_monkey_one_sent) {
        g_monkey_one_sent = 1u;
        rl_ach_event("caught_one_monkey");
    }
    if (total >= 10u && !g_monkey_ten_sent) {
        g_monkey_ten_sent = 1u;
        rl_ach_event("caught_10_monkeys");
    }
    if (total >= 100u && !g_monkey_hundred_sent) {
        g_monkey_hundred_sent = 1u;
        rl_ach_event("caught_100_monkeys");
    }
    if (total >= 204u && !g_monkey_all_sent) {
        g_monkey_all_sent = 1u;
        rl_ach_event("caught_all_204_monkeys");
    }
}

static void ape_check_stage_and_gadget_unlocks(void) {
    size_t i;
    const uint8_t gadgets = psx_mod_read_byte(APE_GADGET_MASK_ADDRESS);

    for (i = 0u; i < sizeof(APE_STAGES) / sizeof(APE_STAGES[0]); ++i) {
        if (!g_stage_sent[i] &&
            psx_mod_read_byte(APE_STAGES[i].address) >= 2u) {
            g_stage_sent[i] = 1u;
            rl_ach_event(APE_STAGES[i].event);
        }
    }

    for (i = 0u; i < sizeof(APE_GADGETS) / sizeof(APE_GADGETS[0]); ++i) {
        if (!g_gadget_sent[i] && (gadgets & APE_GADGETS[i].mask) != 0u) {
            g_gadget_sent[i] = 1u;
            rl_ach_event(APE_GADGETS[i].event);
        }
    }
}

static void ape_track_live_monkey_catches(void) {
    uint8_t current[APE_MONKEYS_PER_STAGE];
    uint16_t timer;
    unsigned i;
    unsigned caught = 0u;

    if (psx_mod_read_byte(APE_PHASE_ADDRESS) != APE_PHASE_PLAYING) {
        g_monkey_flags_valid = 0u;
        g_previous_timer_valid = 0u;
        return;
    }

    timer = psx_mod_read_half(APE_STAGE_TIMER_ADDRESS);
    if (g_previous_timer_valid && timer + 60u < g_previous_timer)
        g_monkey_flags_valid = 0u;

    g_previous_timer = timer;
    g_previous_timer_valid = 1u;

    for (i = 0u; i < APE_MONKEYS_PER_STAGE; ++i) {
        current[i] = psx_mod_read_byte(
            APE_MONKEY_FLAGS_ADDRESS + i * APE_MONKEY_FLAG_STRIDE);
        if (current[i] == 1u)
            ++caught;
    }

    if (!g_monkey_flags_valid) {
        memcpy(g_previous_monkey_flags, current, sizeof(current));
        g_monkey_flags_valid = 1u;
    } else {
        for (i = 0u; i < APE_MONKEYS_PER_STAGE; ++i) {
            if (g_previous_monkey_flags[i] != 1u && current[i] == 1u)
                ++g_session_new_catches;
        }
        memcpy(g_previous_monkey_flags, current, sizeof(current));
    }

    if (caught > 0u && !g_monkey_one_sent) {
        g_monkey_one_sent = 1u;
        rl_ach_event("caught_one_monkey");
    }
}

static void ape_recomp_achievements_vblank(void) {
    if (!psx_mod_game_started())
        return;

    if (!g_initialized) {
        rl_ach_set_info("ape-escape-recompiled", "2.0.0");
        (void)rl_ach_init("psx", NULL, NULL);
        g_initialized = 1u;

        if (g_pending_casual) {
            if (g_pending_casual_reason[0] != '\0')
                rl_ach_casual(g_pending_casual_reason);
            else
                rl_ach_casual("cheats");
            g_pending_casual = 0u;
        }

        /* A fresh profile must remain eligible for Ranked achievements. Only
         * an actual existing Ape Escape save gets the save-loaded marker. */
        if (ape_read_saved_monkeys(&g_saved_monkeys)) {
            rl_ach_save_loaded();
            g_saved_monkeys_at_start = g_saved_monkeys;
            g_last_saved_monkeys = g_saved_monkeys;
            g_save_data_valid = 1u;
        }
    }

    rl_ach_frame();
    ape_check_stage_and_gadget_unlocks();
    ape_track_live_monkey_catches();
    ape_update_saved_progress();
    ape_check_monkey_milestones(ape_total_monkeys());
}

static void ape_recomp_achievements_savestate_loaded(void) {
    if (g_initialized) {
        rl_ach_casual("save_state");
        rl_ach_save_loaded();
    } else {
        ape_recomp_achievements_mark_casual_internal("save_state");
    }
    g_monkey_flags_valid = 0u;
    g_previous_timer_valid = 0u;
}

int ape_recomp_achievements_initialized(void) {
    return g_initialized != 0u;
}

#else

void ape_recomp_achievements_mark_casual(const char *reason) {
    (void)reason;
}

int ape_recomp_achievements_initialized(void) {
    return 0;
}

static void ape_recomp_achievements_vblank(void) {}
static void ape_recomp_achievements_savestate_loaded(void) {}

#endif

PSX_MOD_CONSTRUCTOR(register_ape_recomp_achievements) {
    (void)psx_mod_register_vblank_plugin(
        "ape.recomp-achievements", ape_recomp_achievements_vblank);
    (void)psx_mod_register_savestate_plugin(
        "ape.recomp-achievements", ape_recomp_achievements_savestate_loaded);
}
