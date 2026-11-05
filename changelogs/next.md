# Next

## Netcode
- **Your hits show at once at any ping:** the hit flash, hitsound and damage number appear the moment you land a hit, instead of a round trip later when the server answered. The server's answer no longer shows them a second time; only damage the server adds on its own (stickies, your own blasts) still shows when it answers.
- **Your rockets explode on the wall at high ping:** your own rocket, bolt or knife copy now waits as long as your ping needs for the server's, so a rocket no longer sits on the wall waiting for the server's explosion.
- **Stickies, thrown knives and syringes fly from your click too,** like rockets and bolts: a thrown knife's hit and a sticky blast's damage are yours at once (with the hit flash, hitsound and number), and a sticky shows stuck on the wall straight away. Network protocol 70: this testing build doesn't play with 1.19.8 servers.

## Latency
- **F3 frame pacing graph:** under the mouse-to-photon graph, F3 now draws the last 5 seconds of frame times, so uneven frames show as spikes. It's scaled to your average frame time ±50%, and widens while a slower or faster frame is on it. Nothing is measured while F3 is off.
- F3's latency graph says GPU WAIT (LATENT SYNC) instead of GPU WAIT (ULTRA) when Latent Sync is on and LOW LATENCY isn't ULTRA.

## Menus
- **Sharp main menu icons:** the icons down the main menu's left strip are drawn as smooth shapes now, so they stay crisp at any resolution instead of looking pixelated; they and the main menu's text are drawn supersampled (as smooth as rendering at 400%). The main screen shows the QUARK wordmark at the top, and the Q in the strip's corner sits centred. The QUARK logo has a glossy arcade look: a lit top half and a drop shadow.
- **Menu fixes from a three-player review** (a total beginner, an FPS player new to Quark, a regular):
  - With no last game, the main menu's big button is **QUICK PLAY**: straight into the official server (the server list is still SERVERS).
  - HOST shows the address your friends join with, and a **COPY** button.
  - Settings opens on CONTROLS (sensitivity) unless REMEMBER LAST TAB is on.
  - Bots in free for all start on Sewer, the map built for it, instead of Cobblestone.
  - DISCORD, INFO and QUIT at the strip's foot have labels.
  - The page title lines up with the top bar; the aim trainer's difficulty row and BACK no longer stick out of the panel, and its "LEADERBOARD: SOON" lines are gone.
- The version in the menus' bottom left (and F3) shows the full build, e.g. `quark-1.19.8-a4bfca60`, with `-dirty` on a build of uncommitted changes.
- **Exclusive fullscreen at a custom FULLSCREEN SIZE:** the picture fills the screen again (it sat cut off in the bottom-left corner), alt-tabbing out gives the desktop its own resolution back (the game minimizes), and closing the game restores it straight away.
- RANKED on the main menu is now a queue: pick a ladder and it joins a ranked game where someone is waiting, or hosts one (listed publicly) so the next player finds you.
- The main menu's side strip (PLAY, PRACTICE, CREATE, RANKS, SETTINGS, DISCORD, INFO, QUIT) now stays on every menu screen, so you can jump anywhere without backing out first.
- QUICK PLAY stays on the PLAY card after your first game (beside RECONNECT) and joins the busiest official server with room; if no official server answers, it starts a game against bots.
- Clearer errors in the menus: the server browser says when it couldn't load the list and offers bots instead, RANKS shows one "couldn't reach the ranking server" message with RETRY, official servers that don't answer say NO RESPONSE, and an empty address box tells you what it's for.
- RANKED's queue now shows a FINDING A RANKED MATCH card with a timer and CANCEL, and tells you when nobody is waiting and it hosts a ranked game for you. The ladder cards say which is 1v1 and which is free for all.
- Every menu screen now has the main menu's top bar: its name top-left (SERVERS, HOST, BOTS, RANKS...) and the QUARK wordmark in the same place, and START is highlighted on HOST and BOTS.
- VIDEO + AUDIO: the graphics PRESET (LOW / MEDIUM / HIGH / COMPETITIVE) is now on page 1, and hovering a page number shows what's on that page.
- Menu keys: 1-5 switch between PLAY, PRACTICE, CREATE, RANKS and SETTINGS, and Enter presses QUICK PLAY on the main menu and START on HOST and BOTS.
- QUICK PLAY now only joins your build's official server (the testing server on testing builds), never a ranked game; when it's full or doesn't answer within 3 s it starts bots and says why. The ranked queue looks for about 20 s (HOST NOW skips the wait), leaves your HOST settings alone and remembers your last ladder. SERVERS starts with an empty address box.
