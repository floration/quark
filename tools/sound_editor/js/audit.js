/* ===== AUDIT DATA (docs: the 1.18.3 sound audit) ======================================================
   Where every sound plays (volume, pitch, 2D/3D, reuses), the weak spots, and the events with no sound yet.
   Kept in one place so the director can update it without touching the editor's code. */
(function () {
  'use strict';
  const Q = window.Q = window.Q || {};

  // the game's mixer (audio.cpp): master default 0.6 (config.h), soft limit x/(1+0.3|x|)
  Q.GAME = { master: 0.6, limiter: 0.3, stepEvery: 2.4 / 9.5, crouchStepEvery: 0.6, dashStepEvery: 0.1 };
  // weapon refires (gamedefs.h:249), seconds
  Q.REFIRE = { RIFLE: 0.75 * 2 / 9.9, ROCKET: 1.2, KNIFE: 0.75, SNIPER: 1.5, LASER: 0.05 /* the sound: 20 Hz */, SHOTGUN: 0.75,
    STICKY: 0.75, CROSSBOW: 2.0, BUBBLE: 0.2, SYRINGE: 1.0, LOCATOR: 5.0 };

  /* Each sound's call sites: what plays it, 2D/3D, volume, pitch ('j97' = 0.97..1.03 jitter, 'j90' = 0.90..1.09),
     whose pack. reuse: true marks a sound standing in for another event. */
  Q.USES = {
    HIT: [{ what: 'you hit for < 10 (laser ticks 20/s)', d: '2D', vol: 0.9 }],
    HIT_MED: [{ what: 'you hit for 10 to 24', d: '2D', vol: 0.9 }],
    HIT_HEAVY: [{ what: 'you hit for 25 to 50', d: '2D', vol: 0.9 }],
    HIT_BRUTAL: [{ what: 'you hit for > 50', d: '2D', vol: 0.9 }],
    KILL: [{ what: 'you killed someone (on top of the hitsound)', d: '2D', vol: 0.9 }],
    HURT: [{ what: 'you took damage (no rate limit: ~40/s under a laser)', d: '2D', vol: 0.7 },
      { what: 'someone else took damage (max 5/s)', d: '3D', vol: 0.8, pack: 'theirs' },
      { what: 'rock-paper-scissors: you are out', d: '2D', vol: 0.8, reuse: true }],
    STEP: [{ what: 'your step every 2.4 m (0.253 s running)', d: '2D', vol: 0.55, pitch: 'j90' },
      { what: 'others\' steps (silent in the air / on stairs)', d: '3D', vol: 0.8, pitch: 'j90', pack: 'theirs' }],
    JUMP: [{ what: 'your jump (others\' jumps are silent)', d: '2D', vol: 0.5 }],
    LAND: [{ what: 'your landing, vol = clamp(-vy/20, 0.3, 1): a 1.22 m jump is 0.37', d: '2D', vol: 0.37 }],
    RIFLE: [{ what: 'your shot', d: '2D', vol: 0.8, pitch: 'j97' }, { what: 'their shot', d: '3D', vol: 1.0, pitch: 'j97', pack: 'theirs' }],
    ROCKET: [{ what: 'your shot (lob too)', d: '2D', vol: 0.8, pitch: 'j97' }, { what: 'their shot', d: '3D', vol: 1.0, pitch: 'j97', pack: 'theirs' }],
    KNIFE: [{ what: 'your swing / throw', d: '2D', vol: 0.8, pitch: 'j97' }, { what: 'their swing', d: '3D', vol: 1.0, pitch: 'j97', pack: 'theirs' }],
    SNIPER: [{ what: 'your shot', d: '2D', vol: 0.8, pitch: 'j97' }, { what: 'their shot', d: '3D', vol: 1.0, pitch: 'j97', pack: 'theirs' }],
    LASER: [{ what: 'your beam, 20 one-shots a second', d: '2D', vol: 0.6, pitch: 'j97' }, { what: 'their beam, 20/s', d: '3D', vol: 0.7, pack: 'theirs' }],
    SHOTGUN: [{ what: 'your blast', d: '2D', vol: 0.8, pitch: 'j97' }, { what: 'their blast', d: '3D', vol: 1.0, pitch: 'j97', pack: 'theirs' }],
    STICKY: [{ what: 'your launch', d: '2D', vol: 0.8, pitch: 'j97' }, { what: 'their launch', d: '3D', vol: 1.0, pitch: 'j97', pack: 'theirs' }],
    CROSSBOW: [{ what: 'your bolt', d: '2D', vol: 0.8, pitch: 'j97' }, { what: 'their bolt', d: '3D', vol: 1.0, pitch: 'j97', pack: 'theirs' }],
    BUBBLE: [{ what: 'your shot (a 5-15 bubble stream is ONE sound)', d: '2D', vol: 0.8, pitch: 'j97' },
      { what: 'their shot', d: '3D', vol: 1.0, pitch: 'j97', pack: 'theirs' },
      { what: 'a bubble POPS: the fire sound reused (no gate: a stream gives 5-15 at once)', d: '3D', vol: 0.7, pitch: 1.7, pack: 'owner', reuse: true }],
    SYRINGE: [{ what: 'your use / throw', d: '2D', vol: 0.8, pitch: 'j97' }, { what: 'theirs', d: '3D', vol: 1.0, pitch: 'j97', pack: 'theirs' },
      { what: 'you were healed by a thrown syringe', d: '2D', vol: 0.8, pitch: 1.3, reuse: true }],
    EXPLOSION: [{ what: 'a rocket / sticky explodes (no gate: 6 stickies = 6 at once)', d: '3D', vol: 1.25, pack: 'owner' }],
    STICKY_ATTACH: [{ what: 'a sticky sticks', d: '3D', vol: 0.8, pack: 'owner' },
      { what: 'an enemy sticky lands on YOU (warning)', d: '2D', vol: 1.0, pitch: 1.6, reuse: true }],
    BOLT_HIT: [{ what: 'a crossbow bolt lands', d: '3D', vol: 0.8, pack: 'owner' },
      { what: 'rock-paper-scissors reveal', d: '2D', vol: 0.9, pitch: 0.9, reuse: true }],
    DASH: [{ what: 'your dash (others\' dashes are silent)', d: '2D', vol: 0.8 }],
    SUPER: [{ what: 'your super (dash jump)', d: '2D', vol: 0.9 }],
    HYPER: [{ what: 'your hyper (super dash)', d: '2D', vol: 0.9 }],
    GRAPPLE_FIRE: [{ what: 'your hook latches (with GRAPPLE_HIT; a miss is silent)', d: '2D', vol: 0.7 }],
    GRAPPLE_HIT: [{ what: 'your hook, 3D at the anchor (30 m away: 0.225)', d: '3D', vol: 0.9 },
      { what: 'their hook latches', d: '3D', vol: 0.9, pack: 'theirs' },
      { what: 'the locator FOUND someone', d: '2D', vol: 0.6, pitch: 1.4, reuse: true }],
    LOCATOR: [{ what: 'your locator ping (every 5 s)', d: '2D', vol: 0.8 },
      { what: 'the seeker\'s / tagger\'s ping, heard by everyone else (even out of sight)', d: '3D', vol: 1.0, pack: 'theirs' }],
    DEATH: [{ what: 'you died', d: '2D', vol: 0.8 }],
    STREAK: [{ what: 'someone else\'s 3/5/7/10 streak', d: '2D', vol: 0.7 }, { what: 'ranked queue filled', d: '2D', vol: 1.0, reuse: true }],
    TYPING: [{ what: 'someone typing (max 4/s)', d: '3D', vol: 0.6, pitch: 'j90', pack: 'theirs' }, { what: 'a hidden typist', d: '2D', vol: 0.4 },
      { what: 'rock-paper-scissors pick', d: '2D', vol: 0.9, pitch: 0.8, reuse: true }],
    CHECKPOINT: [{ what: 'race split (also a MISSED checkpoint)', d: '2D', vol: 0.9 }, { what: 'rock-paper-scissors new throw', d: '2D', vol: 0.7, pitch: 0.8, reuse: true }],
    FINISH: [{ what: 'race finish', d: '2D', vol: 0.9 }, { what: 'rock-paper-scissors win', d: '2D', vol: 0.6, pitch: 1.2, reuse: true }],
  };
  // weak / wrong uses of existing sounds (shown on the sound's row and in the editor)
  Q.WEAK = {
    BUBBLE: 'The pop reuses the fire sound at pitch 1.7, ungated (a stream = 5-15 pops at once). Suggested: its own BUBBLE_POP.',
    GRAPPLE_HIT: '"Locator found" reuses it at pitch 1.4. A far own anchor is barely audible (3D at the anchor).',
    GRAPPLE_FIRE: 'Always plays together with GRAPPLE_HIT (the hook is instant); a miss is silent.',
    STREAK: '"Ranked queue filled" reuses it. Suggested: MATCH_FOUND.',
    SYRINGE: '"Healed by a syringe" reuses it at pitch 1.3. Suggested: HEAL.',
    EXPLOSION: 'No gate: 6 stickies give 6 explosions at volume 1.25 at once.',
    STEP: 'Same volume when crouching; others\' steps drop on any height change (stairs) and in the air.',
    HURT: 'Your own hurt has no rate limit (about 40/s under a laser).',
    CHECKPOINT: 'A missed checkpoint plays the same ding. Race practice plays none.',
    LAND: 'Only your own landings make a sound.',
    JUMP: 'Only your own jumps make a sound.',
    DASH: 'Others\' dashes / supers / hypers are silent.',
  };
  Q.WEAK_LINES = {
    HILL_TAKEN: 'Repeats on every hill owner change (stepping on / off an empty hill).',
    HILL_LOST: 'Repeats on every hill owner change.',
  };

  /* Events with no sound yet (audit section B). Each needs a game hook before it can be heard; the owner can design
     the sound now: it exports as sounds/<kit>/<NAME>.wav (voices/<voice>/<NAME>.wav for a voice line).
     pri: P1 information, P2 feel, P3 polish. reuse: no new sound needed, a game fix that reuses existing ones. */
  Q.MISSING = [
    // combat and weapons
    { area: 'Combat', name: 'HEADSHOT', pri: 'P1', d: '2D', when: 'you dealt a headshot (a rifle head hit is the same light tick as a body hit)', note: 'a bright metallic "tink" layered on the tier, < 0.15 s' },
    { area: 'Combat', name: 'HURT_HEAD', pri: 'P2', d: '2D', when: 'you took a headshot', note: 'a sharper, higher HURT, < 0.2 s' },
    { area: 'Combat', name: 'SWITCH', pri: 'P2', d: '2D own, 3D others', when: 'weapon switch / draw', note: 'a short mechanical clack, 0.1 s' },
    { area: 'Combat', name: 'WEAPON_BACK', pri: 'P2', d: '2D', when: 'a thrown knife / syringe is back', note: 'a soft clink, 0.15 s' },
    { area: 'Combat', name: 'KNIFE_HIT', pri: 'P2', d: '3D', when: 'a thrown knife / syringe sticks in a wall (also a knife swing hitting a wall, P3)', note: 'a woody thunk, 0.15 s' },
    { area: 'Combat', name: 'DETONATE', pri: 'P3', d: '2D', when: 'stickies detonated (alt-fire)', note: 'a remote-trigger beep, 0.08 s' },
    { area: 'Combat', name: 'STICKY_ARM', pri: 'P3', d: '3D / 2D', when: 'a sticky arms (0.35 s), or the oldest of 6 is removed', note: 'a tiny blip' },
    { area: 'Combat', name: 'STICKY_BEEP', pri: 'P2', d: '2D', when: 'a sticky is on you: a beep every 0.5 s', note: 'a short beep' },
    { area: 'Combat', name: 'ZOOM', pri: 'P3', d: '2D', when: 'scope in / out', note: 'a lens click, 0.06 s' },
    { area: 'Combat', name: 'DRYFIRE', pri: 'P3', d: '2D', when: 'firing during holdfire, after a switch, or while a weapon is away', note: 'a dull click' },
    { area: 'Combat', name: 'LASER_LOOP', pri: 'P3', d: '3D', when: 'the laser beam (today 20 overlapping one-shots a second; the mixer has no loops yet)', note: 'a seamless hum loop' },
    { area: 'Combat', name: 'BUBBLE_POP', pri: 'P3', d: '3D', when: 'a bubble pops (today the fire sound at pitch 1.7)', note: 'a wet pop, 0.12 s; gate a stream\'s 5-15 pops' },
    { area: 'Combat', name: 'HOOKED', pri: 'P1', d: '2D', when: 'an enemy grapple hooked you (dash to break free)', note: 'a chain-snag jolt, 0.25 s' },
    { area: 'Combat', name: 'GRAPPLE_MISS', pri: 'P2', d: '2D', when: 'your grapple missed', note: 'a dry whiff' },
    { area: 'Combat', name: 'GRAPPLE_RELEASE', pri: 'P3', d: '2D', when: 'your grapple released / ready again', note: 'a retract zip' },
    { area: 'Combat', reuse: 'EXPLOSION, RIFLE...', pri: 'P1', d: '3D', when: 'players > 20 m away and out of sight are silent (no shots, steps, explosions)', note: 'game fix: a coarse EV_SOUND for far gunfire / explosions' },
    { area: 'Combat', reuse: 'weapon sounds', pri: 'P1', d: '2D', when: 'demo playback: the POV player\'s shots are silent', note: 'game fix' },
    { area: 'Combat', reuse: 'DASH / SUPER / HYPER', pri: 'P1', d: '3D', when: 'other players\' dash / super / hyper are silent', note: 'game fix: needs the state in the snapshot' },
    { area: 'Combat', reuse: 'JUMP / LAND', pri: 'P1', d: '3D', when: 'other players\' jump / land are silent (airborne remote players)', note: 'game fix' },
    // movement and player state
    { area: 'Movement', name: 'DASH_READY', pri: 'P2', d: '2D', when: 'a dash charge refilled / cooldown over', note: 'a soft rising blip, 0.1 s' },
    { area: 'Movement', name: 'BOUNCE', pri: 'P2', d: '2D', when: 'dash wall bounce', note: 'a springy thud' },
    { area: 'Movement', name: 'LADDER', pri: 'P2', d: '2D / 3D', when: 'climbing a ladder: a rung step every 0.4 m (silent today)', note: 'a metal rung step' },
    { area: 'Movement', name: 'IMPACT', pri: 'P3', d: '2D', when: 'a hard wall hit at speed', note: 'a body thud' },
    { area: 'Movement', name: 'SPLASH', pri: 'P3', d: '2D', when: 'falling into the void / water / lava', note: 'a splash or fall whoosh' },
    { area: 'Movement', name: 'SPAWN', pri: 'P2', d: '2D', when: 'you spawn / respawn', note: 'a soft whoosh-in, 0.4 s' },
    { area: 'Movement', name: 'LOW_HEALTH', pri: 'P2', d: '2D', when: 'under 25 hp: a heartbeat every 0.8 s', note: 'a heartbeat' },
    { area: 'Movement', name: 'HEAL', pri: 'P3', d: '2D', when: 'syringe heal (also EV_HEALED instead of SYRINGE at pitch 1.3)', note: 'a rising sparkle' },
    { area: 'Movement', reuse: 'STEP (x0.4)', pri: 'P2', d: '2D / 3D', when: 'crouch walking uses the full step volume', note: 'game fix: scale the volume' },
    // game modes
    { area: 'Modes', name: 'BOMB_EXPLODE', pri: 'P1', d: '3D', when: 'the bomb explodes (silent today: the round just ends)', note: 'a big long boom, 2 s (or EXPLOSION at 2.0, pitch 0.6)' },
    { area: 'Modes', name: 'BOMB_TICK', pri: 'P1', d: '3D', when: 'bomb planted: a beep that speeds up (interval max(0.15, timer/20))', note: 'a short beep' },
    { area: 'Modes', name: 'PLANT', pri: 'P1', d: '3D', when: 'a plant in progress (a click every 0.5 s or a loop)', note: 'arming clicks' },
    { area: 'Modes', name: 'DEFUSE', pri: 'P1', d: '3D', when: 'a defuse in progress: you must hear an enemy defusing', note: 'wire / tool clicks' },
    { area: 'Modes', name: 'BOMB_PICKUP', pri: 'P2', d: '2D team', when: 'bomb dropped / picked up / returned', note: 'a pickup chime' },
    { area: 'Modes', name: 'CONTESTED', pri: 'P1', d: '2D', when: 'the hill is contested', note: 'a two-tone alarm, rate-limited' },
    { area: 'Modes', name: 'CAPTURING', pri: 'P1', d: '2D', when: 'a point is being captured / contested', note: 'a ticking that rises with progress' },
    { area: 'Modes', name: 'CART', pri: 'P2', d: '3D', when: 'payload moving / stopped / rolling back', note: 'a rolling loop or creak' },
    { area: 'Modes', name: 'CHECKPOINT', kind: 'voice', pri: 'P1', d: '2D', when: 'payload checkpoint reached (or reuse POINT_TAKEN / POINT_LOST)', note: 'announcer: "checkpoint"' },
    { area: 'Modes', name: 'TAGGED', pri: 'P1', d: '2D', when: 'tag: you are it / you tagged someone (no sound at all today)', note: 'a sting, plus a hit for the tagger' },
    { area: 'Modes', name: 'RELEASE', pri: 'P2', d: '2D', when: 'tag freeze over (2 s)', note: 'a release cue' },
    { area: 'Modes', name: 'SPOTTED', pri: 'P2', d: '2D', when: 'hide and seek: the locator spotted you', note: 'a warning ping' },
    { area: 'Modes', name: 'CHECKPOINT_MISS', pri: 'P2', d: '2D', when: 'race: a missed checkpoint (plays the normal ding today)', note: 'a low buzz' },
    { area: 'Modes', name: 'FIRE', kind: 'voice', pri: 'P1', d: '2D', when: 'holdfire over (dojo, ranked): count down THREE / TWO / ONE then this', note: 'announcer: "FIGHT"' },
    { area: 'Modes', name: 'COUNTDOWN_TICK', pri: 'P2', d: '2D', when: 'warmup "MATCH STARTS IN 5"', note: 'a tick per second' },
    { area: 'Modes', name: 'SUDDEN_DEATH', kind: 'voice', pri: 'P2', d: '2D', when: 'stalemate drain starts', note: 'announcer: "sudden death" (or a siren)' },
    { area: 'Modes', name: 'NOTICE', pri: 'P3', d: '2D', when: 'duel modifier this life / tourney: your match is next', note: 'a notice chime' },
    { area: 'Modes', name: 'MATCH_FOUND', pri: 'P2', d: '2D', when: 'ranked queue filled (reuses STREAK today)', note: 'a distinct chime' },
    { area: 'Modes', name: 'RANK_UP', pri: 'P3', d: '2D', when: 'rating went up', note: 'a rising sting' },
    { area: 'Modes', name: 'RANK_DOWN', pri: 'P3', d: '2D', when: 'rating went down', note: 'a falling sting' },
    { area: 'Modes', reuse: 'CHECKPOINT / FINISH', pri: 'P1', d: '2D', when: 'race practice gives no checkpoint sounds', note: 'game fix' },
    // UI and social
    { area: 'UI', name: 'CHAT', pri: 'P2', d: '2D', when: 'a chat / server message arrived', note: 'a soft blip' },
    { area: 'UI', name: 'VOTE', pri: 'P2', d: '2D', when: 'a vote started', note: 'a ballot chime' },
    { area: 'UI', name: 'JOIN', pri: 'P3', d: '2D', when: 'a player joined', note: 'a soft up-blip' },
    { area: 'UI', name: 'LEAVE', pri: 'P3', d: '2D', when: 'a player left', note: 'a soft down-blip' },
    { area: 'UI', name: 'UI_HOVER', pri: 'P3', d: '2D', when: 'menu: button hover (menus have no audio)', note: 'a very quiet tick' },
    { area: 'UI', name: 'UI_CLICK', pri: 'P3', d: '2D', when: 'menu: click / toggle / cycle; editor actions', note: 'a click' },
    { area: 'UI', name: 'UI_BACK', pri: 'P3', d: '2D', when: 'menu: back', note: 'a lower click' },
    { area: 'UI', reuse: 'RIFLE + HIT', pri: 'P2', d: '2D', when: 'VOLUME slider / SOUND PACK cycle have no preview', note: 'game fix' },
    { area: 'UI', reuse: 'TYPING (low)', pri: 'P3', d: '2D', when: 'your own chat typing is silent', note: 'game fix' },
  ];
})();
