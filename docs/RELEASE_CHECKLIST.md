# Production release checklist (for the owner)

Do this for **every** update, after it has been pushed to the `testing` branch and published as a testing prerelease, and **before** it goes to production (the `testing` → default-branch PR and the `v<VERSION>` tag). Claude never pushes to production until you have been through this list and said so.

- **When:** CI has published the prerelease `test-<VERSION>.testing.<N>` (before 1.19.0: `test-<VERSION>-test.<N>`). The launcher's TESTING channel and Discord's #testing-updates both show it.
- **Time:** about 60–90 minutes for the full list. For a small patch (one fix, no netcode, map or server changes), do sections 0–2, the parts of 3–16 the changelog touches, and 17.
- **How to record it:** copy the sign-off table at the bottom into the PR description (or a note), and tick each section as you go. Write down anything odd, even if it seems minor, and give it to Claude before production.
- **If something fails:** stop. Tell Claude what failed and how to reproduce it. The fix goes to `testing` as a new build (`.testing.<N+1>`), and you redo the sections it touches. Never tag production on a build with a known failure in sections 0, 1, 2, 11, 12 or 13.

---

## 0. Before you start: the build is complete

| ✓ | Check |
|---|---|
| ☐ | GitHub → Actions: the run for the `testing` push is green: meta (VERSION, changelogs, secrets check), the Windows, Linux and macOS builds, the testing prerelease. CI runs **no** gameplay or UI tests (owner, 2026-10-24): those are the local runs below and this checklist. |
| ☐ | This is the **final test build** (the one you mean to ship) and its **sanitizers** run is green. Mark it either way: put `[final]` in the subject line (first line) of the push's head commit (for a merged PR, in the PR's title; a mention further down a message doesn't count), or after the push run Actions → **build-and-release** → **Run workflow** on `testing` with **final** ticked (that runs only the sanitizers, on the branch's current commit; nothing is built or published). The sanitizers fail only on a memory or undefined-behaviour error (ASan / UBSan / the fuzzers), never on a test's own result. No other build runs them (work branches and pull requests included): only a human marks a final test, never an agent. |
| ☐ | Claude's local test runs passed **on this commit** (ask for the results): `tools/release_check.sh` (every `build/quark_tests` group, plus every map hosted headless; exit 0) and the UI test `build/quark --uitest` under Xvfb ([UI_TEST.md](UI_TEST.md); exit 0). CI no longer runs them, so nothing else checks them before production. |
| ☐ | GitHub → Releases: the prerelease `test-<VERSION>.testing.<N>` exists. It is marked **Pre-release**, not "Latest". |
| ☐ | Its assets: `quark-windows.qpak`, `quark-linux.qpak` (and `quark-macos.qpak` once the macOS build finished), each with a `.sig`; `quark-release.txt` + `.sig`; `SHA256SUMS` + `.sig`. |
| ☐ | The release PR bumped `VERSION` to the release this build becomes and renamed `changelogs/next.md` and `changelogs/full/next.md` to `changelogs/<VERSION>.md` (the short notes) and `changelogs/full/<VERSION>.md` (every change, and why). Both exist and no `next.md` is left; a production tag fails otherwise. (Testing builds between releases keep `VERSION` and only add to `next.md`.) |
| ☐ | Discord #testing-updates announced the build (and #updates did **not**). |
| ☐ | Claude's notes for this update list nothing still open (ask: "anything unfinished or untested in this build?"). |

## 1. Install and update (launcher)

Do this on Windows; also on Linux / macOS if you have one.

| ✓ | Check |
|---|---|
| ☐ | Start from the **current production** install (with your normal settings, maps, demos and HUDs). |
| ☐ | **First production from the `launcher` branch** (1.19.0): every package's `quark_launcher` is now built from the `launcher` branch (`tools/launcher_build.sh`), not the game branch. After the update the launcher still starts, shows its version, and updates the game; `quark_launcher` and its `.sig` are in each platform's qpak. |
| ☐ | Launcher → CHANNEL → TESTING: the new build is listed and installs without any signature warning. |
| ☐ | After the update, your settings, keybinds, HUD layouts, ranked key, your own maps and your demos are all still there. |
| ☐ | Files the old version shipped and the new one doesn't are gone from the install folder; nothing you made was deleted. |
| ☐ | If this update changed the demo format (1.19.0 does: protocol 42 → 44 from 1.17.0): old demos were converted (originals in `demos/old/<version>/`), and an old demo plays. A demo that couldn't convert says "recorded with version X" instead of failing. |
| ☐ | If this update changed the map format (1.19.0 doesn't: still version 2, as in 1.17.0, so skip this row): your own maps were converted (originals in `maps/old/v<N>/`, `maps/old/convert.log`) and still open in the editor. |
| ☐ | Switch the launcher back to PRODUCTION: it offers the production version and installs it cleanly (so players can always go back), then return to TESTING. |
| ☐ | A fresh install (empty folder, new user) works too: first start makes the user folder, no errors. |

## 2. First start and performance

| ✓ | Check |
|---|---|
| ☐ | The game starts to the main menu without errors; the version in the corner is the testing build's. |
| ☐ | **F3 overlay** in three busy places: Midtown (multi point, 8+ bots), Hightown (hide and seek), Lodestone (FFA, 8 bots). Note avg / **1% low / 0.1% low**. |
| ☐ | Compare with the same three on the current production build (same settings, same machine). **Lows must not be worse by more than ~5%.** Performance comes first: a regression is a blocker. |
| ☐ | **F3's mouse-to-photon graph**: every row has a sensible time; with NO VSYNC, LATENT SYNC and VSYNC the total is stable (no row growing without end). **F4** shows the number alone; F3 and F4 give about the same number. |
| ☐ | **PRESENT: LATENT SYNC** (Windows, the default for a new config) in exclusive fullscreen: no tear line in play; fps sits at the refresh rate; F3 shows LATENT SYNC rows. On a PC (or a cap) below the refresh rate it tears instead of stuttering. TEAR LINE OFFSET appears with it. Linux / macOS: no LATENT SYNC option, NO VSYNC by default. |
| ☐ | **MAX FPS** slider: drag, type a number, 0 = UNLIMITED; right-click sets your screen's refresh rate; the cap holds in F3. **GPU MAX CLOCKS** (NVIDIA): turning it on says it applies from the next start (or asks for administrator once). |
| ☐ | Late mouse read: fast flicks show no lag or wobble on the weapon model, and nothing pops in late at the screen's edges. Alt-tabbed, moving the mouse doesn't turn you; the cursor is free on the desktop and held again when you're back. |
| ☐ | Loading a big map (Midtown) the first time: the window never says "Not Responding"; joining a remote server's big map doesn't time out. |
| ☐ | **Shipped light bakes**: with an empty user cache (or a fresh install), joining Midtown / Lodestone loads in well under a second with no bake bar; the install has `maps/lightcache/*.qlbz` (21 files, about 12 MB). |
| ☐ | **Alt-tab back with a click**: the click that brings the window back doesn't fire; the next click does. ALT TOGGLE on: a tap zooms and stays zoomed until the next tap. |
| ☐ | **Sound**: an explosion behind you moves to the other ear as you turn while it plays; EXCLUSIVE AUDIO on / off still clean; the startup log's audio line names the engine period. |
| ☐ | No stutter when bots spawn, when a round restarts, when the scoreboard opens, or when a demo is recording. |
| ☐ | Alt+Enter switches borderless ↔ windowed (and doesn't also send Enter to chat). |

## 3. Menus (every screen)

The automated UI test clicks every button (`quark --uitest`, run locally on this commit: section 0). Here you check that things look right and make sense to a person.

| ✓ | Check |
|---|---|
| ☐ | Main menu: JOIN GAME, HOST GAME, RANKED, MAP EDITOR, SETTINGS, INFO, DEMOS, QUIT each open the right screen, and Esc / BACK returns. |
| ☐ | **The menu pages' layout** (main, join, host, vs bots, the map / mode / ruleset / ladder pickers, custom mode, info, leaderboard, demos, connecting): one 360 px column (or one 960 px panel for the wide pages) under the title; on every page BACK is bottom-left and its main action (START, SAVE, REFRESH LIST, OPEN FOLDER, PLAY TIME TRIAL) bottom-right, inside the panel; nothing overlaps or runs off the bottom at 1280×720, 1920×1080 and 1024×768 (4:3). QUIT is on its own row at the bottom of the main menu. |
| ☐ | HOST: TEAMS (1V1 / 2V2) is a row of its own under MODE in duel only; ADVANCED > opens UDP PORT, AUTO PORT FORWARD, SERVER LOG and BOT BRAIN (Esc / BACK: back to HOST); a changed port is the one the server uses. Ranked shows its two notes under MODE. |
| ☐ | Pickers (HOST / VS BOTS: MAP, MODE, 1V1 rulesets, RANKED ladders): cards with their description under them, the current one framed. In a mode most maps aren't built for (e.g. VS), the maps built for it say FOR <MODE> in the accent colour, the others NOT FOR THIS MODE under one orange legend line, and any can still be picked. |
| ☐ | Custom mode (HOST → CUSTOM MODE NEW / EDIT): five tabs (GENERAL, WEAPONS, MAPS, MOVEMENT, DASH + GRAPPLE) in one panel, BACK / DELETE / SAVE in the same place on each. Sliders show the value in the mode with its unit (GRAVITY 22.0 M/S2, DASH SPEED 24.0 M/S...): drag one, type a value, right-click it (back to standard), SAVE, EDIT again: the values came back; a game hosted with it plays with them. A hitscan weapon's SPEED says HITSCAN (greyed). |
| ☐ | INFO: the tabs, the current one framed; a long page scrolls with the wheel and says LINES x-y OF n; no line runs out of the column. LEADERBOARD: four columns, top 15 each; MY RACE STATS (BACK returns to the board). DEMOS: 8 rows, the wheel scrolls; RECORDING RATE. |
| ☐ | The **QUARK title** looks like the app icon (the Q split cyan / red along a slanted line with a black seam, every letter on a black outline) and is crisp at 1280×720, 1920×1080 and your own resolution. It's in your team colours: the Q's left and UARK in TEAM COLOR, the Q's right in ENEMY COLOR (cyan / red by default; try another TEAM COLORS preset). Pick another font (SETTINGS → PLAYER → FONT): the title is in that font, its Q still split with the seam. The app icon keeps its cyan / red. The settings pages show no title. |
| ☐ | INFO: every tab reads correctly and matches this version (weapons, modes, controls). |
| ☐ | Text fits everywhere: nothing overlaps or runs off a button at your normal resolution and at 1280×720. |
| ☐ | Pause menu in a game: SETTINGS, record / stop demo, EDIT THE MAP TOGETHER (not in ranked), DISCONNECT / STOP HOSTING (host with bots, play 30 s, STOP HOSTING: back to the main menu, the game must not close), QUIT. |
| ☐ | **The in-game pages' layout** (pause menu, VOTE, QUEUE, the CHANGE MAP / MODE pickers, TOURNEY MAPS / RULES, LADDER): one opaque panel between the lines under the score bar and the weapon bar, nothing showing through it; the pause menu has a HOST section when you host (FORCE START / RELOAD MAP, CHANGE MAP / MODE, EDIT THE MAP TOGETHER) and STOP HOSTING (or DISCONNECT, or STOP DEMO) beside QUIT on the last row; VOTE has a BOTS section. The lines under the score bar (objective, HOSTING ON UDP PORT, reachability with CHECK AGAIN at the end of its line, a running vote, ranked's notes) are one list: none drawn over another. The pickers are the menus' cards (one legend line, CALL VOTE bottom-right in the tourney lists). At 1280×720, 1920×1080 and 1024×768. |
| ☐ | HUD: TAB's scoreboard starts under those lines (TOTAL K / D only when someone has a total; a bot says -). F3's box stays left of the score bar (4 lines). Hide and seek: the seeker's "FACE THE WALL" line is above the countdown; the round's message sits over the tiebreak and hides while TAB is held; the tiebreak panel is under the scoreboard while TAB is held. The cooldown bars' captions (WEAPON / DASH / HOOK / SPEED) are readable. |
| ☐ | Console (`~`): monospaced; Tab completes commands and their arguments; it does not open the scoreboard. |

## 4. Settings

For each one: change it, check the effect in a game, quit, restart, check it was kept.

Settings is one panel with six tabs (PLAYER, GAMEPLAY, CONTROLS, VIDEO + AUDIO, CROSSHAIR, HUD), the same from the main
menu and from the pause menu: BACK (bottom-left) and Esc leave it from any tab; in a game the HUD is hidden while it's open (except on the HUD tab, which previews it).

| ✓ | Check |
|---|---|
| ☐ | The shell: every tab opens from the tab row, BACK never moves, nothing overlaps at 1280×720, 1920×1080 and 1024×768 (4:3); in a game nothing of the HUD shows through (but on the HUD tab). |
| ☐ | **Colour pickers** (TEAM / ENEMY COLOR, CUSTOM NAME COLOR, MAP COLOR, CROSSHAIR / HIT FLASH COLOR, the HUD element's colour): click one: drag in the square and the hue bar, type a hex value (Enter), click outside or Esc to close; right-click the swatch: its default. TEAM COLOR is also the menu accent. |
| ☐ | Video (VIDEO + AUDIO): fullscreen / borderless / windowed, resolution, vsync, FPS limit; field of view (GAMEPLAY). |
| ☐ | Outline thickness: **world** and **player** separately (0 = off). |
| ☐ | Crosshair: style, size and colour; the **hit-flash colour** setting, including **off**; the default is unchanged from the last release. |
| ☐ | Audio: master, effects, announcer, music volumes; the announcer pack. |
| ☐ | **EFFECTS VOLUME / ANNOUNCER VOLUME** (VIDEO + AUDIO → SOUND, default 1.00, on top of VOLUME): EFFECTS at 0 silences every sound effect (weapons, steps, the hitsound) but not the announcer; ANNOUNCER at 0 silences the call-outs (3-2-1-GO, ROUND WON...) but not the effects; a sound already playing follows the slider. Right-click: back to 1.00; restart: kept. |
| ☐ | **DEVICES** (VIDEO + AUDIO → DEVICES, bottom right): AUDIO OUTPUT lists SYSTEM DEFAULT and every output by name; picking one moves the sound there at once (no cut-outs, no added delay; also with EXCLUSIVE AUDIO on), restart: kept. Unplug the picked device and restart: a notice says it's gone and the default plays; plug it back, REFRESH: it plays there again. AUDIO INPUT lists the microphones (stored only: the hint says nothing records yet). MONITOR (two screens): pick the second, then BORDERLESS and EXCLUSIVE FULLSCREEN open on it; PRIMARY MONITOR puts it back. REFRESH RATE: another rate in EXCLUSIVE FULLSCREEN switches the monitor to it; DESKTOP goes back. BACK / Esc return to VIDEO + AUDIO. |
| ☐ | **QUIPS / QUIPS VOLUME** (VIDEO + AUDIO → SOUND; QUIPS on by default, on HITSOUND's row; QUIPS VOLUME 1.00 under ANNOUNCER VOLUME): press G (QUIP) in a game: a hype line in your VOICE (HEADSHOT, GODLIKE, SPREE...), never a call-out (no 3-2-1, GO, BOMB PLANTED, ROUND WON, ONE MINUTE...); another player's quip comes from where they stand. ANNOUNCER: OFF still plays quips; QUIPS: OFF silences them (yours and others') while the announcer still calls; QUIPS VOLUME at 0 silences them, ANNOUNCER / EFFECTS VOLUME leave them alone. Turning QUIPS on plays one. Right-click QUIPS VOLUME: back to 1.00; restart: both kept. Windows: with EXCLUSIVE AUDIO in SOUND, VOICE still clears DEVICES. |
| ☐ | Quip overrides: put `LAUGH.wav` (any short sound) in `%APPDATA%\quark\overrides\voices\male\quips\` and restart: with VOICE MALE every quip is that sound (it replaces MALE's whole set); FEMALE still says its hype lines. Remove it after. The sound editor's Voices tab lists each voice's quips in a QUIPS group and exports them to `voices/<voice>/quips/<NAME>.wav`. |
| ☐ | Announcer: every voice (Settings > VOICE) has 27 lines, none silent: the Asset Studio / `/sounds/` Voices list no AIRBORNE, TEN_SECONDS or FIVE_SECONDS (removed: never played, never recorded); the countdown, kill calls and "one minute" / "thirty seconds" still say the right words in each voice. |
| ☐ | Audio (Windows): sound is clean with EXCLUSIVE AUDIO off (shared, 10 ms) and on (3 ms on the owner's PC; other programs go silent, back when it's turned off); unplugging or switching the output device mid-game moves the sound to the new default. |
| ☐ | Sound levels: put a quiet `ROCKET.wav` in `%APPDATA%\quark\overrides\sounds\default\` and restart: the rocket is as loud as before (matched). Add `sounds/default/ROCKET` to `%APPDATA%\quark\overrides\sound-levels.txt` and restart: it is now quiet (its own level). Remove both after. The sound editor (`tools\sound_editor\build.cmd`) builds and its Export to game writes these files. |
| ☐ | Model overrides: open the model editor (Quarkology installs it by itself: its Model Editor tab, or `Quark\model-editor\model-editor.html`), recolour `models/player/STANDARD` (e.g. bright green), Export to game into `%APPDATA%\quark\overrides` and restart. A casual game (vs bots) shows it, with outlines and crouching like the default; a **ranked** match (and a time trial) shows the default STANDARD instead. The payload cart and checkpoint gates (railyard), the bomb on a carrier's back and on the ground (depot), and the arms (other players' gun arms; the held-weapon arms in hide and seek) look as in the last release. Remove the file after. |
| ☐ | Controls: rebind every action you use (GENERAL + INTERFACE KEYS and MOVE + WEAPON KEYS); **right-click** a slot unbinds it (or click it and press Del). Bind **chat to a letter key** (e.g. Y): pressing it opens chat **without** typing a "y". Bind **Toggle chat**: incoming chat hides / shows with a short message. **Disable chat** (GAMEPLAY) stays in sync with the toggle. |
| ☐ | Interpolation setting (up to 200 ms): shots still hit what you see. |
| ☐ | HUD tab: the ELEMENT dropdown lists WEAPON MODEL and GRAPPLE MODEL first and every HUD element; each one's SHOW / offsets / size / depth and PITCH / YAW / ROLL (the two models: tilts it about its pivot; your shots / laser / knife slash leave the tilted gun's muzzle, the rope the tilted launcher's barrel; hiding one model leaves the other) / colour work, RESET ELEMENT and RESET WHOLE HUD put the defaults back. **Live preview**: on the HUD tab (in a game and from the main menu) the HUD shows under a see-through panel (in game with live values; from the menu health 100, full bars, 12 m/s, FFA's weapon list, the key overlay when VERBOSE INFO is on); the selected element has a thin pulsing accent outline (dashed and dim when SHOW is off; none for WEAPON MODEL / GRAPPLE MODEL); dragging its X / Y OFFSET or SIZE, or changing its colour, draws it over the panel at full strength as it moves, for 1.5 s after the last change; the panel's text stays readable; other tabs look as before (opaque, no HUD). |
| ☐ | **First-person models in the corners**: by default the gun sits low in the bottom-right corner and the grapple launcher in the bottom-left, partly cut off by the screen edge; at 16:9, 4:3 and ultrawide (VIDEO resolution) they sit at the same spot on the screen and the same size, and changing FOV or zooming the rifle doesn't grow or shrink them; the sniper's barrel and the knife stay on screen. |
| ☐ | HUD editor (HUD → WIDGET EDITOR): drag, add, delete, import / export a layout; the weapon list shows only the weapons the mode has. |
| ☐ | **HUD tab footer**: on the HUD tab BACK and RESET WHOLE HUD sit above the weapon list and the health box (not over them) at 1280×720, 1920×1080 and 1024×768; the ELEMENT list also has SCORE BOX, KILL FEED and CHAT, and each moves / sizes / recolours in game. |
| ☐ | **EDIT ON SCREEN** (HUD → EDIT ON SCREEN, in a game and from the menu): every element is drawn where it goes (score box, kill feed and chat with sample lines), outlined, hidden ones dashed. Click one: selected (accent outline, corner squares, the panel shows it). Drag: it moves and snaps to the screen's edges / centre and the other elements with a pink guide line; hold Alt: no snapping. Drag a corner, the wheel or + / -: it resizes. Arrows nudge (Shift: 10), Tab next element, Delete hides / shows. The panel moves to the other side when the selected element is under it. Change BACKGROUND, BG OPACITY, BORDER, CORNERS, TEXT SIZE, PAD, GAP, TEXT ALIGN, OPACITY: the element changes at once; Ctrl+Z / UNDO steps back one change (a whole drag is one step), Ctrl+Y / REDO forward; RESET STYLE, RESET ELEMENT, RESET ALL, CLEAR THEME put the defaults back. DONE or Esc goes back to the HUD tab. Restart: the layout (quark.cfg) and the style (`theme.cfg` in the data folder) are kept. F3 lows with the HUD as before. |
| ☐ | **theme.cfg by hand**: write `hud { radius: 6; } hud.health { background: #000 50%; border: 2 team; }` into `theme.cfg` while a game runs: the health box gets a rounded dark box with a team-colour border within a second; a typo in one property skips only that one (a notice names the line); delete the file: the HUD is back to the default look. In a **ranked** match an `opacity: 0%` element still shows at 30%. |
| ☐ | DISCORD CODE: see section 13. |
| ☐ | **Font** (SETTINGS → PLAYER → FONT): the game is in **Rubik Bold** everywhere (menus, HUD, chat, console still monospaced, scoreboard, editor). Pick WORK SANS, QUARK CLASSIC (square pixels, black outline, the old letter widths) and each game-styled entry: everything redraws in it at once with no stutter afterwards (F3 lows unchanged); restart: it's kept. Put a .ttf in `mods/fonts` (OPEN FOLDER), REFRESH: it's listed and works. Delete that file and restart: a notice says it fell back to Rubik. With Steam's TF2 / Quake or Windows' Verdana present, those entries say "USING YOUR ..." and look like the game. |
| ☐ | **Name colour** (SETTINGS → PLAYER → LOOK): turn it on and pick a colour (picking one turns it on; right-click the swatch: off); a second player sees your name in it on the scoreboard, in chat and in the kill feed; team name tags stay the team colour; a very dark colour shows lighter. Off again: your name goes back to the default. |
| ☐ | **Health bars**: turning them off stays off across restarts and updates (from 1.17.0 they keep whatever you had set). |
| ☐ | Every number slider: click it and **type a value**; **right-click** resets it to its default (check the custom-mode MOVEMENT page too). Damage-number size, damage flash (off by default; on / intensity in GAMEPLAY), sniper and rifle zoom FOV, and the fullbright map colour (VIDEO + AUDIO → FLAT MAP) all work. |

## 5. Weapons and movement

Use HOST GAME → FFA (bots off or beginner) and the changelog's numbers.

| ✓ | Check |
|---|---|
| ☐ | Each weapon fires, sounds right, shows its own projectile model, and does the damage the changelog / INFO says (rifle 5.0 body / 7.5 head; crossbow 50 body / 75 head; shotgun 2.6 a head pellet; laser 24 m range; sniper 40 body / 50 head; rockets fly straight with no drop, the alt-fire lob drops at 13.5 m/s²). |
| ☐ | Alt-fires: rifle scope, shotgun bounce, rocket lob, knife throw, bubble stream. |
| ☐ | Knife throw: the knife is **disabled for 4 s** (greyed with a timer), and nothing can be used for the knife's normal swing cooldown. |
| ☐ | Syringe: self-use heals **10 HP** with a **1 s** cooldown; a throw heals a teammate, disables the syringe for 4 s, with a 1 s cooldown. It only exists in modes where you respawn. |
| ☐ | Movement: jump, crouch, bunny-hop, air-strafe, dash, grapple; run speed 9.5 feels right; ladders (6.75 up) and staircases climb smoothly; a standing jump can be steered; at speed A/D strafing turns better than W alone; A/D strafe-jumping builds speed noticeably faster than 1.17.0, the current production build (about 20.7 m/s after 1 s of good strafing from a run), W-only air turning is a little slower. |
| ☐ | **Dash charges, cooldown + recharge**: two dashes, usable **0.6 s apart** (from the END of the previous dash): from full in the air, both go within a second (about 0.72 s start to start), then none until you land. A spent charge **recharges for 1.2 s** from the end of the dash that spent it and only then comes back the usual way (landing refills both, a bunny hop one); on the floor, pressing dash repeatedly gives a dash about every 0.72 s. Pressing dash during a dash, or before the 0.6 s, does nothing; a dash jump keeps both from full (it gives back the one it spent at once; a charge still recharging from an earlier dash stays out); a super dash and a wall bounce leave one; a super dash (hyper) leaves the floor like the dash bounced: the further down you look, the more of its speed goes up, at most 90% (1.18.31: from a run, about 0.45 m at 20 degrees down, 1.9 m at 45, 3.9 m straight down, with less speed kept level the steeper it is). **Dash aim** (1.18.31): looking straight up a dash climbs at only 40% of its speed (a standing jump + dash up at the jump's top peaks about 2.8 m), looking straight down it dives at 90%; super and hyper still play their own sounds. **Speeds** (F3 or the speed meter): a dash jump leaves at 24 m/s; a wall bounce at **16**; a super dash at your speed before it **+ 5** (out of a run 14.5, from standing 11, never over 36). **Quick dash** (duel QUICK DASH / DASHING BLADES, BLADES arena): 2 charges, **0.2 s apart, 0.4 s recharge**. The HUD shows a pip per charge: a charge you have but can't dash yet (the dash running, or the 0.6 s) is a whole **light grey** pip with no fill, turning wholly **blue** the moment you can dash; a spent charge is empty (still empty after its 1.2 s while you're in the air, until you land). One dash must never look like it emptied both. Custom mode MOVEMENT page: DASH COOLDOWN slider (standard 0.6 s; it scales the 1.2 s recharge too; type a value, right-click resets). **1.19.0 is protocol 44** (1.17.0 is 42): it can't join 1.17.0 production servers or host for 1.17.0 clients (the version message says so); old demos convert at startup. |
| ☐ | Hitmarkers, red headshot numbers, kill feed, damage direction. Hit boxes: the body up to the bottom of the head, plus the head. A rifle shot just past the side of a standing or a crouched player's head (above the shoulders) misses (no hitmarker); just under the head's bottom it's a white body hit. |
| ☐ | **Feel batch, combat** (1.18.21): **Rocket at your knees** costs about 10 HP, at chest height about 34; a rocket jump at the feet is unchanged. **Knife throw** (and syringe throw): you go back to the weapon you held, not the rocket. Sniper / crossbow / rocket: a click just before it's ready fires when ready. 20 rifle body shots kill. |
| ☐ | **Feel batch, movement** (1.18.21): **jump tap buffer**: tap jump just before landing (not held), you jump on landing every time. **Held dash** (T2): hold dash on the floor: a dash every ~0.72 s; in the air two, then none until you land. Short taps of jump / dash / grapple at low fps (cap 30) are never lost. **Dash jump** off a ground dash aimed slightly up works, and while holding jump to bunny hop. **Ladders**: W climbs, S climbs down, A / D move sideways only. **Stairs**: running up steps, the view rises smoothly, no jolts. |
| ☐ | **Feel batch, sound and HUD** (1.18.21): **hurt sound under the laser**: a calm repeat (not a buzz), footsteps still audible. **Enemy jump / dash / land sounds** heard in 3D, a hard landing louder. **Muffled distant gunfire**: a bot firing out of sight 20-60 m away is a quiet, low shot from about its spot. **Damage arc**: a red arc round the crosshair points at whoever hit you; **enemy rifle shots draw tracers**. **Kill marker**: a red X on a kill (also scoped), the victim drops with a death sound and a red burst. **Death screen**: KILLED BY name [WEAPON] - their HP (no HP in ranked) and RESPAWN IN counting down (none in round modes). **Hit sparks**: rifle / shotgun / laser hits show a red spark at once (online: before the hit sound). **Your rockets / bolts / knives** appear from your weapon the moment you fire, with no pop-in a few metres out; check also at a real ping (**open for 1.19.0**: only tested locally so far). **Behind you**: a bot firing behind you sounds quieter and duller than in front. |
| ☐ | **Sparks, muzzle flash, client-side explosions** (next): shooting a wall leaves a small yellow spark; a bot's shots spark on walls and on you-adjacent players; every gun (yours and theirs) flashes at the muzzle; GAMEPLAY > SHOTS > IMPACT SPARKS / MUZZLE FLASH turn each off. Online at a real ping: your rocket explodes the moment it touches the wall (no delay), a sticky detonate goes off at once, and you never see two explosions for one rocket; a bot only dies when the server says so. |
| ☐ | **Weapon model opacity + screenshots** (next): GAMEPLAY > VIEW > WEAPON MODEL OPACITY at 0.4 makes the gun and grapple launcher see-through as one solid (no inner boxes showing), at 0 they're gone, at 1 as before. F12 twice, quit, start again, F12: three files in the data folder's `screenshots/`, none overwritten; the notice names each. |
| ☐ | **Mode calls** (1.18.23): bomb freeze shows ATTACK / DEFEND for your side; halftime says SIDES SWITCHED; dropping the bomb calls BOMB DROPPED (attackers) / THEY DROPPED THE BOMB (defenders); payload checkpoints play POINT TAKEN / LOST; FFA calls only your own lead (YOU TOOK THE LEAD) and the line under the mode shows your place (2ND OF 8). |
| ☐ | **Grapple launcher arm**: a launcher on the left arm, in first person (low left; its own HUD element GRAPPLE MODEL; hidden when scoped and in a mode with the grapple off) and on other players (`--models --hook` lineup); the claw sits in its barrel, flies out on the rope (the rope leaves the barrel, first and third person) and sits at the hook point; while hooked your arm comes up a little toward the hook. |
| ☐ | **Grapple against a wall** (1.18.32): stand against a wall, look at it and hold grapple: the grapple sound plays once, then at most once a second while you hold it (it used to play every frame, very loud). Grappling something far away still works as before. |
| ☐ | **Weapon switch is instant**: the weapon, HUD and zoom change on the key press even with a high ping; holding fire on the laser while switching to it keeps its beam and sound; A, B, A quickly ends on A. |
| ☐ | **Round clock** under the mode title in FFA, tag, hide, bomb (BOMB m:ss after the plant), payload, both-ways payload, multi point and race; KOTH shows FIRST TO N, VS / duel / 1v1 the drain timer. |
| ☐ | Thrown knives, crossbow bolts and thrown syringes stay stuck where they land: in walls until the match ends (gone after a restart or match end), in a player until that player dies. With lots of them about, F3's 1% and 0.1% lows don't drop. Super and hyper play their own sounds; the locator pings. |

## 6. Game modes

Host each mode on **its own map** with bots (and a second human if you can). For every mode: the round starts, the objective works, scores add up, the round and match end, and the HUD and scoreboard are right.

| ✓ | Mode | Map | Mode-specific checks |
|---|---|---|---|
| ☐ | VS | Bastion / Orbital | Rounds, no respawn, health drains in a stalemate. |
| ☐ | Bomb | Depot | Plant, defuse, a dropped bomb falls to the floor and returns to an attacker after 15 s, overtime. Let a planted bomb go off: a deep, long boom from the bomb (also from across the map). **Bomb vs bots on Depot and Lodestone** (1.18.21): defender bots split across the sites; Depot attackers leave spawn and get kills (no sticking on the gatehouse wall). |
| ☐ | Race | Viaduct | Checkpoints, finish time, the time trial records a best time. Practice (warmup, before vote start): each checkpoint dings once as you enter it and the finish plays FINISH. |
| ☐ | King of the hill | Summit / Downtown | Capture, a contested hill freezes the clock. |
| ☐ | Payload | Railyard | The cart moves only with attackers on it, checkpoints add time, overtime. Round 1 ends with "TEAM A pushed N m - TEAM B must beat it" (no ROUND WON); round 2 ends **as soon as** the attackers pass that mark. A level push goes to the side there sooner ("there sooner than its 3:40"), no tiebreaker rounds. Past the last checkpoint, attackers respawn in 3 s (death screen says RESPAWN IN 3; **open for 1.19.0**: bots never reached the last leg in testing, so check it with people). |
| ☐ | Neutral payload | Canal | Both teams push. |
| ☐ | FFA | Harbor / Gantry | Frags on the scoreboard, overtime tiebreak. Respawns vary (not always the same farthest spawn) and never in an enemy's sight. |
| ☐ | Duel | Clockwork | Size modifiers (giant / tiny) change the collision box. Play vs bots DUEL: the duel team size per side (not 2v3). The break between rounds is 2.5 s. |
| ☐ | 1v1 | Temple, Furnace, Dojo | Rulesets (vampire, instagib, ...), first to 5; Dojo's 5 s hold-fire start. Play vs bots 1V1 is exactly one bot; the round end is 2.5 s (was 4) and the calls use names, not TEAM A. |
| ☐ | Tourney | Colosseum | Bracket, seeding, byes, a drop isn't an instant walkover. |
| ☐ | Hide and seek | Hamlet / Hightown | Everyone seeks once. A hider still unfound at round end scores 1 and the seeker 1 per hider found. Only the seeker holds the knife and the **locator** (key 0: one random hider shown for 1 s, every 5 s). Hiders are never shown the seeker's position, but every hider (and spectator) hears each locator ping in 3D from where the seeker stands, in the seeker's sound pack, even out of sight; the seeker hears their own ping once, flat (2D). A tie for first after the last round goes to whoever **stayed hidden longest** (the message says so); only a dead level tie → **rock paper scissors** (keys 1 / 2 / 3). A seeker who jumps off the map is **back after 2 s**, still seeker. |
| ☐ | Tag | Foundry | Only the tagger holds the knife + locator; runners always see the tagger (marker through walls / edge arrow) and hear the locator's ping from the tagger (3D). Leaving or spectating eliminates you; `addbot` mid-match waits for the next match. A tag is heard: tagged, a stab and a sting; tagging, a heavy hit sound (HITSOUND on); someone else's tag, a hit sound from the new tagger. |
| ☐ | Multi point | Midtown / Lodestone | Points A–E in order, staged spawns move forward, overtime. Round 2 ends once the result is settled; a level attack goes to the side there sooner, no tiebreaker rounds. |
| ☐ | Custom mode | any | Make one in the custom-mode editor, host it, the rules apply. |
| ☐ | Custom mode with movement equations | any | Save a `.qmode` (physics editor, or by hand in `custom_modes\`) with `expr.friction=curve(p.hspeed, 2, 9.5, 2, 1)` and `expr.gravity=p.air > 0.15 ? 2 : 1`: hosted, you stop sooner than standard (about 0.8 m from a run, against 1.2) and a standing jump peaks lower (about 1.04 m, against 1.22); a second PC joined to it moves smoothly (no rubber-banding: its prediction runs the same equations). A broken line (`expr.speed=bad(`) changes nothing and doesn't stop the mode loading. **Protocol 44**: this build can't join servers of the protocol 43 versions (1.18.7 to 1.18.21) or of production 1.17.0 (protocol 42), or host for their clients (the version message says so); their demos convert at startup. |

### Mode logic

(The gamemode editor's own checks are in section 16: floration/quarkology's CHECKLIST.md.)

| ✓ | Check |
|---|---|
| ☐ | A mode with logic: in the gamemode editor (section 16) make one from FFA whose `slot kill` scores 2 a frag and Save it (or copy FFA's `.qlogic` into a `.qmode` in `custom_modes\` and change that slot by hand). Host > Custom modes: that mode shows **LOGIC: name#xxxxxxxx** (break a line in its logic: the error shows in red and hosting it is refused with the line). Host it: the server list name starts `[name#xxxxxxxx]`, joining shows CUSTOM MODE ... - LOGIC: name#xxxxxxxx, and a frag scores 2. Saving the mode in Host > Custom modes keeps its logic. |
| ☐ | A **ranked** match (and a time trial) plays the standard rules: no custom numbers or logic, no LOGIC label. The 13 built-in modes play as in the last release. |

### Weapon sets

(The weapons editor's own checks are in section 16: floration/quarkology's CHECKLIST.md.)

| ✓ | Check |
|---|---|
| ☐ | In the weapons editor (section 16) make a set with rocket damage 40 and a `slot hit sniper:` that doubles airborne damage, then **Write wpn-<set>.qmode**. Host > Custom modes > that mode: rockets do 40 (a direct hit at least 50), an airborne sniper body hit 80. A second client joining gets the same refire / spread / projectiles (no rubber-banding of shots), and the gamemode or physics editor saving that .qmode keeps its `weaponset=` line. Ranked and time trials play the standard weapons. |

## 7. Maps

| ✓ | Check |
|---|---|
| ☐ | `build/quark_tests group maps`, `group mapbounds` and `group mapbounds2` passed on this commit (spawns, routes, nav graph, ladders, stairs, cart track; nothing reachable out of bounds). Human-made maps print HUMAN-MADE notes instead of failing; read them. |
| ☐ | Load **every** map in its own mode once (the list is in CLAUDE.md → Maps). Look for missing walls, floating objects, black / unlit areas, z-fighting, and spawns facing a wall. |
| ☐ | Run around the edges of 3–4 maps: you can't get out of bounds or stuck; invisible walls (clip) block players but not shots or light. |
| ☐ | Any map in a mode it wasn't built for: the menu warns that it may not work, and it still loads. |
| ☐ | **Acid**: in the MAP EDITOR put an ACID box (M key, after NOGRAPPLE) 0.2 m deep on a floor and TEST: it glows green with a slow shimmer; walking in takes 10 health at once, then about 10 a second (the HUD drops at once, smoothly); you dissolve after about 9-10 s ("dissolved" in the kill feed); stepping out and straight back in doesn't take the 10 again, waiting a second out does. With a second player: get knocked in by a rocket and die there 5+ s later: their kill. Bots walk round a pool when they can. |
| ☐ | Bots on 3–4 maps for a few minutes: they navigate, use objectives, and don't keep falling off. |
| ☐ | **Bot routes round walls and crates**: Depot (bomb): the attackers' bomb carrier goes round the gatehouse's front wall, and bots cross the defenders' yard past the perch; Summit (KOTH): bots from the west spawn (behind the ledge ramp) head for the hill straight away; Viaduct (race): bots take the zig-zag pads. No bot runs on the spot into a wall for more than a second or two. |
| ☐ | **Bot brains** (1.18.11): the default bots play as before. Copy `tools/bots/default.qbrain` to `%APPDATA%\quark\bots\fast.qbrain`, change `reactMs` to 40 for every level, host, console `addbot b expert brain=fast`: the join line reads `BOT <NAME> (EXPERT, fast#xxxxxxxx)` and that bot reacts much faster. A broken file (delete a `:`) is refused with `file:line:col`. `brain=default` gives the built-in bot. Ranked never loads brain files. **HOST GAME → ADVANCED → BOT BRAIN** lists BUILT-IN and every file in the bots folder (a file added while the game runs shows the next time HOST opens); pick FAST, START: every bot's join line shows `fast#...`; the choice is still there after a restart. A broken file shows `ERROR ON LINE n - BOTS PLAY THE BUILT-IN BRAIN` there, and hosting with it says so and the bots join with no brain label. With MODE > RANKED it reads BOT BRAIN: OFF (RANKED). VS BOTS always plays the built-in brain. |

## 8. Map editor

| ✓ | Check |
|---|---|
| ☐ | NEW map: add boxes, every material (including TRIM and CLIP; clip shows see-through violet and is selectable), ladders, stairs, spawns, lights, objectives; move / resize handles on all six faces work through other objects; undo / redo. |
| ☐ | SAVE, SAVE AS, OPEN, TEST (plays it), back to the editor. Names are what you typed. |
| ☐ | Open a **built-in** map, change something, then SAVE / leave: it asks whether to keep your edits as a **new map** (suggests e.g. `temple_edit`). Yes saves a copy; No drops the edits; the built-in map is never changed. |
| ☐ | EDIT THE MAP TOGETHER with a second player: edits appear for both; leaving offers a copy. |
| ☐ | Layout (the redesign), at 1280x720 and in a 4:3 window: the side panel in sections (FILE with NEW / SAVE / EXIT / TEST and BAKE LIGHT beside HOST EDIT SESSION, ADD, the GROUPS row, MODES, the selection), 12 px from the edges, no text smaller than the captions; select a box, room, stairs, ladder, spawn, light, bomb site, hill, checkpoint, path point and a custom group: the selection starts at the same height each time and ROTATE / MIRROR stay on screen. The line under the 3D view shows PLAYABLE AS and the counts, the key help fits the view (a 4:3 window puts it on more lines); EXPORT's question, IMPORT's list and the keep-your-edits question sit in the middle of the 3D view; in a session the chat's SAY line is above the key help. |

## 9. Demos

| ✓ | Check |
|---|---|
| ☐ | Record a demo in a game, stop it, play it back from DEMOS: seek, speed, free camera / follow player. Watching through the recorded player, their own shots are heard (each shot once, the laser as a steady buzz). |
| ☐ | A demo from the previous production version plays (it was converted on update). |

## 10. Hosting and joining (two machines, or two game windows)

| ✓ | Check |
|---|---|
| ☐ | HOST GAME with UPnP on: the pause menu shows whether UPnP worked and whether the port is reachable (green / red with advice; CHECK AGAIN). |
| ☐ | Join by `ip:port` from another machine; also join from the LAN list and the internet server list. |
| ☐ | Reconnect grace: drop one client's network for ~5 s: it rejoins with the same slot, team and score. |
| ☐ | Votes: change map / mode (pick mode, then map), `callvote spec <player>` (needs 3+ players; the target goes to spectator until the next map), restart. Kick is **not** votable: only the host / server console can `kick <name>`. |
| ☐ | Chat, team chat, quips; the chat filter and rate limit. |
| ☐ | Spectating: free camera, follow a player; spectators of a ranked match see health bars, crowns and HUD extras. |
| ☐ | **Spectating in team modes** (1.18.21): die in bomb or payload: you watch your killer for 1.5 s, then cycling shows only teammates, never an enemy's view. |
| ☐ | **Forfeit on an emptied server** (1.18.21, unranked): in VS, one side leaves: the message says the match ends in 10 s, then the other side wins by forfeit; everyone leaves: back to warmup. No "round draw" loop. Rejoining within 10 s carries on. |

## 11. Dedicated servers

Every server box runs two servers (`tools/server/install.sh`): **production** (updates to production releases only)
and **testing** (the next port up, "<name> [TESTING]", updates itself to each testing prerelease within a few minutes).
So the testing build is already live on every box: check it there.

| ✓ | Check |
|---|---|
| ☐ | Within ~10 minutes of the prerelease, each box's testing server runs it: `journalctl -u quark-testing-update` says `built test-<VERSION>.testing.<N>`, `systemctl status quark-testing` is active, and `journalctl -p crit -t quark-update` has nothing new (an alert there names the instance that failed). |
| ☐ | The testing server appears in the in-game internet list of a testing-build game (and in Discord's live servers) as "... [TESTING]"; you can join it; console commands work (`map`, `mode`, `forcestart`, `kick`, `status`, `quit`, typed in a `quark_server` you run by hand: the service has no console). |
| ☐ | A ranked match on it plays normally (queue, lock, result) and says **TESTING SERVER - RANKED NOT RATED** (on joining, at the start, at the end, and on the HUD tag); the leaderboard doesn't change. To test that ranking itself still works, play a ranked match on the **production** server after the release (section 12). If the box is behind a forwarded port or in a container, `--own-addr host:port` is set and neither log shows a "ranked key proof is for ..." warning. |
| ☐ | Load governor: `cat /var/lib/quark-governor/*.status` shows both servers, fresh (`t` within a second or two). With players on both and bots added (`addbot` x10 on each in a hand-run console, or a busy evening), `journalctl -u quark -u quark-testing \| grep governor` shows any throttling in the right order (the empty server first, then the one not in a ranked match), and production never below what it needs. |
| ☐ | Server CPU and rate (1.18.3): with a few players on the testing server, `status` in a hand-run console shows `tick 1000 Hz`, a spin, late p99 and `stalls` near 0%, and `top` shows the server well under its old ~30% of a core. On a slow or shared box the log may show `tick rate -> 500 Hz (... ticks over 4.0 ms late)` at a break: expected, but only after two 3 s windows in a row had late ticks. A one-off host pause (cloud VM steal) only logs `tick rate: late ticks in one window (...), waiting for a second` and then `the next window was clean: no step down`, and the rate stays (`status` says `waiting for a second` meanwhile, and `tick rate steps down at the next break (...)` once a step is pending). Bandwidth steps say `snapshot rate -> ...`. |
| ☐ | Server options (1.18.3, 1.18.11): a `quark_server` started by hand with a low `--max-upload-kbps` (e.g. 2000) and enough players steps its snapshot rate down past 90% of it (`snapshot rate -> ...`; `status` shows both rates and why); `--tick-spin 0` and `--tick-spin auto` both start, and `status` shows the spin and `tick 1000 Hz`; `--botbrain fast` (a file in its `bots/` folder) makes every bot it adds join as `fast#xxxxxxxx`, and a ranked match on it still plays the built-in bot. |
| ☐ | Pause (1.18.15): with only bots (or nobody) on, the testing server's log says `paused: no humans (bots wait)` and `top` shows it near 0% of a core; joining logs `resumed` and you start at warmup. |
| ☐ | Leave the testing server running ~30 minutes with bots and a player on (bots alone pause): no crash, no growing memory (`top`), tick rate steady (`status`). |
| ☐ | **After** production is tagged: each box's production server picks up the update by itself within a few minutes once no match is running (`journalctl -u quark-update`, `systemctl status quark`), its testing server stops once its match ends (`systemctl status quark-testing`: inactive, with the condition not met), since nothing is newer than production, and `journalctl -p crit -t quark-update` is empty. |

## 12. Ranked

| ✓ | Check |
|---|---|
| ☐ | Queue from the game (RANKED) and from Discord (`/queue`): both see the same queue; a fill pings both players. |
| ☐ | Play a ranked 1v1 vanilla (Temple) to the end: both clients show the same result; the rating changes on the in-game leaderboard and in Discord's #leaderboard within a few minutes. |
| ☐ | During a live ranked match: no votes, no map or mode change, no team switch, no kick (the lock is on exactly the three ranked ladders: 1v1 vanilla, 1v1 laser vampire, FFA instagib; the Viaduct race isn't locked). |
| ☐ | Leaving a live ranked match early: the leaver loses; the player who stayed can then leave freely. |
| ☐ | A new account is "provisional" for its first 5 matches. |
| ☐ | Viaduct time trial: a run records a best time on the race board. |

## 13. Ranking server (the Cloudflare Worker + D1 database)

Only needed when the update changes `tools/master/` (worker.js, schema.sql) or the game's ranked protocol; Claude's notes for the update say so. The Worker and the game must be updated together.

| ✓ | Check |
|---|---|
| ☐ | Locally: `node tools/master/test.mjs` passes. |
| ☐ | The SQL changes listed in `tools/master/README.md` for this version have been run in the D1 console **before** deploying the Worker. |
| ☐ | Deploy `tools/master/worker.js` (Cloudflare dashboard → the Worker → Edit code → paste → Deploy). Settings: the cron trigger `*/15 * * * *` exists, and the secrets `ADMIN_KEY` and `BOT_KEY` are set. |
| ☐ | The master URL is in `site/master.txt`. Its `/list` returns servers, and `/leaderboard`, `/crowns`, `/raceboard` return JSON. |
| ☐ | The Worker's logs (dashboard → Logs) show no errors while you play the ranked match in section 12. |

## 14. Discord bot

| ✓ | Check |
|---|---|
| ☐ | The testing build was posted in **#testing-updates** only, with the Testing Updates ping. |
| ☐ | `/changelog <VERSION>.testing.<N>` shows the notes labelled as testing; `/changelog` alone shows the current production version. |
| ☐ | `/servers`, `/leaderboard`, `/queues`, `/queue`, `/leave` all answer; the #queues panel buttons work. |
| ☐ | `/link`: the code it gives you, typed in the game (SETTINGS → DISCORD CODE), links your account; verified #1 / top 10 players get their roles within 5 minutes. |
| ☐ | Live server channels appear for busy servers (at most 5) and go away when a server stops. |
| ☐ | If the bot's code changed (the `discord-bot` branch): it updates itself from that branch within 10 minutes (docs/OPERATIONS.md section 2), and `journalctl -u quark-discord` shows it running without errors. |
| ☐ | **After** production: #updates gets the release post with the Update Pings ping. |

## 15. Website

The site is only redeployed by a production release (a testing build only replaces its `assets/data/testing/`), so check the source now and the live page afterwards.

| ✓ | Check |
|---|---|
| ☐ | With the `site` branch checked out in `site/` (README "Website"), open `site/index.html` in a browser: the version, changelog, weapon table and download section are right and nothing is broken (also on a phone-width window). |
| ☐ | `changelogs/full/<VERSION>.md` exists and explains every change and why. Run `python3 tools/site_updates.py .` and open `site/updates/index.html` and `site/updates/<VERSION>/index.html`: the list shows every version, and the version page shows the full changelog. |
| ☐ | Assets and Sounds pages: run `python3 tools/site_assets.py`, serve `site/` (`python3 -m http.server -d site`) and open `/assets/` and `/sounds/`: each tab's models and maps spin, zoom and pan with the team colour switch; sounds and voices play with waveforms; the box builder downloads a .glb and the synth a .wav; TESTING says "No testing build published yet" when there's none. **After** this testing build: the live `/assets/?channel=testing` shows it, and `SHA256SUMS` still verifies. |
| ☐ | **First production deploy from the `site` branch** (1.19.0): the production run's pages job checks out the `site` branch into `site/` and deploys it. In Actions, the pages job is green and its log shows the `site` checkout; if it failed or was skipped, run Actions → build-and-release → Run workflow on the default branch with **site** ticked. |
| ☐ | **After** production: https://floration.github.io/quark shows the new version and changelog; each download link works, and its `.sig` and `SHA256SUMS` are there. |
| ☐ | **After** production: https://floration.github.io/quark/updates/ lists the new version, and https://floration.github.io/quark/updates/<VERSION>/ shows its full changelog. The Discord #updates post links to that page. |

## 16. Quarkology and the editors

Quarkology (the Supervisor Board app) and the editors live in their own repository since 1.20, floration/quarkology (private), and so do their checks: its `CHECKLIST.md`, which Quarkology keeps in `Quark\quarkology-repo\CHECKLIST.md`. The Supervisor Board's Release testing tab shows them here, under this section. Do them every release: the editors run the game's code (`quark_maptool`, `quark_tests`), so a game change can break one.

- Every row of `CHECKLIST.md` in floration/quarkology (`Quark\quarkology-repo\CHECKLIST.md`): the app on both PCs (the clone of floration/quarkology, the WIP sync), then each editor.

## 17. Sign-off and going to production

| ✓ | Check |
|---|---|
| ☐ | Sections 0–16 are done (or skipped for a small patch, with a reason), and nothing is open. |
| ☐ | Tell Claude: **"the testing build passed the checklist, go to production"**. Claude opens the `testing` → default-branch PR. |
| ☐ | You merge the PR, then tag the merge commit: `git tag v<VERSION> <merge commit> && git push origin v<VERSION>`. The production run needs Windows, Linux **and** macOS green; it publishes the release as Latest and deploys the website. |
| ☐ | Check the "after production" rows in sections 1, 11, 14 and 15 within the hour. |

**If production turns out broken:** don't delete or move the tag (launchers and servers may already have it). Fix it on `testing`, bump the patch version (e.g. 1.19.1), run this checklist on the fix, and release that. Dedicated servers update to it by themselves.

### Sign-off record

| Build | Date | Tester | Platforms | Result | Notes |
|---|---|---|---|---|---|
| `test-<VERSION>.testing.<N>` | | | Windows / Linux / macOS | pass / fail | |
