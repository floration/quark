# next

## New
- Voting: Pause > VOTE now has the GAMEMODE / MODIFIERS / MAP toggles right on it, one click to start, and BACK steps back through the vote. New vote modifiers: FIRE RATE and PROJECTILE SPEED.
- Acid: standing in it tints your screen green, pulses your health bar and sizzles. Shots and stickies that go into lava or acid are destroyed with a hiss.

## Fixes
- Fixed a rare wrong trail or missing explosion after very long matches, players drawn up to a second in the past after a server clock reset, and giants hidden behind cover their head stuck out over.
- Dedicated servers: re-running install.sh no longer puts the testing server back on the production build, and a server that can't start yet keeps retrying instead of staying down.
- macOS builds come out with each testing build again. When GitHub has no Apple Silicon Mac free, the macOS build now runs on an Intel Mac (it still makes the same universal app) instead of being dropped.

## Performance
- **Higher frame rates and lows when the GPU is the limit:** the world draws with the GPU's early depth test while no holes have been blown in breakable walls. On a 3090 Ti at 1440p that's +3.8% average FPS, +9.4% on the 1% lows and +11.3% on the 0.1% lows, with identical pictures. It's a switch on the LATENCY tab, **EARLY DEPTH TEST** (on by default), so you can compare it yourself.
- **Higher frame rates from a leaner renderer:** players, props and effects upload 40% less data each frame, the shadow pass only touches the part of its map that's used, and stuck knives and bolts skip per-frame maths. On a 3090 Ti at 1440p: +6.9% average FPS, +4.0% on the 1% and 0.1% lows (before the early depth test above, which stacks on top).
- **Faster collision and line-of-sight checks** (14-32% per ray, server ticks ~4% faster) with identical results.
- **Smoother loading screen** (about 30 FPS instead of 16 on Windows) and fewer small hitches in the first seconds after a map loads.
- **Dedicated servers:** with game logging on, writing the log to disk no longer stalls a tick.
- **Maps load faster:** the world shader compiles while the light bakes, and is saved so the next load of a map skips it.
- **No stutter when changing colours with PATTERN FADE or CLEAN SURFACES on:** dragging GLOW STRENGTH, HIGH CONTRAST or the sky and glow colours no longer makes the CPU wait for the graphics card.
- **BORDERLESS is now the default display** instead of a window: a window goes through the desktop compositor, which costs frames and latency. If you were on WINDOWED (the old default), this update moves you to BORDERLESS once; set it back in VIDEO and it stays.
- **Slightly faster HUD, menus and server ticks:** rounding and square roots no longer call the C maths library.

## Latency
- **New LATENCY settings tab**, with every latency tweak on its own switch so you can try each combination and compare them in F3 (which now names the tweaks it was taken with):
  - LOW LATENCY (OFF / ON / ULTRA), EARLY FLUSH, CPU LATE LATCH, **GPU LATE LATCH** (new: on Windows the graphics card reads your newest mouse movement as it starts drawing the world, after the frame was sent; off by default) and **HUD BEFORE WORLD** (new, ULTRA only: the HUD is drawn before the last mouse read and laid over the world after it; off by default).
  - GPU SCHEDULING and NO FULLSCREEN OPT. moved here from VIDEO + AUDIO > MORE.
- **ULTRA's safety margin now learns itself (AUTO):** it starts at 150 µs and settles at the smallest margin your PC needs, so ULTRA reads your mouse as late as it safely can. F3 shows the learnt value. You can still pick a fixed margin in VIDEO + AUDIO > MORE.
  - **PERFORMANCE CHECK**: the game reads your graphics driver's settings (NVIDIA; AMD Radeon, integrated Radeon Graphics too; Intel, integrated and Arc) and Windows' power plan, power mode, battery, Game Mode, GPU scheduling and which GPU it runs on, and lists anything that costs frames or adds latency with where to change it: triple buffering, forced vsync, frames queued up, frame caps (Chill, FRTC, max frame rate, Intel's limiter), Radeon Boost, Enhanced Sync, Anti-Lag or Intel Low Latency off, Intel Endurance Gaming, power saving, the integrated GPU while a dedicated one is there.

## Languages
- **More of the game is translated:** the aim trainer, the info pages' headings, settings hints with numbers in them, and labels with a value after them.
- Chinese, Japanese and Korean text fills the line before wrapping, and F3's latency rows no longer run into their numbers.
- Squeezed labels keep their word spaces.
- Fixed wrong words: Russian VOTES, Turkish ADVANCED, and the spectator FOV's MINE ("my own", not the explosive).
