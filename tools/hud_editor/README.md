# Quark HUD Editor

The Quark Project app's **Editor Manager > HUD Editor** tab (http://127.0.0.1:8774/). A 1280x720 preview of the
in-game HUD (the game's 720p units, so it holds at every resolution) where you drag, size and recolour:

- **Built-in HUD** (quark.cfg `hud_<N>=x,y,scale,custom,r,g,b,hidden,z[,pitch,yaw,roll]`, `Settings::HUD_*` in
  `src/client/config.h`): health, weapon name, cooldown bars, speed meter, weapon slots, key overlay, the first-person
  weapon model and the grapple launcher (`hud_7`), each with its own x / y / depth / size / hide and pitch / yaw / roll
  (degrees, -90 to 90: + up, + right, + clockwise). The game reads quark.cfg at start and writes
  it back when its settings change, so save these with the game closed (the page warns when quark.exe is running).
  Only the `hud_*`, `hud_widgets` and `hud_verbose` lines are rewritten; the rest of quark.cfg stays as it was.
- **Widgets** (hud.cfg, `src/client/hudwidgets.*`): any game value as a number, bar, graded bar, icon or icon + bar,
  anchored to a corner or the centre. The game re-reads hud.cfg within a second, so these show in a running game.
- **Profiles**: open or save widget sets in the game's `huds/` folder (Settings > HUD > widgets, per-mode HUD rules).
- **Text**: hud.cfg as it will be written; paste a layout and Apply, or Copy it for the game's IMPORT button.

Drag on the preview, mouse wheel to size, arrow keys to nudge (Shift: 10 units), Ctrl+Z / Ctrl+Y, Ctrl+S to save. Each
file it replaces is kept once per run as `<file>.bak`. Element shapes follow `Game::drawHud*` closely, not exactly;
check the result in game with Settings > HUD.

    python3 tools/hud_editor/serve.py [--port 8774] [--data-dir DIR] [--no-open]
    python3 tools/hud_editor/serve.py --install [FOLDER]     (default <Quark folder>/hud-editor; the app does this)

The data folder is the game's: `QUARK_DATA_DIR`, else `%APPDATA%\quark` (Windows), `~/Library/Application
Support/quark` (macOS) or `~/.local/share/quark`. Python 3 standard library only, 127.0.0.1 only; writes need the
page's `X-Quark-Editor` header.
