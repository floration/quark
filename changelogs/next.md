# Next

## Netcode
- **Your hits show at once at any ping:** the hit flash, hitsound and damage number appear the moment you land a hit, instead of a round trip later when the server answered. The server's answer no longer shows them a second time; only damage the server adds on its own (stickies, your own blasts) still shows when it answers.
- **Your rockets explode on the wall at high ping:** your own rocket, bolt or knife copy now waits as long as your ping needs for the server's, so a rocket no longer sits on the wall waiting for the server's explosion.
- **Stickies, thrown knives and syringes fly from your click too,** like rockets and bolts: a thrown knife's hit and a sticky blast's damage are yours at once (with the hit flash, hitsound and number), and a sticky shows stuck on the wall straight away. Network protocol 70: this testing build doesn't play with 1.19.8 servers.

## Latency
- **F3 frame pacing graph:** under the mouse-to-photon graph, F3 now draws the last 5 seconds of frame times, so uneven frames show as spikes. It's scaled to your average frame time ±50%, and widens while a slower or faster frame is on it. Nothing is measured while F3 is off.
- F3's latency graph says GPU WAIT (LATENT SYNC) instead of GPU WAIT (ULTRA) when Latent Sync is on and LOW LATENCY isn't ULTRA.

## Menus
- The version in the menus' bottom left (and F3) shows the full build, e.g. `quark-1.19.8-a4bfca60`, with `-dirty` on a build of uncommitted changes.
