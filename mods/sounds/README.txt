YOUR OWN SOUNDS (no programming needed)

Put a .wav file named after a sound into the default folder here, then start the game:
  mods/sounds/default/<SOUND>.wav   replaces that sound
  mods/sounds/all/<SOUND>.wav       works too (from when there were several sound packs; default/ wins)
The game has one sound kit, DEFAULT. Folders of the old packs (heavy_metal, magical, retro, mouth) are ignored.
Example: mods/sounds/default/ROCKET.wav gives you your own rocket sound.

WAV only: 8, 16 or 24-bit PCM or 32-bit float, any sample rate, mono or stereo.
The game trims silence at the start, cuts it at 3 seconds and matches its loudness to the sound it replaces.
To keep a file's own loudness, list it in sound-levels.txt in this folder, one per line without the extension
(for example default/ROCKET); it is then only turned down if it would clip.
Updates never touch your files here.

SOUND names:
  HIT            you hit someone for under 10 damage (the light hitsound)
  KILL           you killed someone
  HURT           you took damage
  STEP           a footstep
  JUMP           a jump
  LAND           landing from a fall
  RIFLE          rifle shot
  ROCKET         rocket launcher fired
  KNIFE          knife swing / throw
  SNIPER         sniper shot
  LASER          laser (repeats while firing: keep it tiny)
  SHOTGUN        shotgun blast
  STICKY         sticky bomb launched
  CROSSBOW       crossbow bolt fired
  BUBBLE         bubble gun shot
  SYRINGE        syringe used / thrown
  EXPLOSION      rocket / sticky explodes
  STICKY_ATTACH  a sticky bomb sticks to something
  BOLT_HIT       a crossbow bolt hits
  DASH           dash
  GRAPPLE_FIRE   grappling hook fired
  GRAPPLE_HIT    grappling hook catches
  DEATH          you died
  STREAK         someone else's killstreak
  TYPING         someone typing in chat (one key click)
  CHECKPOINT     race: you passed a checkpoint
  FINISH         race: you crossed the finish
  HIT_MED        you hit someone for 10 to 24 damage (heavier, louder)
  HIT_HEAVY      you hit someone for 25 to 50 damage
  HIT_BRUTAL     you hit someone for more than 50 damage (the heaviest, loudest)
