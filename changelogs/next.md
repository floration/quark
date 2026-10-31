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

## Settings (batch 3: HUD, social, controls, menus, host, editor, launcher)
- **Hit feedback (GAMEPLAY > MORE 2):** damage number colour, stacking and time; damage direction arcs (colour, size, time); kill mark time and colour; a KILLED <NAME> line; hit flash time; low health warning (threshold, colour, pulse, beep); green +N heal numbers; weapon kick amount; and per weapon: show its model, and move it (x, y, depth).
- **HUD info (GAMEPLAY > MORE 3):** speed in m/s, km/h or ups with a scale and ticks; bar captions; a 12 h clock, the round clock counting up and tenths in the last 10 s; pick the verbose info lines; auto-hide health, weapon list and bars; objective marker size, opacity, distance and edge arrow; scope size and reticle colour / width; network warnings (ping, loss, stall); a minimal spectator HUD; a headshot mark in the kill feed; a stalemate warning before the drain.
- **Social (GAMEPLAY > MORE 4):** team chat only, a word filter, one chat colour, timestamps, your name highlighted with a ping, system messages all / important / off, a chat box with its own opacity, scoreboard sort and columns, others' name colours on or off, streamer mode (hides addresses, codes and names), alerts when the game is in the background, and what to do on joining (play, spectate, ask). Death camera: on the killer, your body or a teammate, for 0 to 3 s; spectator FOV and third person.
- **Controls (CONTROLS > MORE 2):** per-weapon sensitivity, alt-fire toggle and swapped fire buttons; mouse acceleration and raw input; auto-hop off; a console key of your choice and the console in menus; alt-fire toggles allowed in ranked. New binds: volume up / down / mute, record demo, join team, spectate. Mode profiles can now set sensitivity, FOV and zoom FOV.
- **Menus (PLAYER > MORE):** menu theme (dark or high contrast), your own accent, text brightness, panel darkness; the menu background as clockwork, a map you pick or a plain colour; the data folder with OPEN; your quip; mods (use your overrides, open the folders); remember your ranked queues; hide ratings. PLAYER shows your model turning in your colours (drag to turn it).
- **HUD:** the objective line, the notices and the scoreboard can now be moved, sized, coloured or hidden on the HUD tab like the other elements. Custom widgets get 8 more values (snapshot rate, accuracy, damage, streak, round, vertical speed, height, weapon).
- **Players and votes:** pause > PLAYERS is open to everyone: block one player's quips. New votes: restart, 5 more minutes, shuffle teams, kick a player.
- **Hosting:** tick rate, snapshot rate and upload cap; auto start; balance bot skill; vs bots up to 15 a side.
- **Editor:** fly keys follow your binds, fast-fly multiplier, a custom grid, a rotation step, the fly speed, grid and last light are remembered; test play picks the mode, bots, starts at the camera and can skip warmup.
- **Race:** splits against your best, the previous segment or your last run, with a display time; the race stats page picks the map and the date format.
- **Launcher:** OPTIONS: start the game automatically, keep the launcher open, launch options, and updates automatic / ask / at next launch.

## Settings (batch 4: colours, screenshots, sound extras, latency)
- **New VIDEO + AUDIO > MORE 7 (colours):** the world's outline colour, a wallbang tint, a high-contrast map palette, your own sky and glow colours on every map, objectives in your palette, zone pulse and opacity, a colour per FFA opponent, your own colour for your shots / laser / rope, gun colour (map / team / custom), health bar low threshold and colours, name tag colour / opacity / size, explosion colours (their own / your team / custom) and a colourblind palette for F3.
- **New VIDEO + AUDIO > MORE 8:** screenshots as PNG or TGA, saved in the game's folder or Pictures/Quark, with or without the HUD, and an OPEN FOLDER button; demo rate (also here, not only on DEMOS); always on top; dash buffer (off / 0.05 / 0.1 / 0.15 s); ultra low-latency safety margin; NVIDIA threaded optimization off and DXGI present (Windows); hitsound tiers on / off, typing sounds on / off, quip captions and announcer ducking.
- **MORE 2:** liquids flowing or still, glow strength, SMOOTH strength and the lightmap filter (smooth / nearest).

## Settings (batch 4: HUD, menus, social, editor, practice)
- **New HUD > MORE:** your own colours for recharging bars, the speed meter (slow to fast), weapons you can't use, the sticky counter at its most, warning and info lines, and headshot / big-hit damage numbers; the text outline's width (none to thick); how many notices show and for how long; a compass and a radar (teammates and objectives only, not in ranked); hide the REC indicator; tourney's TAB shows the bracket or the scoreboard.
- **Gameplay:** STAIR SMOOTHING off steps the view up stairs at once (GAMEPLAY > MORE 3). SHOW I'M TYPING off hides your "..." from others, and the tiebreak's rock-paper-scissors cards can be clicked (GAMEPLAY > MORE 4).
- **Menus and bots (PLAYER > MORE):** FIXED LOGO COLORS keeps the title cyan / red; your server's bots can all wear the standard model, and take names from `bots/names.txt`.
- **Editor:** CLIP BOX COLOR (CONTROLS > MORE 2). **Race:** SPLITS: PALETTE also colours your best run and the leaderboard's #1.

## Movement fixes
- Wall teleporters keep your speed. Going in fast used to throw you out at a crawl about half the time (surfy too).
- **Walk teleporters:** a teleporter can now let you out at walking speed instead of carrying all your speed (map editor: SPEED: CARRY / WALK on a teleporter; the one you go into decides). **Maps are now format 12**: older maps convert by themselves, but an older game can't open a map with a walk teleporter.
- The grapple can't hook clip any more: it hits it and lets go, like nograpple. No more invisible hook points on map lids.

## Maps
- **Thin walls are shoot-through:** any surface 1/8 m thick or less (coats, panels, thin trim) now lets hitscan through at wallbang damage, like the dark grey wallbang walls.
- **Map picker:** lists only the maps made for the mode you picked (ranked: only that ladder's maps), 7 across, each with a picture of the map from above. The mouse wheel scrolls when there are more.
- **Two new maps, made by q:** **Surfy** (surf), now the map surf mode starts on, and **Cobblestone**.

## Servers
- **Testing server alone:** `install.sh --testing-only` sets up just the testing server, on the normal port, and keeps it running all the time on the newest testing build or production release, whichever is newer. Both servers stay the default (`--both` to go back); `--no-testing` is still production only.
- **OFFICIAL tag:** the official Quark servers (production and testing) show a gold OFFICIAL tag in the server list and are listed first. It goes by the server's address, so another server can't get it by putting "official" in its name.

## Modes
- **New mode: POWER.** Each team shares one health pool, 2500 for every player on it, and nobody dies. An enemy's hits come off your team's pool instead of your health. When a team's pool runs out it loses; when the 10 minutes are up, the fuller pool wins. Your own rockets and the world (lava, acid, falls) take nothing from the pool; falling out of the map still respawns you. Someone who joins mid-match adds their 2500. The score box shows both pools, and the bar under it shows yours. Played on Bastion, Orbital and Lodestone. **The network protocol is now 54**, so games and servers update together.

- **Neutral payload's round clock** is now 2 minutes for every player on the teams, counted when the round starts: 4 minutes in a 1v1, 16 in a 4v4 (it was 5 minutes whatever the size).

## Weapons
- **Rockets speed up.** The rocket launcher's rocket now gains 4 m/s every second it flies, on top of its 48 m/s launch speed (52 m/s after a second, 68 m/s at the end of its 5 s life), and keeps speeding up after a rubber bounce. The lob (alt-fire) is unchanged. Bots lead for it, and rocket riding pulls you along at its new speed. **The network protocol is now 55**, so games and servers update together; older demos still play.
- **Rockets speed up much faster.** The rocket still leaves the launcher at 48 m/s but now gains 16 m/s every second: 64 m/s after one second, 80 m/s after two, 128 m/s at the end of its 5 s life (about 440 m). The lob (alt-fire) is unchanged at 33.6 m/s. **The network protocol is now 58**, so games and servers update together; older demos still play.

## Map checks
- **The walk check sees ramps.** The map tests' "can players get from every spawn to every objective" check now walks up wedges, corners and cut slopes up to 45 degrees (the steepest you can walk). Before, every slope counted as a solid block, so routes up a ramp (Mine's haul road and lava ramp, QK7's helix ramps) were reported missing or found only the long way round.

## Bots
- **Bomb bots use every site.** The attackers went for the first bomb site every round, so a site further away (Datacenter's B) never saw them. They now take the sites in turn, round by round. The defenders still guard one site each.

## Map format 11: spawns for one mode
- **A map can give a mode its own spawns**, e.g. a koth hill in a bomb map: `spawn koth-a` / `spawn koth-b` spawns are used only in king of the hill (and koth starts on them when the map has them), never in other modes. Any mode but race and multi point can have a set (`ffa-any`, `bomb-a`, ...). The map editor sets it on a spawn (MODE SET; a white cap marks one). **Maps are now format 11**: older maps convert by themselves, but an older game can't open a map that uses the new spawns.

## Settings: page numbers
- **Every settings tab with more than one page shows its page numbers at the bottom**, with < and > at the ends: click a number to go straight to that page, or the arrows (or the Left / Right arrow keys) for the previous / next page. The arrows stop at the first and last page. The MORE buttons are gone; BACK now leaves settings from any page (Esc still goes back to the tab's first page).
- **Fixed: settings rows drawn over the bottom buttons.** On Windows, VIDEO + AUDIO's MAP COLOR and RESET MAP COLOR sat on top of BACK and MORE. Pages that were too full have moved rows instead: FLAT MAP (FULLBRIGHT, MAP COLOR) is now on VIDEO + AUDIO page 2, EXCLUSIVE AUDIO (Windows) on page 6 under OUTPUT, and SMOOTHING (INTERP) from GAMEPLAY to VIDEO + AUDIO page 7 under NETWORK. SEARCH finds them all.

## Settings: SEARCH tab
- **SEARCH is a settings tab of its own** (the last one): every setting in the game in one list, three wide, scrolled with the mouse wheel or the scrollbar, each with a small tag saying which tab and page it lives on. Type in the field at the top and the list narrows to the settings whose name, values, hint or a few extra keywords have every word you typed: "screen" finds the display mode, fullscreen size, window size, render scale and your monitors; "fov" the field of view and the zoom FOVs. The settings work right there in the list. **Ctrl+F** (or **/**) opens it from any tab; **Esc** clears what you typed. It replaces the small search box at the top right.

## Bots
- **Team order no longer matters:** every bot now decides what to do on the same picture of the world each tick, and who moves first rotates every tick, so it doesn't matter which team or player joined first. At round start, both teams' spawns are picked without either team dodging the other's fresh spawns.

## Bomb
- **Spawns go by role:** the attackers always start at the map's attack spawns and the defenders at the defence spawns, so at halftime the teams swap ends as well as roles. Before, each team kept its own end, so a map built with the attack 1 s closer to a site than the defence gave the same team the short way all match.

## Glass
- **Sloped glass draws as its slope:** glass on a wedge, corner or cut box used to draw as its whole box (a glass dome looked like four slabs floating over the roof). It now draws, and breaks into shards, as its real shape. The same goes for see-through looks and breakable blocks on a slope.

## Race leaderboard: one total over every race map
- **The race board is now a total:** your best time trial on every race and surf map (Particle Accelerator, Jungle Ruins, Rooftop Run, Surfy) added up, fastest first. A map you haven't run counts at its default time, so playing every map pays. Each map's own board is still there.
- **Surf maps have time trials too**, played with surf's rules; their runs go to the boards like race runs.
- **LEADERBOARD:** the RACE TOTAL column, your total and your time on each map under it, and TRIAL MAP to pick which map PLAY TIME TRIAL starts. MY RACE STATS shows what each map adds to your total.
- Viaduct is no longer a ranked race map.

## Maps: the new pool, and legacy maps
- **The old maps are retired.** Temple, Bastion, Hamlet, Lodestone, Viaduct and the rest of the pool before 1.19.6 are gone from the map lists; the new maps (and q's Surfy and Cobblestone) replace them. The menu background is now Castle, and hosting starts on Sewer.
- **LEGACY MAPS** (settings > GAMEPLAY > MAPS, off by default): turn it on to list the old maps again, marked (legacy). They're never ranked. A server or demo that asks for one by name still loads it.
- **Your maps folder is tidied once:** untouched copies of the old official maps that older versions left in your maps folder (they hid the shipped map of that name) move to `maps\legacy\`. Your own and edited maps and downloaded maps aren't touched.
- **Ranked:** qk1-qk8 are ranked on all three ladders. Each ladder starts on the map it's designed for: vanilla 1v1 on Greenhouse, laser vampire on Dojo, instagib on Spire. A ranked server still set to an old ranked map (e.g. `--map temple`) starts on its ladder's map instead.

## Hide and seek
- **The hiders' head start is 5 seconds** (was 30).

## Race and surf
- **No time limit:** a round runs until someone finishes, and the HUD counts your run time up.
## Rockets
- **Rocket splash damage works online again:** since 1.19.3 only a direct rocket hit did damage on a server; a rocket landing on the floor or a wall next to someone did nothing and showed no hit marker. Splash now hurts as it did before.
- **Map votes:** the old (legacy) maps can't be voted unless the server turns them on.
- **Servers:** a new server starts as unranked laser vampire 1v1 on Dojo.
- **Solar System** (surf): 15 stages, Sun to Pluto, each one built from q's own design. It's on the ranked race total (10 min until you've run it).
- **Weapon switch:** holding the scope (right click) while switching from the rifle to the laser no longer stops the laser shooting until you let go.
