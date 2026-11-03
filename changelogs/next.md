# Next (testing builds since 1.19.7)

What's on `testing` since production 1.19.7. The release PR renames this file to `changelogs/<VERSION>.md`.

## Tutorial
- **MAIN MENU > TUTORIAL:** every movement tech, one lesson each. A ghost bot shows the tech first while the camera follows it, with the keys it presses lit under your own key names. Then it's your turn in a lane beside it while it keeps repeating the move. Each lesson has short steps, a goal and a bar that fills as you do it.
- 17 lessons: walking, AD strafing, the dash, sideways dashes, super, hyper, super chaining, the dash slide, the wall bounce, the grapple, grapple walking, rocket jumps, sticky jumps, and four marked **KEY TECH**:
  - **The launch**: grapple along the floor to 24 m/s, then look straight down and jump, dash and fire a rocket together. You fly about 50 m up.
  - **Grapple a player**: hook the PARTNER, a second bot running across the bay. Your hook latches on and pulls you both together.
  - **Dash out of a grapple**: the PARTNER hooks you from the far end and you're pulled toward it. Any dash breaks free at once.
  - **The rocket grapple**: fire a rocket and hook it as it leaves: it carries you along.
- JUMP (or a click) skips the demo, R starts the lesson over and ENTER skips to the next lesson. Esc opens LESSONS (pick any lesson; the ones you've done get a green mark), WATCH AGAIN and SETTINGS. Falling in the lava puts you back at the lesson's start, and nothing does damage in the tutorial.
- The first time you start the game it offers the tutorial once (START TUTORIAL or NOT NOW).
- `quark --tutorial [LESSON]` starts it straight away, for example `quark --tutorial launch`.
- The main menu's HOST GAME button is now half width, with TUTORIAL beside it.
