# Two Spike prototype (0.1.0)

This branch adds an **opt-in, first-stage two-Spike preview** to the Ape Escape recomp.

## What this prototype does

- Creates a second saved Spike context in PSXRecomp Expansion 1 memory.
- Re-enters the game's native `Spike_Update` path with that context projected into the original player globals.
- Places Spike 2 at a small world-X offset from Spike 1.
- Runs the second body only during the ordinary active-level mode. The first Spike remains the camera target and receives its stock update afterward.
- Re-seeds P2 from P1's complete Spike state each frame, then runs the native update at a fixed world-X offset. It reuses P1's processed left-stick vector and passes only a limited button mask (`0x0A`); it is a visual/action follower, not a separately simulated player.
- Keeps Spike 2 on the default club and suppresses its `Collision_Set` registrations. That avoids registering second-player hitboxes against the single global `g_Spike` pointer.

## Known limitations

This is **not yet playable two-controller co-op**. Both bodies follow Player 1's movement; Player 2 has no independent pad, gadgets, HUD, monkey targeting, enemy/object collision, platform-rider handling, or second camera. It is intended to validate the first hard milestone: a second Spike state can run through the original game update and appear in a normal level without replacing Player 1's state or camera.

The prototype is disabled by default. Select **Mods → Experimental Gameplay → Two Spike Preview** in a development build to test it. P2 should now mirror P1's current action and gadget while staying beside him. Try a normal level with open ground first. Toggle it off to return to the stock single-player behavior.

## What to test

1. Confirm the game boots and plays normally with the feature disabled.
2. Enable the feature and enter a normal level.
3. Check that Spike 2 appears beside Spike 1 and follows its walking/jump animation.
4. Move, stop, and change direction; check that both bodies stay visible. Note whether the limited shared button mask triggers any unexpected action.
5. Change areas or restart a stage; confirm Spike 2 respawns beside Spike 1.
6. Check that Player 1's camera, HUD, gadget inventory, and normal controls behave as before.

The runtime counters `ape.coop.two-spikes.*` show whether the second update is running and whether its collision registrations are being suppressed.

A successful compile is not proof of in-game stability. The feature needs a local retail-disc playtest before it should be enabled by default or expanded into independent Player 2 input and interactions.
