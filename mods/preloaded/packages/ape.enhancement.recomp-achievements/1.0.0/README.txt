RecompLauncher Achievements (v1.0.0)

This is a non-gameplay integration. It starts enabled and reports unlock events
to the desktop RecompLauncher through the official Recomp Achievements SDK.

Coverage:
- Completion of all 22 documented stage entries (the game's completion status
  byte must reach 2; status 1 merely makes a stage available).
- All eight gadget bits: Stun Club, Time Net, Monkey Radar, Slingshot,
  Super Hoop, Magic Punch, Sky Flyer and RC Car.
- Catch 1, 10, 100 and all 204 monkeys. The live stage table tracks new catches
  during a session; saved per-stage monkey totals are read from the PS1 memory
  card using the published North-American save-format offsets.

Use RecompLauncher to receive toasts and sync achievements to an account. When
the game is started outside RecompLauncher the SDK safely does nothing.

Progress that already exists on the memory card is reported as Casual on first
load. Loading an emulator savestate also marks subsequent unlocks Casual.
Gameplay Assist cheats mark the session Casual. This integration does not
include RetroAchievements and does not alter the original game.
