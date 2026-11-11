#pragma once

/* Marks the active RecompLauncher achievement session as Casual when a
 * gameplay-altering assist/cheat is used. Safe before SDK initialization. */
void ape_recomp_achievements_mark_casual(const char *reason);
int ape_recomp_achievements_initialized(void);
