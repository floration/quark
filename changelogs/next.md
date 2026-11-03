# Next (testing builds since 1.19.7)

What's on `testing` since production 1.19.7. The release PR renames this file to `changelogs/<VERSION>.md`.

## Tutorial
- **MAIN MENU > TUTORIAL:** every movement tech, one lesson each. A ghost bot shows the tech first while the camera follows it, with the keys it presses lit under your own key names. Then it's your turn in a lane beside it while it keeps repeating the move. Each lesson has short steps, a goal and a bar that fills as you do it.
- 28 lessons, ten of them marked **KEY TECH** and listed first. Each lesson's numbers are what the game really does, measured in the tests:
  - **The launch**: grapple along the floor to 24 m/s, then look straight down and jump, dash and fire a rocket together. You fly about 50 m up.
  - **Pop**: a super, then a hyper straight down as you land: about 14 m up. **Rocket pop**: the same with a rocket fired the moment the dash goes: about 47 m up, no grapple needed.
  - **Rocket super**: a super, then a rocket straight down: about 33 m/s along, past the dash's 32 m/s cap. **Hook boost**: grapple walking at 24 m/s, a rocket straight down while still hooked: about 40 m/s along.
  - **Sticky stack**: three stickies at your feet set off together: 50 m/s up (one: 22, two: 36).
  - **Bubble launch**: stand still, look straight down and alt-fire the bubble gun: 58 m/s up, about 73 m (about 12 hp in a real match). From a run it throws you along at 25 m/s; a jump first or an enemy within 5 m makes it much weaker.
  - **Grapple a player**: hook the PARTNER, a second bot running across the bay. Your hook latches on and pulls you both together. **Dash out of a grapple**: the PARTNER hooks you and any dash breaks free at once.
  - **The rocket grapple**: fire a rocket and hook it as it leaves: it carries you along.
  - And walking, AD strafing, the dash, sideways dashes, super, hyper, super chaining, the dash slide, the wall bounce, the **ramp dash** (a dash runs up a 60 degree slope you can't walk up), the grapple, the **ceiling hang**, the **swing turn** (hook a pillar at speed to turn round it), grapple walking, the **hook-start chain** (grapple walk to 24 m/s, then hyper every landing: 35 m/s), rocket and sticky jumps and the **fall bounce** (hold jump landing from a big fall: a 15.7 m/s hop and a dash back).
- The lessons spell out two rules: a dash erases a blast's push (fire after the dash, never just before it), and a dash above 32 m/s (a hyper above 36) slows you down to the cap.
- JUMP (or a click) skips the demo, R starts the lesson over and ENTER skips to the next lesson. Esc opens LESSONS (pick any lesson; the ones you've done get a green mark), WATCH AGAIN and SETTINGS. Falling in the lava puts you back at the lesson's start, and nothing does damage in the tutorial.
- The first time you start the game it offers the tutorial once (START TUTORIAL or NOT NOW).
- `quark --tutorial [LESSON]` starts it straight away, for example `quark --tutorial launch`.
- The main menu's HOST GAME button is now half width, with TUTORIAL beside it.

## Info
- INFO > CONTROLS fixed where it was out of date: there is no air drift (a standing jump drifts 2.3 m/s at most), an air dash adds about a third of what you are below 32 m/s, and the syringe heals 5 hp (a throw: 15 hp).
