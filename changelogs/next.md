# Next (testing builds since 1.19.1)

What's on `testing` since production 1.19.1. The release PR renames this file to `changelogs/<VERSION>.md`.

## Performance
- **New Settings > VIDEO > PRIORITIZE GAME (Windows, on by default)**: while the game has focus, Windows runs its GPU work before other programs' (a YouTube tab, Discord or other hardware-accelerated apps) and gives it a higher CPU priority. Before, a video playing in the background could take a big share of every frame. Alt-tab and the other programs get their normal share back.

- Human-made assets: 16.1%, unchanged (no asset changes).
