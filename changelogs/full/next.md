# next (full)

Every change since 1.19.7, with what changed and why.

## Build and release (CI)

- **The macOS build goes to an Intel Mac first, with arm64 as the fallback.** On 2026-11-02, from 13:39 to 23:08 UTC, GitHub's macOS arm64 pool was full. Every `macos-latest` job sat queued about 15 minutes, then ended with "The job was not acquired by Runner of type hosted even after multiple attempts". That turned every macos run red (runs 187-205), and those testing builds and releases 1.19.5 and 1.19.6 went out without macOS files. A probe at 22:55 UTC showed the split: `macos-14` got a runner in 3 s, `macos-15-intel` in 19 s, `macos-latest` and `macos-26` only after 13 min, and `macos-15` never did. The build steps moved to `.github/actions/macos-build`. The `macos` job now runs them on `macos-15-intel`, and the new `macos-arm` job (`macos-latest`) runs them only when the Intel job didn't build. Both make the same universal (arm64 + x86_64, macOS 10.15+) files, and `publish` takes whichever one built. The trade-off: when no Intel Mac picks the job up, the arm64 fallback starts about 15 min later and the run still shows that first job as failed. GitHub plans to retire `macos-15-intel` in August 2027; switch the order back before then.

## Tests (quark_tests)

- **modelockstep2: no more "winner at N Hz: no hooks".** The lockstep cases still had a winner stays case. Winner stays was retired (1v1 rotates the line itself now), so a server started in it plays 1v1 (`retiredTo`). Logic compiled for winner stays never hooks into a 1v1 server, which made it fail at 1000, 250 and 125 Hz, on 1.19.6 too. The case is gone, and 1v1's case covers the line.
- **The 3D flying checks (rules flymod, vamp3d) no longer fail under load.** `pump()` counts wall-clock time, and under load the server's own clock falls behind it, so 0.5 s after `addBot` the flymod bot often hadn't spawned yet. With eight busy cores beside it, the check failed in 8 of 8 runs; now it fails in 0 of 8. Both tests now pump server time and wait (for up to 5 s of game time) for the spawn and for the fly flag's snapshot.
- **The movement group's air drift checks follow `kAirDriftAccel`.** The owner set air drift to 0 on 2026-11-01 (protocol 63). The checks still expected 8 m/s² of drift: a standing jump + W at about 7 m/s, a long fall reaching 9.5 m/s, and a sideways push past 3 m/s. That made 3 checks fail. With drift at 0, they now check the strafe push alone: 2.30 m/s for W-only and 2.53 m/s for A/D. If drift comes back, the old numbers apply again.

## Latency

- **ULTRA's pacing loop polls the GPU fence every 10 µs instead of every 100 µs** (optimization thread's Tracy reading, 2026-11-03). Each step was already a spin, so this adds only non-blocking polls. Before, a GPU that finished mid-step was seen up to 100 µs late, the input read started that much later than it could, and the learnt GPU time (`ultraGpu_`) took in the delay too. The loop now has its own Tracy zone ("ultra: pace the input read", about 70-110 µs of every frame on the desktop).

- **LATENCY tab** (owner, 2026-11-02: "actually make a latency settings tab and each tweak gets its own toggle, so i can test every combination myself"). `TAB_LATENCY` sits before SEARCH (8 tabs; `last_tab` clamps to 7). Each tweak that was tied to LOW LATENCY is now its own config key, so any combination can be measured:
  - `early_flush` (on): the two `glFlush()` calls of 1.19.7's early flush (after the shadow pass and after the world's draw calls).
  - `cpu_latch` (on): the CPU late latch, the mouse read again just before the world's draw calls (it still needs LOW LATENCY ON or ULTRA, which time the frame for it).
  - `gpu_latch` (off, new): **GPU late latch.** The view-projection matrix lives in a persistent, coherent uniform buffer (`GL_ARB_buffer_storage`, GL 4.4; a ring of 3 slots so the GPU never reads a slot being written). The world, dynamic and flat shaders take `uLatchVP` from it when `uLatchUse` is set. From the moment the world's draw calls are issued, Windows' raw-mouse thread (`RawMouse::onMotion`) turns each new report into yaw / pitch with the frame's gain and writes the newer matrix straight into the buffer, so the GPU picks up motion that arrived after the CPU sent the frame (the stretch between the late read and the GPU starting the world: F3's DRAW CALLS row onward). Off when: no buffer storage (N/A), no raw input (not Windows), MOUSE ACCEL on (its gain depends on the frame's motion), or the window isn't focused. The mouse thread and the render thread share the slot under a spinlock (`std::atomic_flag`), held for one 64-byte copy. Disarmed before the next frame's read, so the CPU's aim and the GPU's never count a report twice. Things projected on the CPU (name tags, health bars) keep the frame's aim, a sliver behind.
  - `hud_first` (off, new, ULTRA only): **HUD before the world.** The HUD's batches go into their own RGBA8 layer (alpha-preserving `glBlendFuncSeparate`) before the late read, and are laid over the world afterwards with one premultiplied fullscreen triangle, so only that triangle sits between the world and the swap. Costs one fullscreen blend on the GPU; gains the HUD's draw calls (F3's HUD DRAW row) between the late read and the swap. Not measured yet: that's what the switch is for.
  - `disable_fso` and GPU SCHEDULING moved here from VIDEO > MORE.
  - F3's graph has a line naming LOW LATENCY's level and the tweaks that took effect that frame (FLUSH, CPU LATCH, GPU LATCH, HUD FIRST), so screenshots of different combinations can be told apart.
- **PERFORMANCE CHECK** (owner, 2026-11-02: "the game should read your gpu settings (nvidia/amd/intel/integrated) and if you have anything that is making your performance worse it should recommend you change it"). `perfCheck()` (gpuprefs.cpp) runs when the tab opens and on CHECK AGAIN:
  - NVIDIA (NVAPI's global driver profile, read only): power management not at maximum performance, triple buffering on, vertical sync forced on, max pre-rendered frames above 1, threaded optimization on, a MAX FRAME RATE cap. Each with the NVIDIA Control Panel path.
  - The game on an integrated GPU while a dedicated one is present (Windows' graphics preference path).
  - Windows: the Power saver plan, power mode Best power efficiency, running on battery, hardware GPU scheduling off, Game Mode off.
  - AMD (owner, 2026-11-02: "it should also read amd intel integrated gpu settings"): through ADLX (`amdadlx64.dll`, installed with the Adrenalin driver, also on Radeon integrated graphics), read only, for every AMD GPU: Radeon Anti-Lag supported but off, Radeon Chill on (caps the frame rate), Radeon Boost on (drops the resolution while you turn), Enhanced Sync on, Wait for vertical refresh Always on, Frame Rate Target Control on. The game carries no AMD code: it loads the driver's DLL and calls the few interface slots it needs (as AMD's ADLX SDK lays them out); no AMD driver, no DLL, nothing read. If ADLX can't be read, the tab says so and names the settings to check by hand.
  - Intel, integrated and Arc: through the Graphics Control Library (`ControlLib.dll`, installed with the driver; IGCL API 1.1), read only, the game's own profile first and then the global one: Endurance Gaming on (it trades frames for battery), a forced vsync mode (not "application choice" or off), Low Latency off, the frame rate limiter on. Same approach: the driver's DLL, the few calls and structures as Intel's header declares them, nothing linked in.
  - The check runs on its own thread (loading three drivers' libraries can take a moment), so the menu keeps its frame rate; the tab shows CHECKING... meanwhile.
  - Not Windows: WINDOWS ONLY.

## Languages (fixes from a screenshot sweep of every language on the desktop PC after 1.19.7: 69 screenshots and the untranslated-lines list per language)

- **The aim trainer was in English.** `extract.py` didn't read `trainer.cpp` / `trainer.h`. Its scenario descriptions, kinds and difficulties are now keys; scenario names stay English, like map names.
- **Info-page headings never translated.** Their keys are `# TEAM MODES` and so on, but `#` is the number placeholder, so the drawn heading (without the `# `) never matched. The whole `# ...` line is now translated, then its `# ` is cut.
- **`extract.py` mangled or dropped paragraphs:** its printf rule read "10% damage" as `% d` (the key said "10#amage"); it now ignores the space flag. It dropped the "~13 m/s" paragraph for the `~` and the ".qmap" one under its file-name rule. Upper-case hints with `=`, `<`, `>` or `/` ("WIDTH 0 = NO OUTLINE", "PING: < 50", "SNAPS: = TICK") are kept now. 73 new keys, 3787 in all.
- **"LABEL: VALUE" with an unknown value:** when the left side of ": " isn't a key by itself, the lookup now tries it with its colon ("PLAYER MODEL:"), so the label translates and the value stays.
- **F3's latency column** was sized for English row names, so translated names ran into the numbers; it's now as wide as the widest translated name.
- **CJK lines wrapped too early,** because a line could only break at a space. Chinese, Japanese and Korean text may now break between characters (never before `:` `,` `)` `.` `;`), so lines fill the width as English does.
- **Squeezed labels lost their word spaces** ("BOTSAUSGLEICHEN"): closing up a label tightened a space from both sides, about 30% of its width. A space now closes by 10% at most, and the letters take the rest.
- **Wrong words:** Russian VOTES was "voices" (now ГОЛОСОВАНИЕ), Turkish ADVANCED was "forward" (now GELİŞMİŞ), and the spectator FOV's MINE ("my own") was translated as the explosive in Portuguese, Russian, Turkish, Chinese and German (now possessive; French too).
- The CJK atlases were rebaked for the new characters: zh-CN 1632, ja 1323, ko 1094 glyphs.

## Voting (owner, 2026-11-01: "just one VOTE button, then you toggle on options")

- **The builder's GAMEMODE / MODIFIERS / MAP toggles moved onto the VOTE page itself**, with CHOOSE naming the first step: one click and one page fewer. The page keeps its height by pairing START/RESTART, RESTART/5 MORE MINUTES and RANKED/SHUFFLE TEAMS. BACK walks one step back (summary > map > modifiers > gamemode > VOTE).
- **New modifiers FIRE RATE (x0.25-4) and PROJECTILE SPEED (x0.25-3)** apply to every weapon through the custom rules' existing `weapon=` multipliers, so there is no protocol change; the server's `mods` whitelist gained `firerate` / `projspeed` (an older server answers "bad modifier"). Player size and ammo limits, also on q's list, don't exist as gameplay yet and were not invented.
- The UI test now reaches the builder's modifiers and summary pages (scoped builder pickers, one context per toggle combination); before, they were never tested.

## Acid and lava (owner, 2026-10-27: "it should destroy, so should lava, yes screen tint, and pulse and sizzle")

- **Projectiles that enter lava, a deadly liquid or acid are removed there**: no blast, hit or prop, and no explosion revealed to players who couldn't see it. The server's 1 ms step and the client's own copies use the same `Map::dissolveAlong` test, so nothing blows up or reports a hit past the surface; a vanished projectile at the surface plays a hiss.
- **In acid:** a green screen tint (0.12 alpha, pulsing +-0.04 while it hurts; steady with REDUCE FLASHING), the health bar pulses acid green, and the new SND_SIZZLE plays on contact and every 0.4 s. SIZZLE is generated in code like the other default sounds and added after LOCATOR, so recorded kits' ids are unchanged; override name SIZZLE.

## Netcode and server

- **NET-14: projectile ids never repeat while one is in flight.** `newProjId` skips 0 and any id a live projectile holds, so a long-lived sticky never shares its id after the u16 wrap (a wrong trail or missed explosion). Server only, no protocol change; one scan of 128 slots per shot.
- **COR-8: interpolation is clamped to kMaxInterpMs + 0.1 s behind the newest snapshot**, and the bracket search stops at a server clock reset; it could draw poses up to ~1 s old.
- **Visibility rays scale with the target's size** (a MOVE_BIG giant's head is twice as high), so a giant whose head sticks out over cover is sent. Same ray budget.
- **breakGlass's shard scratch list is a heap buffer that is never freed** instead of a thread_local object: a thread_local with a destructor crashed at thread exit under MinGW on hosted servers. Still no allocation per break.
- **install.sh keeps a testing install that is ahead of the checkout**, runs the testing updater during the install, and sets `StartLimitIntervalSec=0` on the server units. Re-cloning had put quark-testing on the older production build, which couldn't load the newer maps; systemd then gave up before the updater rebuilt (~10 min).

## Tests and tools

- Timing tests notice every host pause (ratePump's sleeps and the loop's own off-CPU time), allow up to 5 attempts and only re-run attempts the host paused, so a real regression still fails; the grapple latch prediction check is a timing test now.
- A test server that fails to start always prints a FAIL line (`START_SERVER`).
- release_check.sh reads its alone groups from `quark_tests groups alone` instead of a copy.
- A CRLF demo versions.txt reads correctly (test added); the material table check allows the ball wall.
- ZONESTATS builds link on MinGW (no thread_local init function); pmove's internal CmdCtx no longer clashes with the map tool's; unused constants removed; SIGNING.md no longer points at a deleted key file.
