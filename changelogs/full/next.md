# Next (full, testing builds since 1.19.1)

Every change on `testing` since production 1.19.1, with what changed and why. The release PR renames this file to `changelogs/full/<VERSION>.md`.

## Performance

- **PRIORITIZE GAME** (Settings > VIDEO > LATENCY, Windows, `prioritize=1` by default). Why: the owner's F3 with YouTube and Electron apps open (2026-10-28, desktop, Hamlet, 2560x1440, 240 Hz): about 400 us a frame, of which the game's own CPU work was about 100 us (build 42, draw calls 50, swap 5) and its GPU work 102 us, but it waited 294 us for the GPU (LOW LATENCY ON): about 190 us of every frame went to other programs' GPU work queued ahead of the game's. What: while the window has focus the process's GPU scheduling class is raised to HIGH with gdi32's `D3DKMTSetProcessSchedulingPriorityClass` (ABOVE_NORMAL if Windows refuses HIGH; REALTIME, which needs administrator, is never asked for), so Windows' GPU scheduler runs the game's work ahead of a NORMAL process's; and the CPU priority class goes to ABOVE_NORMAL (not HIGH: at thousands of frames a second a HIGH process can starve input, voice chat and the launcher; the hosted server and audio threads are raised on their own already). Losing focus (alt-tab, minimise) or turning it off puts both back to NORMAL. The call is skipped when nothing changes, so focus changes cost nothing. Trade-off: other programs' drawing (a video, an overlay) can stutter while the game is in front; the setting turns it off. Works best with Windows' "Hardware-accelerated GPU scheduling" on. Not in the UI test (it would change the test machine's priorities). Linux and macOS: no setting, nothing changes.

## Weapons

All four are the owner's numbers (2026-10-28, right after 1.19.1), in `src/game/gamedefs.h`'s standard table; custom modes scale them as before (`weaponset.cpp` reads the same constants). No protocol change: like earlier tuning (rifle 4.95 -> 5.0, sniper 40), only the numbers moved, and testing servers and games update together.

- **Rifle +1 a shot**: body 5 -> 6, `kRifleHeadDamage` 7.5 -> 8.5. Refire unchanged (0.1515 s). Time to kill from 100 hp: 17 body shots, 2.42 s (was 20, 2.88 s); 12 headshots, 1.67 s (was 14, 1.97 s). DPS against the laser's 40: 0.99 body (was 0.82), 1.40 head (was 1.24): the rifle now matches the laser inside its 24 m on body shots.
- **Bubble fire rate +20%**: refire 1/5 -> 1/6 s, so 60 damage a second (was 50); 10 pops kill in 1.5 s (was 1.8 s).
- **Bubble stream up to 25**: `kBubbleStreamMax` 15 -> 25 (at 30 m or more; `kBubbleStreamMin` stays 5 within 5 m, the count still grows evenly in between; reload 1.8 s unchanged). At full range a stream is 250 damage if every bubble lands. Trade-off: a stream is 10 more live projectiles; the server holds 128 at once, so in a crowded fight a later stream can come out shorter.
- **Knockback +33%**: `kShotgunKnock` 0.30 -> 0.40 m/s per pellet's worth of damage (a full 20 pellet blast 6 -> 8 m/s); `kCrossbowKnock` 16 -> 21.28 m/s along the bolt.
