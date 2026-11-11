# Two Spike co-op prototype

This branch upgrades the original follower preview into an **experimental, independently controlled P2 prototype**. It remains disabled by default.

## Player 2 input

P2 reads the second PlayStation controller port (SIO slot 2), including digital buttons and both analog sticks. A second physical controller can be assigned through the launcher's Player 2 input card.

The built-in local keyboard layout is:

| P2 action | Default key |
|---|---|
| Move | Arrow keys |
| Gadget aim | I / J / K / L |
| Cross / Circle / Square / Triangle | Right Ctrl / Right Shift / Right Alt / Enter |
| L1 / R1 / L2 / R2 | U / O / Y / P |
| Start / Select | Backspace / Insert |

Keyboard input is added to the P2 pad before the native Spike update. For **online** sessions, also assign the keyboard to Player 2 in the launcher input card so the keys are included in the framework's synchronized input sample; the local SDL keyboard fallback itself is not a network transport.

## Separate player state

P2 keeps separate Expansion 1 copies of:

- Spike state and animation/event history
- Pad buttons, pressed-edge history, left/right sticks, magnitudes, and camera-relative direction vectors
- Gadget state and gadget auxiliary state

This prevents the second update from resetting its animation/gadget state to P1 every frame. P2's current gadget and action state can advance independently.

P2 collision registrations are routed to the P2 Spike object instead of the global P1 Spike pointer. This enables distinct collision ownership; enemy, monkey, platform, and gadget interactions still need in-game verification.

## Shared systems and current limits

- One shared camera, still following P1.
- One shared HUD/inventory display.
- The CD-backed idle-only animation path is suppressed for P2 until it has a separate resource context.
- Online co-op is configured for **delay-sync netplay** through recomp-net and ICE. Rollback and savestate transfer are deliberately disabled until deterministic state restore is validated.
- Stage scripts and other game systems may still contain single-player assumptions. This is an experimental gameplay branch, not a finished co-op release.

## Enable and test

Offline: select **Mods → Experimental Gameplay → Two Spike Preview**, then enter a normal level.

Test P2 separately from P1:

1. Move P1 and P2 in different directions; confirm they no longer follow each other.
2. Stop, walk, jump, and turn both characters; watch for animation state getting stuck or restarting.
3. Try Stun Club and Net actions repeatedly; confirm the previous buzzing sound is gone and the gadget animations continue normally.
4. Check P2 collisions against terrain, platforms, monkeys, and enemies.
5. Confirm P1's camera, HUD, and controls remain stable.
6. Only after the local test passes, try a two-peer netplay session. Both peers must use the same build/profile and select the appropriate Player 2 input source.

Runtime counters under `ape.coop.two-spikes.*` report activation, spawn, P2 updates, and collision-owner routing. A successful compile is not proof of in-game correctness; this branch still requires playtesting with the retail disc.
