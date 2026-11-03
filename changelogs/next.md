# next

## New
- Voting: Pause > VOTE now has the GAMEMODE / MODIFIERS / MAP toggles right on it, one click to start, and BACK steps back through the vote. New vote modifiers: FIRE RATE and PROJECTILE SPEED.
- Acid: standing in it tints your screen green, pulses your health bar and sizzles. Shots and stickies that go into lava or acid are destroyed with a hiss.
- **Player saturation** (VIDEO > MORE 4 > PLAYERS > SATURATION, next to BRIGHTNESS): makes players' colours more vivid (up to 2x) or greyer, without touching the map, so enemies stand out like with digital vibrance but only on them.

## Fixes
- Fixed a rare wrong trail or missing explosion after very long matches, players drawn up to a second in the past after a server clock reset, and giants hidden behind cover their head stuck out over.
- Dedicated servers: re-running install.sh no longer puts the testing server back on the production build, and a server that can't start yet keeps retrying instead of staying down.
- macOS builds come out with each testing build again. When GitHub has no Apple Silicon Mac free, the macOS build now runs on an Intel Mac (it still makes the same universal app) instead of being dropped.
- Ball: running or dashing into the ball no longer hurts you. The ball only does damage when it's moving over 20 m/s itself and over 25 m/s relative to you.

## Maps
- **Payload has its own default map:** payload now picks Railyard (it still plays neutral payload and ball). The ranked ladders are unchanged.
- **Black Hole** can now be played in duel and team vs too.
- Small geometry fixes: walls that were drawn twice on Mine, QK6, Cruise Ship and Railyard (two door sills on Mine flickered between two materials), a doubled floodlight mast pair on Railyard, two invisible boxes on Black Hole.
- Map editor (Quarkology): its checks now agree with the game's tests. Greenhouse and Spire no longer fail the ranked check (ranked maps must be fair, not mirror images), Solar no longer fails its routes, and Tutorial no longer shows 46,000 m² out of bounds.

## Performance
- **Higher frame rates and lows when the GPU is the limit:** the world draws with the GPU's early depth test while no holes have been blown in breakable walls. On a 3090 Ti at 1440p that's +3.8% average FPS, +9.4% on the 1% lows and +11.3% on the 0.1% lows, with identical pictures. It's a switch on the LATENCY tab, **EARLY DEPTH TEST** (on by default), so you can compare it yourself.
- **Higher frame rates from a leaner renderer:** players, props and effects upload 40% less data each frame, the shadow pass only touches the part of its map that's used, and stuck knives and bolts skip per-frame maths. On a 3090 Ti at 1440p: +6.9% average FPS, +4.0% on the 1% and 0.1% lows (before the early depth test above, which stacks on top).
- **Faster collision and line-of-sight checks** (14-32% per ray, server ticks ~4% faster) with identical results.
- **Smoother loading screen** (about 30 FPS instead of 16 on Windows) and fewer small hitches in the first seconds after a map loads.
- **Dedicated servers:** with game logging on, writing the log to disk no longer stalls a tick.
- **Dedicated servers run about 5% faster per tick** (Linux): the server is now compiled using a profile of its own ticks, with bots thinking about 6% faster. Gameplay is exactly the same.
- **No stutter when changing colours with PATTERN FADE or CLEAN SURFACES on:** dragging GLOW STRENGTH, HIGH CONTRAST or the sky and glow colours no longer makes the CPU wait for the graphics card.
- **BORDERLESS is now the default display** instead of a window: a window goes through the desktop compositor, which costs frames and latency. If you were on WINDOWED (the old default), this update moves you to BORDERLESS once; set it back in VIDEO and it stays.
- **Slightly faster HUD, menus and server ticks:** rounding and square roots no longer call the C maths library.

## Latency
- BENCHMARK results: the frame-time graph is now a line, zoomed so the run's fastest and slowest frames are its edges, with MIN, MAX and AVG shown in µs. Each frame's spread shows faintly behind the line. STDEV, AVG FRAME and the worst frame are in µs too.
- FPS LIMIT holds its rate much more evenly: the limiter used to wake up late now and then (a 1000 cap's 0.1% low was 589). It now learns how late your PC's timer wakes and spins the rest, so capped frames come out on time. While the game has focus, its main thread also runs at the highest priority, and Windows 11 can no longer throttle it onto an efficiency core.
- New LATENCY setting, FRAME CAP OFFSET: with an FPS limit on, the game wakes this much early, does its network and GPU check, and then reads your mouse exactly on the beat, so small hold-ups no longer make a frame late. AUTO is 5% of a frame (50 µs at 1000). You can set it in µs or turn it OFF.
- Fixed a tiny hitch twice a second while the mouse is captured: the game re-centred the cursor every half second even when it was already there.
- F3's graph: the gap between SWAP and the GPU no longer creeps up for 10 seconds and then snaps back. That was the measurement, not your frames. The graph also updates 10 times a second now, not twice.
- **New LATENCY settings tab**, with every latency tweak on its own switch so you can try each combination and compare them in F3 (which now names the tweaks it was taken with):
  - LOW LATENCY (OFF / ON / ULTRA), EARLY FLUSH, CPU LATE LATCH, **GPU LATE LATCH** (new: on Windows the graphics card reads your newest mouse movement as it starts drawing the world, after the frame was sent; off by default) and **HUD BEFORE WORLD** (new, ULTRA only: the HUD is drawn before the last mouse read and laid over the world after it; off by default).
  - GPU SCHEDULING and NO FULLSCREEN OPT. moved here from VIDEO + AUDIO > MORE.
- **ULTRA's safety margin now learns itself (AUTO):** it starts at 150 µs and settles at the smallest margin your PC needs, so ULTRA reads your mouse as late as it safely can. F3 shows the learnt value. You can still pick a fixed margin in VIDEO + AUDIO > MORE.
  - **PERFORMANCE CHECK**: the game reads your graphics driver's settings (NVIDIA; AMD Radeon, integrated Radeon Graphics too; Intel, integrated and Arc) and Windows' power plan, power mode, battery, Game Mode, GPU scheduling and which GPU it runs on, and lists anything that costs frames or adds latency with where to change it: triple buffering, forced vsync, frames queued up, frame caps (Chill, FRTC, max frame rate, Intel's limiter), Radeon Boost, Enhanced Sync, Anti-Lag or Intel Low Latency off, Intel Endurance Gaming, power saving, the integrated GPU while a dedicated one is there.

## Languages
- **The launcher speaks your language:** OPTIONS has a LANGUAGE choice, and it's the same setting as the game's. Change it in either one and the other follows. The launcher's buttons, messages and help are translated into all 10 languages.
- **AUTO now follows your system's display language.** On Windows it used to follow the region format, so English Windows with a German date format got German. It now reads the display language, then your other preferred languages in order, and English if none of them is one of the game's. Mac apps now use System Settings' language list too.
- **More of the game is translated:** the aim trainer, the info pages' headings, settings hints with numbers in them, and labels with a value after them.
- Chinese, Japanese and Korean text fills the line before wrapping, and F3's latency rows no longer run into their numbers.
- Squeezed labels keep their word spaces.
- Fixed wrong words: Russian VOTES, Turkish ADVANCED, and the spectator FOV's MINE ("my own", not the explosive).

## Tutorial
- **MAIN MENU > TUTORIAL:** every movement tech, one lesson each. A ghost bot shows the tech first while the camera follows it, with the keys it presses lit under your own key names. Then it's your turn in a lane beside it while it keeps repeating the move. Each lesson has short steps, a goal and a bar that fills as you do it.
- 28 lessons, ten of them marked **KEY TECH** and listed first. Each lesson's numbers are what the game really does, measured in the tests:
  - **The launch**: grapple along the floor to 24 m/s, then look straight down and jump, dash and fire a rocket together. You fly about 50 m up.
  - **Pop**: a super, then a hyper straight down as you land: about 14 m up. **Rocket pop**: the same with a rocket fired the moment the dash goes: about 47 m up, no grapple needed.
  - **Rocket super**: a super, then a rocket straight down: about 33 m/s along, past the dash's 32 m/s cap. **Hook boost**: grapple walking at 24 m/s, a rocket straight down while still hooked: about 40 m/s along.
  - **Sticky stack**: three stickies at your feet set off together: 50 m/s up (one: 22, two: 36).
  - **Bubble launch**: stand still, look straight down and alt-fire the bubble gun: 58 m/s up, about 73 m (about 12 hp in a real match). From a run it throws you along at 25 m/s; a jump first or an enemy within 5 m makes it much weaker.
  - **Grapple a player**: hook the PARTNER, a second bot running across the bay. Your hook latches on and pulls you both together. **Dash out of a grapple**: the PARTNER hooks you and any dash breaks free at once.
  - **The rocket grapple**: fire a rocket and hook it as it leaves: it carries you along.
  - And walking, AD strafing, the dash, sideways dashes, super, hyper, super chaining, the dash slide, the wall bounce, the **ramp dash** (a dash runs up a 60 degree slope you can't walk up), the grapple, the **ceiling hang**, the **swing turn** (hook a pillar at speed to turn round it), grapple walking, the **hook-start chain** (grapple walk to 24 m/s, then hyper every landing: 35 m/s), rocket and sticky jumps and the **fall bounce** (hold jump landing from a big fall: a 15.7 m/s hop and a dash back).
- The lessons spell out two rules: a dash erases a blast's push (fire after the dash, never just before it), and a dash above 32 m/s (a hyper above 36) slows you down to the cap.
- JUMP (or a click) skips the demo, R starts the lesson over and ENTER skips to the next lesson. Esc opens LESSONS (pick any lesson; the ones you've done get a green mark), WATCH AGAIN and SETTINGS. Falling in the lava puts you back at the lesson's start, and nothing does damage in the tutorial.
- The first time you start the game it offers the tutorial once (START TUTORIAL or NOT NOW).
- `quark --tutorial [LESSON]` starts it straight away, for example `quark --tutorial launch`.
- The main menu's HOST GAME button is now half width, with TUTORIAL beside it.

## Info
- INFO > CONTROLS fixed where it was out of date: there is no air drift (a standing jump drifts 2.3 m/s at most), an air dash adds about a third of what you are below 32 m/s, and the syringe heals 5 hp (a throw: 15 hp).
