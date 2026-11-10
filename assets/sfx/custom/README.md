# Your own sound effects

**For now the game has one sound kit, DEFAULT** (owner, 2026-11-10: "for now you can get rid of every sound pack except
for default"). The SOUND PACK setting is gone and `tools/import_sfx.py`'s output (`src/client/sfx_recorded.h`) is no
longer built or shipped, so files put in these folders do nothing until the packs come back. The folders
(`default/`, `heavy_metal/`, `magical/`, `retro/`), the importer and the Kenney clips in `assets/sfx/` stay in git for
that. The MOUTH kit (`mouth/`, the owner's recordings) was removed altogether; its files are in git history.

To replace a DEFAULT sound today, use an override: `assets/overrides/sounds/default/<SOUND>.*` (docs/ASSETS.md).

How this folder worked (and will again): put a file named after the sound into the folder of the kit it belongs to,
then run `python3 tools/import_sfx.py` (needs ffmpeg) and rebuild. Your file replaced that sound in that kit.
Formats: .wav, .ogg, .mp3 or .flac, any sample rate. The importer makes it mono, trims the silence at the start,
cuts it at the length in the table below (with a short fade) and normalises it. In the other kits it is matched to the DEFAULT sound's loudness.
Example: `heavy_metal/ROCKET.wav`.

Files here are always loudness matched. A sound that must keep its own level (levelled in the sound editor) goes in
as an override instead, `assets/overrides/sounds/default/<SOUND>.*`, listed in `assets/overrides/sound-levels.txt`
(one `sounds/default/<SOUND>` a line; docs/ASSETS.md): the game then skips the matching and only keeps its peak at or
under 1.0.

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
| SUPER | you did a super: a dash jump (jump during a dash on the floor) | < 0.4 s |
| HYPER | you did a hyper: a super dash (dash down into the floor and jump) | < 0.5 s |
| LOCATOR | you used the locator (hide and seek / tag): a sonar-like ping | < 0.9 s |
| SIZZLE | a shot gone into lava or acid, and you while acid hurts you: a hiss | < 0.6 s |
| HEADSHOT_TINK | your hit was a headshot: a metallic tink over the hitsound | very short, < 0.2 s |
| HOOKED | someone's grapple caught you and pulls you | < 0.45 s |
| BOMB_TICK | the planted bomb ticks (3D), faster as its fuse runs out | very short, < 0.1 s |
| BOMB_PLANTING | someone planting (3D, at them): one click every 0.3 s while they do | very short, < 0.1 s |
| BOMB_DEFUSING | someone defusing (3D, at the bomb): one click every 0.3 s while they do | very short, < 0.12 s |
| HILL_CONTESTED | king of the hill: the hill became contested (both teams on it) | < 0.5 s |
| CAPTURE_ALARM | multi point: the enemy is capturing your point (as it starts, then every 3 s) | < 0.75 s |
| HOLDFIRE_TICK | the map's holdfire (no firing at the round start): a tick each second, higher and louder as firing is allowed | very short, < 0.1 s |
| CHECKPOINT_WRONG | race: a checkpoint that won't count (out of order, or a run that doesn't count): a buzz | < 0.4 s |
| WEAPON_SWITCH | you switched weapons: two small mechanical clicks | very short, < 0.15 s |
| DRYFIRE | fire pressed with nothing to fire (the holdfire, a thrown knife / syringe not back, the locator's wait): a dry click | very short, < 0.1 s |
| KNIFE_HIT | a thrown knife or syringe lands (3D; the syringe higher) | < 0.2 s |
| BUBBLE_POP | a bubble pops (3D) | short, < 0.15 s |
| GLASS_BREAK | glass breaks (3D; a shot's hole quieter and higher than a pane shattering) | < 0.6 s |
| DEBRIS | rubble (3D): a breakable block broken, a wall coming down, a crumble platform shaking and falling | < 0.85 s |
| DASH_BOUNCE | your dash bounced off a wall: a springy thud | < 0.3 s |
| GRAPPLE_RELEASE | a grapple let go (yours, or someone's in 3D) | < 0.25 s |
| TELEPORT | a teleporter (yours; someone else's at both ends, in 3D) | < 0.7 s |
| SPLASH | a splash into a liquid (quieter and higher out of it; yours and others') | < 0.6 s |
| SPAWN | you respawned: a soft whoosh in | < 0.6 s |
| LOW_HEALTH | your health fell under the low-health line (WARNING BEEP): a heartbeat | < 0.5 s |
| GOAL | ball: a goal, a stadium horn (lower when it's against your side) | < 1.35 s |
| BALL_KICK | ball: the ball struck (a kick, a push, a hard bounce; 3D, louder the harder) | short, < 0.25 s |
| CHAT | someone else's chat message: a soft blip (higher and louder when it names you) | very short, < 0.15 s |
| VOTE | a vote was called | < 0.5 s |
| MATCH_FOUND | ranked: the queue found your match | < 0.75 s |
| UI_CLICK | a menu click (any button, toggle, arrow or list) | very short, < 0.05 s |

That's 61 sounds. (The announcer's voice lines are separate: assets/voice/, tools/import_voice.py.)
