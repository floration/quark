# Next (testing builds since 1.19.4)

What's on `testing` since production 1.19.4. The release PR renames this file to `changelogs/<VERSION>.md`.

## Player names
- **New players are numbered:** a new player's default name is now "player" plus their player number (the 24th player to play is player24) instead of plain "player". Players still named plain "player" get theirs too. A name you typed is never changed.

## 1v1
- **1v1s rotate every round:** on a 1v1 server with someone waiting, every round is now its own match (rated on its own in ranked, so ranked 1v1 plays like a free-for-all of 1v1s): the loser goes to the back of the line, the next in line plays the winner, and the next round starts straight away with no warmup. With only two players, matches are first to 5 as before. Server owners: `--rotate match` brings back the old rule (a whole match, then winner stays).
