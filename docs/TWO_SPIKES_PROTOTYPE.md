# Two Spike co-op prototype

This branch provides an **experimental, independently controlled P2 prototype**. The Two Spike Preview feature remains disabled by default.

## Keyboard options

When the feature is enabled, its options include:

- **Player 1 keyboard controls** — enabled by default
- **Player 2 keyboard controls** — enabled by default

Turn either option off in the feature's options to use only that player's assigned controller. Both layouts are local/offline keyboard input; online sessions must use the launcher's synchronized Player 1 and Player 2 input assignments.

### Player 1 defaults

| Action | Key |
|---|---|
| Move | W / A / S / D |
| Gadget aim | T / F / G / H |
| Cross / Circle / Square / Triangle | Z / X / C / V |
| L1 / R1 / L2 / R2 | Q / E / R / B |
| Start / Select | Space / Tab |

### Player 2 defaults

| Action | Key |
|---|---|
| Move | Arrow keys |
| Gadget aim | I / J / K / L |
| Cross / Circle / Square / Triangle | Right Ctrl / Right Shift / Right Alt / Enter |
| L1 / R1 / L2 / R2 | U / O / Y / P |
| Start / Select | Backspace / Insert |

The two layouts use separate keys so one keyboard can operate both players at once. A second physical controller can also drive P2 through logical SIO pad 2.

## Separate player state

P2 keeps separate Expansion 1 copies of:

- Spike state and animation/event history
- Pad buttons, pressed-edge history, left/right sticks, magnitudes, and camera-relative direction vectors
- Gadget state and gadget auxiliary state

P2 collision registrations are routed to the P2 Spike object instead of the global P1 Spike pointer. Enemy, monkey, platform, and gadget interactions still need in-game verification.

## Shared systems and current limits

- One shared camera, still following P1.
- One shared HUD/inventory display.
- The CD-backed idle-only animation path is suppressed for P2 until it has a separate resource context.
- Online co-op is configured for **delay-sync netplay** through recomp-net and ICE. Rollback and savestate transfer are deliberately disabled until deterministic state restore is validated.
- The hardcoded keyboard layouts are local/offline only. For online play, assign keyboard input to both player slots using the launcher's Player 1 and Player 2 input cards so the framework synchronizes both players' samples.
- Stage scripts and other game systems may still contain single-player assumptions. This is an experimental gameplay branch, not a finished co-op release.

## Enable and test

1. Select **Mods → Experimental Gameplay → Two Spike Preview**.
2. Leave both keyboard-control options enabled, or disable either one to test that player's controller-only path.
3. Enter a normal level.
4. Use WASD to move P1 and the arrow keys to move P2 in different directions.
5. Test both aim controls, gadget buttons, jumping, and movement simultaneously.
6. Try Stun Club and Net actions repeatedly; check for buzzing and stuck/restarting animations.
7. Check collisions against terrain, platforms, monkeys, and enemies.
8. Confirm P1's camera, HUD, and controls remain stable.

Runtime counters under `ape.coop.two-spikes.*` report activation, spawn, P2 updates, and collision-owner routing. A successful compile is not proof of in-game correctness; this branch still requires playtesting with the retail disc.
