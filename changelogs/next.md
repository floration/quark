# Next (testing builds since 1.19.3)

What's on `testing` since production 1.19.3. The release PR renames this file to `changelogs/<VERSION>.md`.

## Maps
- **Two new maps, made by q:** **Surfy** (surf), now the map surf mode starts on, and **Cobblestone**.

## Servers
- **Testing server alone:** `install.sh --testing-only` sets up just the testing server, on the normal port, and keeps it running all the time on the newest testing build or production release, whichever is newer. Both servers stay the default (`--both` to go back); `--no-testing` is still production only.

## Modes
- **New mode: POWER.** Each team shares one health pool, 2500 for every player on it, and nobody dies. An enemy's hits come off your team's pool instead of your health. When a team's pool runs out it loses; when the 10 minutes are up, the fuller pool wins. Your own rockets and the world (lava, acid, falls) take nothing from the pool; falling out of the map still respawns you. Someone who joins mid-match adds their 2500. The score box shows both pools, and the bar under it shows yours. Played on Bastion, Orbital and Lodestone. **The network protocol is now 54**, so games and servers update together.
