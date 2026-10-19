# Working on quark

- Commit and push every change to the working branch automatically when it's done and tested; don't ask first.
- Before pushing: build (`cmake --build build -j`) and run `build/quark_tests` (headless multiplayer tests must pass).
- Client changes can be checked headless: `Xvfb :99 &` then `DISPLAY=:99 build/quark --host ffa --map arena --screenshot out.png`.
- Weapons and movement in `src/game/gamedefs.h` / `src/game/pmove.cpp` are placeholders until the owner specifies them.
