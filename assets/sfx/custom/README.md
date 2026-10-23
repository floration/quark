# Your own sound effects

Put a file named after the sound into the folder of the kit it belongs to, then run `python3 tools/import_sfx.py`
(needs ffmpeg) and rebuild. Your file replaces that sound in that kit. Anything you don't supply keeps its current sound.

Folders: `default/`, `heavy_metal/`, `magical/`, `retro/`, `mouth/` (the SOUND PACK setting's five kits).
Formats: .wav, .ogg, .mp3 or .flac, any sample rate. The importer makes it mono, trims the silence at the start,
cuts it at the length in the table below (with a short fade) and normalises it. In the other kits it is matched to the DEFAULT sound's loudness.
Example: `heavy_metal/ROCKET.wav`.

Keep them short and starting instantly (no silence before the sound): they play the moment the thing happens.

| File name | When it plays | Length guide |
|---|---|---|
| HIT | you hit someone for under 10 damage (the light hitsound) | very short, < 0.2 s |
| KILL | you killed someone | < 0.6 s |
| HURT | you took damage | < 0.3 s |
| STEP | a footstep (yours and others', in 3D) | very short, < 0.15 s |
| JUMP | a jump | < 0.2 s |
| LAND | landing from a fall | < 0.3 s |
| RIFLE | rifle shot | < 0.3 s (it's automatic: many per second) |
| ROCKET | rocket launcher fired | < 0.7 s |
| KNIFE | knife swing / throw | < 0.35 s |
| SNIPER | sniper shot | < 1 s |
| LASER | laser (lightning gun) - plays repeatedly while firing | very short, < 0.1 s |
| SHOTGUN | shotgun blast | < 0.6 s |
| STICKY | sticky bomb launched | < 0.4 s |
| CROSSBOW | crossbow bolt fired | < 0.5 s |
| BUBBLE | bubble gun shot | < 0.3 s |
| SYRINGE | syringe used / thrown | < 0.4 s |
| EXPLOSION | a rocket / sticky explodes (3D) | < 1 s |
| STICKY_ATTACH | a sticky bomb sticks to something | < 0.2 s |
| BOLT_HIT | a crossbow bolt hits | < 0.3 s |
| DASH | dash | < 0.4 s |
| GRAPPLE_FIRE | grappling hook fired | < 0.3 s |
| GRAPPLE_HIT | grappling hook catches | < 0.3 s |
| DEATH | you died | < 0.7 s |
| STREAK | someone else's killstreak | < 0.6 s |
| TYPING | someone typing in chat (one key click) | very short, < 0.06 s |
| CHECKPOINT | race: you passed a checkpoint | < 0.35 s |
| FINISH | race: you crossed the finish | < 0.8 s |
| HIT_MED | you hit someone for 10 to 24 damage (heavier and louder than HIT) | short, < 0.25 s |
| HIT_HEAVY | you hit someone for 25 to 50 damage | < 0.35 s |
| HIT_BRUTAL | you hit someone for more than 50 damage (the heaviest, loudest) | < 0.5 s |

That's 30 sounds per kit, 150 for all five. (The announcer's voice lines are separate: assets/voice/, tools/import_voice.py.)
