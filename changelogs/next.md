# Next (testing builds since 1.19.4)

What's on `testing` since production 1.19.4. The release PR renames this file to `changelogs/<VERSION>.md`.

## Player names
- **New players are numbered:** a new player's default name is now "player" plus their player number (the 24th player to play is player24) instead of plain "player". Players still named plain "player" get theirs too. A name you typed is never changed.

## 1v1
- **1v1 line:** on a 1v1 server with someone waiting, the line moves on every death: the loser goes to the back, the next in line plays the winner. Kills carry over across turns (the scoreboard keeps everyone's running total) and the first to 11 kills wins the match; then a new match starts straight away. Ranked rates each duel on its own. With only two players, matches are first to 5 rounds as before. Joining a full 1v1 puts you in line (no more "let them in" vote). Server owners: `--rotate-kills N` changes the 11, `--rotate match` brings back the old rule (a whole match, then winner stays).

## 3D VAMP
- **New 1v1 ruleset, 3D VAMP** (HOST > MODE > 1V1, or the vote menu's PLAYER BASED > 1V1): fly in zero gravity like creative mode in Minecraft. W/S/A/D move you on the level, MOVE UP (space) and MOVE DOWN (left shift) straight up and down, all at run speed with a quick stop when you let go. No jump, dash or grapple. Lasers only, hits don't push anyone, and vampire healing starts at 100% of your damage each round and falls to 50% over 30 s. Not ranked.
- **New map, Black Hole**: a lone flat asteroid in space with no cover, a black hole eating the galaxy beside it. Nobody can fire for the first 3 s of a round. Fly too far out and you're lost.
- **3D FLYING is a modifier:** turn it on for any mode and map (the vote's MODIFIERS page, or a custom mode's GENERAL page). 3D VAMP stays as the 1v1 ruleset that has it on.
- **Flying moves like running:** the same acceleration and friction as on the ground, in every direction, up and down too.
- **Snap tap for flying:** with OPPOSITE KEYS: LAST WINS (CONTROLS > MORE 2), holding MOVE UP and MOVE DOWN together goes the way of the last one pressed, as left + right do.
- **New keys**: MOVE UP and MOVE DOWN (CONTROLS > MORE KEYS), and VERTICAL KEYS (CONTROLS > MORE 2): ONE KEY flies up while MOVE UP is held and down when it isn't.

## Aim trainer
- **AIM TRAINER** on the main menu: 36 scenarios of 30 s in 3D STATIC (look and shoot) and 2D (the view stays still, your mouse moves the crosshair), in CLICKING (rifle, one shot a click), SWITCHING and TRACKING (laser), each in EASY / MEDIUM / HARD. Your best, last score and plays show on each scenario's card; a leaderboard comes later. 2D is drawn flat (no 3D at all) with the crosshair moving at an even speed. ESC pauses a run (RESUME, SETTINGS, RESTART, TRAINER MENU) and F3 / F4 work in the trainer.

## Servers
- The testing server now always starts again when a new testing build comes out (it could stay down after a production release).

## Menus
- **JOIN GAME:** the pause menu of a 1v1 or duel shows one JOIN GAME button (it seats you, or puts you in line) instead of JOIN TEAM A / B; free for all modes say JOIN GAME instead of PLAY.
- Spectating a 1v1 or duel: clicking now cycles between both players (it was stuck on one of them).
- 1v1 line: a drawn round (both die at once) now moves the line too (before, the same two played again).
- GAMEPLAY > MORE > FORCE PLAYER MODELS: a new TEAM MODEL draws every teammate as one model, next to ENEMY MODEL.
- 3D FLYING: bots now fly up and down (to your height, weaving), and the HUD hides the dash and hook bars while flying; the speed meter counts vertical speed too.
- Aim trainer: the scenario name moves under the F3 / F4 box instead of being covered by it.
- Aim trainer: hide the HUD (your HIDE HUD key, or SHOW HUD in the pause menu: only the crosshair stays) and the weapon (SHOW WEAPON in the pause menu, the same setting as in matches).
- Aim trainer: F3's frame-time graph (the third F3 page) and F4's mouse-to-photon number now show in a run.
