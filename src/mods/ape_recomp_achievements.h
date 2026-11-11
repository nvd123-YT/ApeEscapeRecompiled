#pragma once

#include <stdint.h>

/* Marks the active RecompLauncher achievement session as Casual when a
 * gameplay-altering assist/cheat is used. Safe before SDK initialization. */
void ape_recomp_achievements_mark_casual(const char *reason);
int ape_recomp_achievements_initialized(void);

/* Called after a guest-initiated memory-card sector read completes. */
void ape_recomp_achievements_memcard_read_complete(
    int card, uint16_t sector, const uint8_t *data128);
