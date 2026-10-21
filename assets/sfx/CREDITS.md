# Sound effect sources

Every clip in this folder is the unmodified original file from a Kenney (www.kenney.nl) audio pack, released under
**Creative Commons Zero (CC0 1.0, public domain dedication)** - http://creativecommons.org/publicdomain/zero/1.0/ -
free for any use with no obligations (credit is not required; listed here for traceability).
`tools/import_sfx.py` mixes / pitches / trims them into `src/client/sfx_recorded.h` for the HEAVY METAL, MAGICAL
and WOODEN sound packs. DEFAULT and RETRO are synthesized in `src/client/audio.cpp`.

| Folder | Pack | Source | License |
|---|---|---|---|
| `impact/` | Impact Sounds 1.0 | https://kenney.nl/assets/impact-sounds (kenney_impact-sounds.zip) | CC0 1.0 |
| `scifi/` | Sci-fi Sounds 1.0 | https://kenney.nl/assets/sci-fi-sounds (kenney_sci-fi-sounds.zip) | CC0 1.0 |
| `iface/` | Interface Sounds 1.0 | https://kenney.nl/assets/interface-sounds (kenney_interface-sounds.zip) | CC0 1.0 |
| `rpg/` | RPG Audio 1.0 | https://kenney.nl/assets/rpg-audio (kenney_rpg-audio.zip) | CC0 1.0 |
| `casino/` | Casino Audio | https://kenney.nl/assets/casino-audio (kenney_casino-audio.zip) | CC0 1.0 |

Each pack's License.txt states: "License: (Creative Commons Zero, CC0) ... This content is free to use in personal,
educational and commercial projects." Downloaded 2026-10-21. Which clip each game sound uses: the `KITS` table in
`tools/import_sfx.py`.
