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
#define APE_MONKEYS_PER_STAGE 24u
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
static uint8_t
    g_stage_previous_completed[sizeof(APE_STAGES) / sizeof(APE_STAGES[0])];
static uint8_t g_stage_state_valid;
static uint8_t g_previous_gadget_mask;
static uint8_t g_gadget_state_valid;
static uint8_t g_monkey_one_sent;
static uint8_t g_monkey_ten_sent;
static uint8_t g_monkey_hundred_sent;
static uint8_t g_monkey_all_sent;
static uint8_t g_monkey_flags_valid;
static uint8_t g_previous_monkey_flags[APE_MONKEYS_PER_STAGE];
static uint8_t g_previous_timer_valid;
static uint16_t g_previous_timer;
static uint32_t g_session_new_catches;
static uint32_t g_saved_monkeys_at_start;
static uint32_t g_pending_saved_monkeys;
static uint8_t g_pending_save_load;
static uint8_t g_card_read_candidate_valid;
static int g_card_read_candidate_card;
static uint16_t g_card_read_candidate_sector;
static uint32_t g_card_read_candidate_monkeys;
static uint8_t g_pending_progress_snapshot;
static uint8_t g_pending_snapshot_idle_frames;

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

static int ape_saved_card_total(const uint8_t *sector, uint32_t *total_out) {
  uint32_t total = 0u;
  unsigned i;

  if (sector == NULL || total_out == NULL ||
      sector[APE_CARD_MARKER_OFFSET] != 0x7Fu ||
      sector[APE_CARD_MARKER_OFFSET + 1u] != 0x4Bu)
    return 0;

  for (i = 0u; i < 19u; ++i) {
    const uint8_t count =
        sector[APE_CARD_MONKEY_OFFSET + APE_SAVE_STAGE_INDEX[i]];
    if (count > APE_SAVE_STAGE_MAX[i])
      return 0;
    total += count;
  }

  if (total > 204u)
    return 0;
  *total_out = total;
  return 1;
}

/* Called only after a guest-initiated SIO memory-card sector read completes.
 * The marker is in the game's save payload (block-relative sector 3), not in
 * the host-side card scan used for diagnostics or launcher save browsing. */
void ape_recomp_achievements_memcard_read_complete(int card, uint16_t sector,
                                                   const uint8_t *data128) {
  uint32_t saved_total;
  const int valid_save_sector =
      card >= 0 && card < 2 && memcard_is_present(card) &&
      sector < MEMCARD_SECTORS &&
      sector >= APE_CARD_BLOCK_SECTORS + APE_CARD_DATA_SECTOR &&
      (sector % APE_CARD_BLOCK_SECTORS) == APE_CARD_DATA_SECTOR &&
      ape_saved_card_total(data128, &saved_total);

  /* Any further card traffic means the title is still consuming card data. */
  if (g_pending_progress_snapshot)
    g_pending_snapshot_idle_frames = 0u;

  /*
   * A card browser can inspect a save's first data sector without loading it.
   * Treat the marker sector as a candidate only; require the immediately
   * following sector from the same card block before declaring a load.
   */
  if (g_card_read_candidate_valid) {
    if (card == g_card_read_candidate_card &&
        sector == (uint16_t)(g_card_read_candidate_sector + 1u) &&
        (sector / APE_CARD_BLOCK_SECTORS) ==
            (g_card_read_candidate_sector / APE_CARD_BLOCK_SECTORS)) {
      g_pending_saved_monkeys = g_card_read_candidate_monkeys;
      g_pending_progress_snapshot = 1u;
      g_pending_snapshot_idle_frames = 0u;
      g_pending_save_load = 1u;
      g_card_read_candidate_valid = 0u;
      return;
    }
    g_card_read_candidate_valid = 0u;
  }

  if (valid_save_sector) {
    g_card_read_candidate_card = card;
    g_card_read_candidate_sector = sector;
    g_card_read_candidate_monkeys = saved_total;
    g_card_read_candidate_valid = 1u;
  }
}

static void ape_snapshot_progress(uint32_t saved_monkeys) {
  size_t i;
  for (i = 0u; i < sizeof(APE_STAGES) / sizeof(APE_STAGES[0]); ++i) {
    g_stage_previous_completed[i] =
        psx_mod_read_byte(APE_STAGES[i].address) >= 2u ? 1u : 0u;
  }
  g_stage_state_valid = 1u;

  g_previous_gadget_mask = psx_mod_read_byte(APE_GADGET_MASK_ADDRESS);
  g_gadget_state_valid = 1u;

  g_saved_monkeys_at_start = saved_monkeys;
  g_session_new_catches = 0u;
  g_monkey_one_sent = saved_monkeys >= 1u;
  g_monkey_ten_sent = saved_monkeys >= 10u;
  g_monkey_hundred_sent = saved_monkeys >= 100u;
  g_monkey_all_sent = saved_monkeys >= 204u;

  /* The first gameplay frame after this baseline is observation-only. */
  g_monkey_flags_valid = 0u;
  g_previous_timer_valid = 0u;
}

static uint32_t ape_total_monkeys(void) {
  return g_saved_monkeys_at_start + g_session_new_catches;
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

    if (!g_stage_state_valid || !g_gadget_state_valid) {
      for (i = 0u; i < sizeof(APE_STAGES) / sizeof(APE_STAGES[0]); ++i) {
        g_stage_previous_completed[i] =
            psx_mod_read_byte(APE_STAGES[i].address) >= 2u ? 1u : 0u;
      }
      g_stage_state_valid = 1u;
      g_previous_gadget_mask = gadgets;
      g_gadget_state_valid = 1u;
      return;
    }

    for (i = 0u; i < sizeof(APE_STAGES) / sizeof(APE_STAGES[0]); ++i) {
      const uint8_t completed =
          psx_mod_read_byte(APE_STAGES[i].address) >= 2u ? 1u : 0u;
      if (!g_stage_previous_completed[i] && completed)
        rl_ach_event(APE_STAGES[i].event);
      g_stage_previous_completed[i] = completed;
    }

    for (i = 0u; i < sizeof(APE_GADGETS) / sizeof(APE_GADGETS[0]); ++i) {
      const uint8_t mask = APE_GADGETS[i].mask;
      if ((g_previous_gadget_mask & mask) == 0u && (gadgets & mask) != 0u)
        rl_ach_event(APE_GADGETS[i].event);
    }
    g_previous_gadget_mask = gadgets;
}

static void ape_track_live_monkey_catches(void) {
    uint8_t current[APE_MONKEYS_PER_STAGE];
    uint16_t timer;
    unsigned i;

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
    }

    if (!g_monkey_flags_valid) {
        memcpy(g_previous_monkey_flags, current, sizeof(current));
        g_monkey_flags_valid = 1u;
        return;
    }

    for (i = 0u; i < APE_MONKEYS_PER_STAGE; ++i) {
      if (g_previous_monkey_flags[i] != 1u && current[i] == 1u)
        ++g_session_new_catches;
    }
    memcpy(g_previous_monkey_flags, current, sizeof(current));
}

static void ape_recomp_achievements_vblank(void) {
    if (!psx_mod_game_started())
        return;

    if (!g_initialized) {
      rl_ach_set_info("apeescaperecompiled", "2.0.0");
      (void)rl_ach_init("psx", NULL, NULL);
      g_initialized = 1u;

      if (g_pending_casual) {
        if (g_pending_casual_reason[0] != '\0')
          rl_ach_casual(g_pending_casual_reason);
        else
          rl_ach_casual("cheats");
        g_pending_casual = 0u;
      }

      if (!g_pending_progress_snapshot)
        ape_snapshot_progress(0u);
    }

    rl_ach_frame();

    /* A memory-card read is reported by SIO before the game consumes the
     * loaded payload. Wait for card I/O to go quiet and gameplay to resume
     * before baselining restored progress or checking any achievements. */
    if (g_pending_progress_snapshot) {
      if (g_pending_snapshot_idle_frames < 3u) {
        ++g_pending_snapshot_idle_frames;
      } else if (psx_mod_read_byte(APE_PHASE_ADDRESS) == APE_PHASE_PLAYING) {
        if (g_pending_save_load) {
          rl_ach_save_loaded();
          g_pending_save_load = 0u;
        }
        ape_snapshot_progress(g_pending_saved_monkeys);
        g_pending_progress_snapshot = 0u;
        g_pending_snapshot_idle_frames = 0u;
      }

      /* Never compare restored state with the pre-load baseline. */
      if (g_pending_progress_snapshot)
        return;
    }

    ape_check_stage_and_gadget_unlocks();
    ape_track_live_monkey_catches();
    ape_check_monkey_milestones(ape_total_monkeys());
}

static void ape_recomp_achievements_savestate_loaded(void) {
    if (g_initialized) {
        rl_ach_casual("save_state");
        rl_ach_save_loaded();
    } else {
        ape_recomp_achievements_mark_casual_internal("save_state");
    }
    g_pending_saved_monkeys = ape_total_monkeys();
    g_pending_progress_snapshot = 1u;
    g_pending_snapshot_idle_frames = 0u;
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

void ape_recomp_achievements_memcard_read_complete(int card, uint16_t sector,
                                                   const uint8_t *data128) {
  (void)card;
  (void)sector;
  (void)data128;
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
