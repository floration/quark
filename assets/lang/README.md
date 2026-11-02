# Languages

The game draws its own text in the player's language (Settings > PLAYER > INTERFACE > LANGUAGE, the `language`
setting: AUTO follows the system's language). The code keeps writing English: the renderer looks each drawn string up
in the language's table just before drawing or measuring it (`src/client/lang.h`), so no menu or HUD code changes.

| File | What |
|---|---|
| `keys.txt` | every player-facing string in the code, one a line (`python3 tools/lang/extract.py` writes it) |
| `<code>.tsv` | one line per key, in keys.txt's order: the English key, a TAB, the translation (the same text: drawn as it is) |
| `atlas/<name>.png` + `.glyphs` | the glyphs past ASCII each language draws, baked by `tools/lang/make_atlas.py` |

Languages: English, 简体中文 (zh-CN), Русский (ru), Español (es), Português (Brasil) (pt-BR), Deutsch (de), 日本語 (ja),
Français (fr), 한국어 (ko), Türkçe (tr): the ten biggest on Steam (owner, 2026-11-02: "gaming list yeah").

How a drawn string finds its translation (`lang.cpp`, first match wins):
1. the string itself is a key;
2. its numbers as `#` (`KILLS: 12` -> `KILLS: #`), the numbers put back into the translation in order;
3. a key with `{}` matches as a pattern (`WARMUP - {} TO VOTE START`): each `{}` is put back, translated if it's a key;
4. a number before a word (`1 ROCKET`), decorations round it (`> PLAY <`, `SAY:`), then each side of `: `, ` - `, ` / `.
Anything else (player names, chat, map names) is drawn as it is. English does no lookup at all.

**After changing text in the code:** run `tools/lang/extract.py`, translate the new keys into every `<code>.tsv` (keep
keys.txt's order, every `#` and `{}`), then `tools/lang/make_atlas.py --cjk <NotoSansCJK-Bold.ttc>` if a translation
uses a character the atlases don't have. `quark_tests assets` fails on a missing key, a `#` count that differs, or a
character an atlas lacks.

To find lines no key covers (the code builds them from pieces): run the UI test in a language with
`QUARK_UITEST_LANG=de QUARK_LANG_MISSES=misses.txt build/quark --uitest`; every untranslated drawn line with words in
it is written to misses.txt.
