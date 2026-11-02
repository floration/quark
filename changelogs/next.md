# Next (testing builds since 1.19.4)

What's on `testing` since production 1.19.4. The release PR renames this file to `changelogs/<VERSION>.md`.

## Player names
- **New players are numbered:** a new player's default name is now "player" plus their player number (the 24th player to play is player24) instead of plain "player". Players still named plain "player" get theirs too. A name you typed is never changed.

## 1v1
- **1v1s rotate every round:** on a 1v1 server with someone waiting, every round is now its own match (rated on its own in ranked, so ranked 1v1 plays like a free-for-all of 1v1s): the loser goes to the back of the line, the next in line plays the winner, and the next round starts straight away with no warmup. With only two players, matches are first to 5 as before. Server owners: `--rotate match` brings back the old rule (a whole match, then winner stays).

## 3D VAMP
- **New 1v1 ruleset, 3D VAMP** (HOST > MODE > 1V1, or the vote menu's PLAYER BASED > 1V1): fly in zero gravity like creative mode in Minecraft. W/S/A/D move you on the level, MOVE UP (space) and MOVE DOWN (left shift) straight up and down, all at run speed with a quick stop when you let go. No jump, dash or grapple. Lasers only, hits don't push anyone, and vampire healing starts at 100% of your damage each round and falls to 50% over 30 s. Not ranked.
- **New map, Black Hole**: a lone flat asteroid in space with no cover, a black hole eating the galaxy beside it. Nobody can fire for the first 3 s of a round. Fly too far out and you're lost.
- **New keys**: MOVE UP and MOVE DOWN (CONTROLS > MORE KEYS), and VERTICAL KEYS (CONTROLS > MORE 2): ONE KEY flies up while MOVE UP is held and down when it isn't.

## Aim trainer
- **AIM TRAINER** on the main menu: 36 scenarios of 30 s in 3D STATIC (look and shoot) and 2D (the view stays still, your mouse moves the crosshair), in CLICKING (rifle, one shot a click), SWITCHING and TRACKING (laser), each in EASY / MEDIUM / HARD. Your best, last score and plays show on each scenario's card; a leaderboard comes later. 2D is drawn flat (no 3D at all) with the crosshair moving at an even speed. ESC pauses a run (RESUME, SETTINGS, RESTART, TRAINER MENU) and F3 / F4 work in the trainer.

## Servers
- The testing server now always starts again when a new testing build comes out (it could stay down after a production release).
