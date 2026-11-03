# next

## New
- Voting: Pause > VOTE now has the GAMEMODE / MODIFIERS / MAP toggles right on it, one click to start, and BACK steps back through the vote. New vote modifiers: FIRE RATE and PROJECTILE SPEED.
- Acid: standing in it tints your screen green, pulses your health bar and sizzles. Shots and stickies that go into lava or acid are destroyed with a hiss.

## Fixes
- Fixed a rare wrong trail or missing explosion after very long matches, players drawn up to a second in the past after a server clock reset, and giants hidden behind cover their head stuck out over.
- Dedicated servers: re-running install.sh no longer puts the testing server back on the production build, and a server that can't start yet keeps retrying instead of staying down.
- macOS builds come out with each testing build again. When GitHub has no Apple Silicon Mac free, the macOS build now runs on an Intel Mac (it still makes the same universal app) instead of being dropped.
