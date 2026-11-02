# Next (testing builds since 1.19.6)

What's on `testing` since production 1.19.6. The release PR renames this file to `changelogs/<VERSION>.md`.

## Languages
- **The game in 10 languages.** Settings > INTERFACE > LANGUAGE offers English, 简体中文 (Simplified Chinese), РУССКИЙ, ESPAÑOL, PORTUGUÊS (BRASIL), DEUTSCH, 日本語, FRANÇAIS, 한국어 and TÜRKÇE. AUTO (the default) uses your system's language. Menus, settings, the HUD, notices and the scoreboard are translated. Player names, chat and map names stay as they are written.
- Accented letters, Cyrillic, Chinese, Japanese and Korean are drawn in the game's own font style, so names and chat in those scripts now show up instead of blank boxes.
- A dropdown near the bottom of the window now opens upward, so its list stays on screen.
- No change for English, and no extra cost: English does no lookups at all.

## Latency
- **LOW LATENCY: ON sends the frame to the GPU sooner.** The shadow map and the world now go to the GPU as soon as they're drawn, as ULTRA already did, so the GPU starts while the HUD is still being drawn instead of waiting for the swap. F3's gap between SWAP and GPU gets shorter.
- **GPU SCHEDULING** (Settings > VIDEO, the LATENCY section, Windows): shows whether Windows' hardware-accelerated GPU scheduling is on. TURN ON sets it after Windows' admin prompt and takes effect after a restart. It lets frames reach the GPU sooner.
- **F3's latency graph in nanoseconds**, with a line splitting the total into GAME + MOUSE POLLING + DISPLAY. F3 has a new fourth view, GAME ONLY: the same graph without the mouse and display rows, so you see the game's own latency alone. F4's number is in ns too.
