# Next (testing builds since 1.19.3)

What's on `testing` since production 1.19.3. The release PR renames this file to `changelogs/<VERSION>.md`.

## Settings (the settings update, batch 1 of the 231 you picked)
- **New MORE pages** on CONTROLS and VIDEO + AUDIO (button at the bottom middle) for settings that didn't fit.
- **Mouse:** vertical sensitivity, invert Y, zoom sensitivity (x1.00 = the same feel as unzoomed).
- **Scoreboard:** hold or toggle.
- **Window:** window size picker, the window's size and position kept between launches, and a 10 second "keep these display settings?" that puts the old mode back if you don't click KEEP.
- **Background:** an FPS cap while alt-tabbed or minimized (60 by default) and an optional mute.
- **Shadow quality** low / medium / high, applied at once. The GPU's name is shown, and laptops with two GPUs now run the game on the dedicated one.
- **HUD size:** every HUD element bigger or smaller at once.
- **Crosshair:** dot size, opacity, outline thickness and opacity, a style per weapon (e.g. a dot on the sniper), and COPY / PASTE CODE to share a crosshair in one line.
- **Hitsounds:** their own volume, any sound pack's hitsound, a headshot ding, and the kill sound on its own switch.
- **Server list:** sort by ping / players / name, hide full / empty, a ping limit, favourites (right-click a server; listed first) and a RECENT button for the servers you joined last.
- **Race and surf:** tap R to go back to your last checkpoint, or hold it a quarter of a second to restart from the start. FULL RESTART can also go on a key of its own (CONTROLS > MORE KEYS). Both can be rebound.

## Settings (batch 2, first half)
- **More MORE pages:** GAMEPLAY gets one, and VIDEO + AUDIO now has three. CONTROLS has a third keys page, MORE KEYS.
- **Picture:** gamma, contrast and saturation for the 3D scene (the HUD stays as it is), fog amount or off, and render scale now goes from 25% to 400%.
- **Presets:** LOW / MEDIUM / HIGH / COMPETITIVE set shadows, anti-aliasing, render scale, fog and effects in one go.
- **Effects:** effects opacity (explosions, sparks, bursts; 0 = off), kill bursts, tracers and the gun's recoil kick on their own switches.
- **Colour blindness:** two team colour presets for red-green and blue-yellow colour blindness, and a warning under the team colours when your pair would be hard to tell apart.
- **Enemy model:** draw every enemy as one model (not in ranked).
- **Sound:** separate volumes for your footsteps, other players' footsteps (up to 2x louder), gunfire and the bomb beep. **The planted bomb now beeps**, faster as it runs down.
- **Kill feed and chat:** how long lines stay, how many show, only kills you're in, chat opacity. While typing, the mouse wheel or Page Up / Down scrolls back through chat.
- **Team chat:** a TEAM CHAT key sends to your team only (shown as "(TEAM)").
- **More keys:** LAST WEAPON, SPEC NEXT / SPEC PREV while dead or spectating, HIDE HUD (with an option to keep the crosshair). When a new bind takes a key from another action, the page says so, with an UNDO button.
- **HUD:** FPS and ping in the top right without a widget, announcer captions, health bars that fill to the mode's max health instead of 100, and a headshot colour for the crosshair's hit flash. Hints that name a key (like "HOLD E" to plant) now show your own bind.
- **Controls:** grapple hold or toggle, left-handed (gun on the left, grapple launcher on the right), and NEXT / PREV WEAPON can stop at the ends and skip weapons you pick.
- **Display:** G-SYNC / FreeSync mode (vsync on with the frame cap 3 under the refresh), and fullscreen can stay on screen when you alt-tab.
- **Windows:** CPU priority HIGH as an option (above normal stays the default).
- **Menus:** settings can open on the last tab you used, QUIT / DISCONNECT / STOP HOSTING can ask "SURE?" first, and a RECONNECT button on the main menu joins your last server again.
- **Connection:** if the connection drops, the game tries to reconnect by itself (3 times; it can be turned off).
- **Queues:** when your queue fills, a chime plays and the taskbar button flashes if the game is in the background.
- **Map editor:** redo (Ctrl+Y or Ctrl+Shift+Z), and an autosave every minute while you have unsaved changes, with RESTORE AUTOSAVE if the game closed before you saved.

## Settings (batch 2, second half)
- **Settings search:** a SEARCH box at the top of settings. Type part of a setting's name (or a key's) and click a match to jump to its page.
- **Three keys per action:** every action can now have three keys instead of two.
- **Console binds:** `bind`, `unbind`, `binds`, `alias`, `unalias`, `exec` and `toggle` in the console. A key can run any console command, and `autoexec.cfg` in the data folder runs at start.
- **Export / import all settings:** GAMEPLAY > MORE > ALL SETTINGS copies every setting to the clipboard, or reads them back; RESET ALL (click twice) goes back to the defaults, keeping your name and servers.
- **Demos:** sort by date, name, size or map, each demo's size, DELETE, AUTO RECORD (every match or ranked only) and KEEP the newest N.
- **Sensitivity from another game:** CONTROLS > MORE turns a CS2, Quake, Apex, TF2, Valorant, Overwatch or Call of Duty sensitivity into Quark's.
- **Fullscreen resolution:** VIDEO + AUDIO > MORE 3 picks any mode your monitor offers.
- **Practice (your own server, alone with bots):** pause > PRACTICE has save / load position, god mode, passive bots (they don't shoot) and reset.
- **Players:** pause > PLAYERS lists everyone on your server, with KICK, ADD BOT, KICK BOTS and RESTART.
- **Race ghost:** in race and surf, your best run on each map plays as a see-through ghost.
- **Team panel and match summary:** an optional panel with your teammates' health, and a summary of your match (shots, hits, accuracy, damage, best streak) under the end-of-match scoreboard.

## Settings (batch 3: video, audio, crosshair, network, accessibility)
- **More picture options:** VIDEO + AUDIO > MORE 2 adds TONE CURVE, CLEAN SURFACES, FLAT LIGHTING, SIMPLE SKY, FAR SCENERY, DRAW DISTANCE (150 / 250 / 400 m), DEBAND and 10-BIT OUTPUT, plus tracer colour (classic, team or your own) and projectile TRAILS (off / short / full, with opacity).
- **MSAA 8x**, and options your PC can't do now say "N/A" (MSAA levels, 10-bit, latent sync, bypass compositor).
- **New VIDEO + AUDIO > MORE 4:** shadow edge, shadow distance, shadows on the map / on players, your own shadow, players always lit, player brightness, custom outline colours and opacity, separate team / enemy outline widths, rope colour, fixed team A / B colours when spectating or watching demos, colour scheme copy / paste and 3 save slots, and accessibility: REDUCE FLASHING, colourblind-safe OK / error colours, tag "it" and race splits in your palette, AHEAD / BEHIND on splits.
- **New VIDEO + AUDIO > MORE 5 (sound):** mono / swapped channels, stereo width, behind-you muffle, hearing range, sound occlusion (walls muffle sounds), dynamic range (clean / normal / night), audio latency (low / normal / safe), volume in dB or on a perceptual curve and up to 150%, background volume, force your sound pack on everyone, streak callouts (all / mine / off), time warnings (off / 60 + 30 / countdown, and on screen), each cue sound and announcer line on or off, and VISUALIZE SOUNDS.
- **New VIDEO + AUDIO > MORE 6:** a NET GRAPH (apart from F3), packet loss protection 2 / 4 / 8, map downloads on / off with a cache limit and CLEAR, the light bake cache (size, bake threads, CLEAR), FOV as horizontal / vertical / 4:3, bypass compositor (Linux), latent sync safety and late-frame behaviour (Windows), a bigger or coloured menu cursor, and USE as hold or toggle.
- **Crosshair:** a spread circle, a cooldown ring, and sizes in exact pixels instead of scaled with the resolution.

## Maps
- **Thin walls are shoot-through:** any surface 1/8 m thick or less (coats, panels, thin trim) now lets hitscan through at wallbang damage, like the dark grey wallbang walls.
- **Map picker:** lists only the maps made for the mode you picked (ranked: only that ladder's maps), 7 across, each with a picture of the map from above. The mouse wheel scrolls when there are more.
- **Two new maps, made by q:** **Surfy** (surf), now the map surf mode starts on, and **Cobblestone**.

## Servers
- **Testing server alone:** `install.sh --testing-only` sets up just the testing server, on the normal port, and keeps it running all the time on the newest testing build or production release, whichever is newer. Both servers stay the default (`--both` to go back); `--no-testing` is still production only.

## Modes
- **New mode: POWER.** Each team shares one health pool, 2500 for every player on it, and nobody dies. An enemy's hits come off your team's pool instead of your health. When a team's pool runs out it loses; when the 10 minutes are up, the fuller pool wins. Your own rockets and the world (lava, acid, falls) take nothing from the pool; falling out of the map still respawns you. Someone who joins mid-match adds their 2500. The score box shows both pools, and the bar under it shows yours. Played on Bastion, Orbital and Lodestone. **The network protocol is now 54**, so games and servers update together.
