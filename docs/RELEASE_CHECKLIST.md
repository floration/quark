# Production release checklist (for the owner)

Do this for **every** update, after it has been pushed to the `testing` branch and published as a testing prerelease, and **before** it goes to production (the `testing` → default-branch PR and the `v<VERSION>` tag). Claude never pushes to production until you have been through this list and said so.

- **When:** CI has published the prerelease `test-<VERSION>-test.<N>`. The launcher's TESTING channel and Discord's #testing-updates both show it.
- **Time:** about 60–90 minutes for the full list. For a small patch (one fix, no netcode, map or server changes), do sections 0–2, the parts of 3–15 the changelog touches, and 16.
- **How to record it:** copy the sign-off table at the bottom into the PR description (or a note), and tick each section as you go. Write down anything odd, even if it seems minor, and give it to Claude before production.
- **If something fails:** stop. Tell Claude what failed and how to reproduce it. The fix goes to `testing` as a new build (`-test.<N+1>`), and you redo the sections it touches. Never tag production on a build with a known failure in sections 0, 1, 2, 11, 12 or 13.

---

## 0. Before you start: the build is complete

| ✓ | Check |
|---|---|
| ☐ | GitHub → Actions: the run for the `testing` push is green: Linux tests, Windows tests, macOS build, sanitizers. |
| ☐ | GitHub → Releases: the prerelease `test-<VERSION>-test.<N>` exists. It is marked **Pre-release**, not "Latest". |
| ☐ | Its assets: `quark-windows.qpak`, `quark-linux.qpak` (and `quark-macos.qpak` once the macOS build finished), each with a `.sig`; `quark-release.txt` + `.sig`; `SHA256SUMS` + `.sig`. |
| ☐ | `VERSION` is bumped. `changelogs/<VERSION>.md` (the short notes) and `changelogs/full/<VERSION>.md` (every change, and why) both exist; a production tag without either fails. |
| ☐ | Discord #testing-updates announced the build (and #updates did **not**). |
| ☐ | Claude's notes for this update list nothing still open (ask: "anything unfinished or untested in this build?"). |

## 1. Install and update (launcher)

Do this on Windows; also on Linux / macOS if you have one.

| ✓ | Check |
|---|---|
| ☐ | Start from the **current production** install (with your normal settings, maps, demos and HUDs). |
| ☐ | Launcher → CHANNEL → TESTING: the new build is listed and installs without any signature warning. |
| ☐ | After the update, your settings, keybinds, HUD layouts, ranked key, your own maps and your demos are all still there. |
| ☐ | Files the old version shipped and the new one doesn't are gone from the install folder; nothing you made was deleted. |
| ☐ | If this update changed the demo format: old demos were converted (originals in `demos/old/<version>/`), and an old demo plays. A demo that couldn't convert says "recorded with version X" instead of failing. |
| ☐ | If this update changed the map format: your own maps were converted (originals in `maps/old/v<N>/`, `maps/old/convert.log`) and still open in the editor. |
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
| ☐ | No stutter when bots spawn, when a round restarts, when the scoreboard opens, or when a demo is recording. |
| ☐ | Alt+Enter switches borderless ↔ windowed (and doesn't also send Enter to chat). |

## 3. Menus (every screen)

The automated UI test clicks every button (`quark --uitest`, part of the test suite). Here you check that things look right and make sense to a person.

| ✓ | Check |
|---|---|
| ☐ | Main menu: JOIN GAME, HOST GAME, RANKED, MAP EDITOR, SETTINGS, INFO, DEMOS, QUIT each open the right screen, and Esc / BACK returns. |
| ☐ | INFO: every tab reads correctly and matches this version (weapons, modes, controls). |
| ☐ | Text fits everywhere: nothing overlaps or runs off a button at your normal resolution and at 1280×720. |
| ☐ | Pause menu in a game: SETTINGS, record / stop demo, EDIT THE MAP TOGETHER (not in ranked), DISCONNECT / STOP HOSTING, QUIT. |
| ☐ | Console (`~`): monospaced; Tab completes commands and their arguments; it does not open the scoreboard. |

## 4. Settings

For each one: change it, check the effect in a game, quit, restart, check it was kept.

| ✓ | Check |
|---|---|
| ☐ | Video: fullscreen / borderless / windowed, resolution, vsync, FPS cap, field of view. |
| ☐ | Outline thickness: **world** and **player** separately (0 = off). |
| ☐ | Crosshair: style, size and colour; the **hit-flash colour** setting, including **off**; the default is unchanged from the last release. |
| ☐ | Audio: master, effects, announcer, music volumes; the announcer pack. |
| ☐ | Audio (Windows): sound is clean with EXCLUSIVE AUDIO off (shared, 10 ms) and on (3 ms on the owner's PC; other programs go silent, back when it's turned off); unplugging or switching the output device mid-game moves the sound to the new default. |
| ☐ | Controls: rebind every action you use. Bind **chat to a letter key** (e.g. Y): pressing it opens chat **without** typing a "y". Bind **Toggle chat**: incoming chat hides / shows with a short message. **Disable chat** in settings stays in sync with the toggle. |
| ☐ | Interpolation setting (up to 200 ms): shots still hit what you see. |
| ☐ | HUD editor: drag, add, delete, import / export a layout; the weapon list shows only the weapons the mode has. |
| ☐ | DISCORD CODE: see section 13. |
| ☐ | **Name colour** (SETTINGS, right column): turn it on and pick a colour; a second player sees your name in it on the scoreboard, in chat and in the kill feed; team name tags stay the team colour; a very dark colour shows lighter. Off again: your name goes back to the default. |
| ☐ | **Health bars** are on after updating from 1.16 even if you had them off; turning them off now stays off across restarts. |
| ☐ | Every number slider: click it and **type a value**; **right-click** resets it to its default (check the custom-mode MOVEMENT page too). Damage-number size, damage flash (off by default; on / intensity in VISUALS), sniper and rifle zoom FOV, and the fullbright map colour all work. |

## 5. Weapons and movement

Use HOST GAME → FFA (bots off or beginner) and the changelog's numbers.

| ✓ | Check |
|---|---|
| ☐ | Each weapon fires, sounds right, shows its own projectile model, and does the damage the changelog / INFO says (rifle 4.95 body / 7.5 head; laser 24 m range; sniper a flat 50; rocket drops 10% less than before). |
| ☐ | Alt-fires: rifle scope, shotgun bounce, rocket lob, knife throw, bubble stream. |
| ☐ | Knife throw: the knife is **disabled for 4 s** (greyed with a timer), and nothing can be used for the knife's normal swing cooldown. |
| ☐ | Syringe: self-use heals **10 HP** with a **1 s** cooldown; a throw heals a teammate, disables the syringe for 4 s, with a 1 s cooldown. It only exists in modes where you respawn. |
| ☐ | Movement: jump, crouch, bunny-hop, air-strafe, dash, grapple; run speed 9.5 feels right; ladders (6.75 up) and staircases climb smoothly; a standing jump can be steered; at speed A/D strafing turns better than W alone. |
| ☐ | **Dash charges + cooldown**: two dashes, then none until you land (landing refills both, a bunny hop refills one); each dash also waits 1.2 s from the END of the previous one (two in a row don't work, on the floor either); pressing dash during a dash does nothing; a dash jump keeps both; a super dash and a wall bounce leave one. The HUD shows a pip per charge; while the cooldown runs the pips fill grey and light up when it's done. Custom mode MOVEMENT page: DASH COOLDOWN slider (type a value, right-click resets). |
| ☐ | Hitmarkers, red headshot numbers, kill feed, damage direction. |
| ☐ | Thrown knives, crossbow bolts and thrown syringes stay stuck where they land: in walls until the match ends (gone after a restart or match end), in a player until that player dies. With lots of them about, F3's 1% and 0.1% lows don't drop. Super and hyper play their own sounds; the locator pings. |

## 6. Game modes

Host each mode on **its own map** with bots (and a second human if you can). For every mode: the round starts, the objective works, scores add up, the round and match end, and the HUD and scoreboard are right.

| ✓ | Mode | Map | Mode-specific checks |
|---|---|---|---|
| ☐ | VS | Bastion / Orbital | Rounds, no respawn, health drains in a stalemate. |
| ☐ | Bomb | Depot | Plant, defuse, a dropped bomb falls to the floor and returns to an attacker after 15 s, overtime. |
| ☐ | Race | Viaduct | Checkpoints, finish time, the time trial records a best time. |
| ☐ | King of the hill | Summit / Downtown | Capture, a contested hill freezes the clock. |
| ☐ | Payload | Railyard | The cart moves only with attackers on it, checkpoints add time, overtime. |
| ☐ | Neutral payload | Canal | Both teams push. |
| ☐ | FFA | Harbor / Gantry | Frags on the scoreboard, overtime tiebreak. |
| ☐ | Duel | Clockwork | Size modifiers (giant / tiny) change the collision box. |
| ☐ | 1v1 | Temple, Furnace, Dojo | Rulesets (vampire, instagib, ...), first to 5; Dojo's 5 s hold-fire start. |
| ☐ | Tourney | Colosseum | Bracket, seeding, byes, a drop isn't an instant walkover. |
| ☐ | Hide and seek | Hamlet / Hightown | Everyone seeks once. A hider still unfound at round end scores 1 and the seeker 1 per hider found. Only the seeker holds the knife and the **locator** (key 0: one random hider shown for 1 s, every 5 s). Hiders are never shown the seeker's position. A tie for first after the last round → **rock paper scissors** (keys 1 / 2 / 3). |
| ☐ | Tag | Foundry | Only the tagger holds the knife + locator; runners always see the tagger (marker through walls / edge arrow). Leaving or spectating eliminates you; `addbot` mid-match waits for the next match. |
| ☐ | Multi point | Midtown / Lodestone | Points A–E in order, staged spawns move forward, overtime. |
| ☐ | Custom mode | any | Make one in the custom-mode editor, host it, the rules apply. |

## 7. Maps

| ✓ | Check |
|---|---|
| ☐ | `docs/MAP_REPORT.md` in this build: every generated map scores ≥ 7 in every category, and the attack/defend timing (attacker minus defender arrival) is within −1..+1 s at every site / point. Human-made maps are listed separately with their numbers. |
| ☐ | Load **every** map in its own mode once (the list is in CLAUDE.md → Maps). Look for missing walls, floating objects, black / unlit areas, z-fighting, and spawns facing a wall. |
| ☐ | Run around the edges of 3–4 maps: you can't get out of bounds or stuck; invisible walls (clip) block players but not shots or light. |
| ☐ | Any map in a mode it wasn't built for: the menu warns that it may not work, and it still loads. |
| ☐ | Bots on 3–4 maps for a few minutes: they navigate, use objectives, and don't keep falling off. |

## 8. Map editor

| ✓ | Check |
|---|---|
| ☐ | NEW map: add boxes, every material (including TRIM and CLIP; clip shows see-through violet and is selectable), ladders, stairs, spawns, lights, objectives; move / resize handles on all six faces work through other objects; undo / redo. |
| ☐ | SAVE, SAVE AS, OPEN, TEST (plays it), back to the editor. Names are what you typed. |
| ☐ | Open a **built-in** map, change something, then SAVE / leave: it asks whether to keep your edits as a **new map** (suggests e.g. `temple_edit`). Yes saves a copy; No drops the edits; the built-in map is never changed. |
| ☐ | EDIT THE MAP TOGETHER with a second player: edits appear for both; leaving offers a copy. |

## 9. Demos

| ✓ | Check |
|---|---|
| ☐ | Record a demo in a game, stop it, play it back from DEMOS: seek, speed, free camera / follow player. |
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

## 11. Dedicated servers

Every server box runs two servers (`tools/server/install.sh`): **production** (updates to production releases only)
and **testing** (the next port up, "<name> [TESTING]", updates itself to each testing prerelease within a few minutes).
So the testing build is already live on every box: check it there.

| ✓ | Check |
|---|---|
| ☐ | Within ~10 minutes of the prerelease, each box's testing server runs it: `journalctl -u quark-testing-update` says `built test-<VERSION>-test.<N>`, `systemctl status quark-testing` is active, and `journalctl -p crit -t quark-update` has nothing new (an alert there names the instance that failed). |
| ☐ | The testing server appears in the in-game internet list of a testing-build game (and in Discord's live servers) as "... [TESTING]"; you can join it; console commands work (`map`, `mode`, `forcestart`, `kick`, `status`, `quit`, typed in a `quark_server` you run by hand: the service has no console). |
| ☐ | A ranked match on it plays normally (queue, lock, result) and says **TESTING SERVER - RANKED NOT RATED** (on joining, at the start, at the end, and on the HUD tag); the leaderboard doesn't change. To test that ranking itself still works, play a ranked match on the **production** server after the release (section 12). If the box is behind a forwarded port or in a container, `--own-addr host:port` is set and neither log shows a "ranked key proof is for ..." warning. |
| ☐ | Load governor: `cat /var/lib/quark-governor/*.status` shows both servers, fresh (`t` within a second or two). With players on both and bots added (`addbot` x10 on each in a hand-run console, or a busy evening), `journalctl -u quark -u quark-testing \| grep governor` shows any throttling in the right order (the empty server first, then the one not in a ranked match), and production never below what it needs. |
| ☐ | Leave the testing server running ~30 minutes with bots: no crash, no growing memory (`top`), tick rate steady (`status`). |
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
| ☐ | `/changelog <VERSION>-test.<N>` shows the notes labelled as testing; `/changelog` alone shows the current production version. |
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
| ☐ | **After** production: https://floration.github.io/quark shows the new version and changelog; each download link works, and its `.sig` and `SHA256SUMS` are there. |
| ☐ | **After** production: https://floration.github.io/quark/updates/ lists the new version, and https://floration.github.io/quark/updates/<VERSION>/ shows its full changelog. The Discord #updates post links to that page. |

## 16. Sign-off and going to production

| ✓ | Check |
|---|---|
| ☐ | Sections 0–15 are done (or skipped for a small patch, with a reason), and nothing is open. |
| ☐ | Tell Claude: **"the testing build passed the checklist, go to production"**. Claude opens the `testing` → default-branch PR. |
| ☐ | You merge the PR, then tag the merge commit: `git tag v<VERSION> <merge commit> && git push origin v<VERSION>`. The production run needs Windows, Linux **and** macOS green; it publishes the release as Latest and deploys the website. |
| ☐ | Check the "after production" rows in sections 1, 11, 14 and 15 within the hour. |

**If production turns out broken:** don't delete or move the tag (launchers and servers may already have it). Fix it on `testing`, bump the patch version (e.g. 1.17.1), run this checklist on the fix, and release that. Dedicated servers update to it by themselves.

### Sign-off record

| Build | Date | Tester | Platforms | Result | Notes |
|---|---|---|---|---|---|
| `test-<VERSION>-test.<N>` | | | Windows / Linux / macOS | pass / fail | |
