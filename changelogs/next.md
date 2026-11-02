# Next (testing builds since 1.19.7)

What's on `testing` since production 1.19.7. The release PR renames this file to `changelogs/<VERSION>.md`.

## Latency
- **New LATENCY settings tab**, with every latency tweak on its own switch so you can try each combination and compare them in F3 (which now names the tweaks it was taken with):
  - LOW LATENCY (OFF / ON / ULTRA), EARLY FLUSH, CPU LATE LATCH, **GPU LATE LATCH** (new: on Windows the graphics card reads your newest mouse movement as it starts drawing the world, after the frame was sent; off by default) and **HUD BEFORE WORLD** (new, ULTRA only: the HUD is drawn before the last mouse read and laid over the world after it; off by default).
  - GPU SCHEDULING and NO FULLSCREEN OPT. moved here from VIDEO + AUDIO > MORE.
  - **PERFORMANCE CHECK**: the game reads your graphics driver's settings (NVIDIA; AMD Radeon, integrated Radeon Graphics too; Intel, integrated and Arc) and Windows' power plan, power mode, battery, Game Mode, GPU scheduling and which GPU it runs on, and lists anything that costs frames or adds latency with where to change it: triple buffering, forced vsync, frames queued up, frame caps (Chill, FRTC, max frame rate, Intel's limiter), Radeon Boost, Enhanced Sync, Anti-Lag or Intel Low Latency off, Intel Endurance Gaming, power saving, the integrated GPU while a dedicated one is there.
