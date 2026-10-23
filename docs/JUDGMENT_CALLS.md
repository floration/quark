# Judgment calls (1.17.0)

The owner's decisions on the 1.17.0 judgment-call list, recorded in a separate session on 2026-10-23. "DEFAULT" means the owner accepted the proposed default as it was. Where the owner changed or added something, their words are quoted and the instruction reflects them.

The owner added one rule afterwards: **human-made maps** (the owner will hand-make every map later) **don't have to pass the automated map checks**. Their numbers are still computed and reported, for balancing.

---

### C1: Map metric corrections (MAP-4)
**Decision:** DEFAULT ("go ahead").

**Instruction:**
- Fix the map-report metrics strictly:
  - count loops correctly;
  - account for vertical play;
  - score balance per team for each objective;
  - give unreachable areas no credit;
  - grade the worst objective, not the average.
- Never loosen a metric or the "every map scores ≥ 7" bar to make a map pass.
- Fix any generated map that drops below 7 in the generator.
- List each changed metric and each changed map, with before and after scores.

### C2: Map identity by sha256 (MAP-13)
**Decision:** DEFAULT ("do it").

**Instruction:** Identify maps by the sha256 of their content instead of name + CRC32: the ranked map allowlist, light caches and map downloads. Nothing about this should be visible to players.

### C3: Split tools/make_maps.py (MAP-14)
**Decision:** DEFAULT ("do it").

**Instruction:** Split `tools/make_maps.py` into a shared prefabs module plus one file per map. The generated output must be byte-identical to the current output; verify with a diff.

### C4: GameMode interface refactor
**Decision:** DEFAULT ("sure").

**Instruction:** Not in 1.17.0. After the release, replace the roughly 90 `mode == X` branches with one GameMode interface per mode, on the testing branch.

### C5: LTO
**Decision:** DEFAULT ("leave off yeah it needs a real pc to test").

**Instruction:** Leave LTO off for 1.17.0.

### C6: sfx_recorded.h
**Decision:** DEFAULT ("keep it committed").

**Instruction:** Keep the 20 MB `sfx_recorded.h` committed. Don't add an ffmpeg build dependency.

### C7: Map file format (MAP-10/11)
**Decision:** DEFAULT ("do it now").

**Instruction:** In 1.17.0:
- a `version N` header in map files;
- unknown optional keys warn instead of failing;
- real `stairs` and `setting` keywords instead of the comment conventions.

The entity system for objective markers (first review G8) comes later.

### C8: Projectile physics fixed-step
**Decision:** DEFAULT ("yes").

**Instruction:** Integrate projectiles in fixed 1 ms steps, like player movement, so arcs are identical on every server tick rate.

### C9: Ranked behind unusual NAT/containers
**Decision:** DEFAULT ("ok").

**Instruction:** Keep `--own-addr host:port`. Document it in the server README and on the host screen, in addition to the log message.

### C10: Windows CI netcode timing check
**Decision:** DEFAULT ("ok").

**Instruction:** If that one netcode timing check fails on the real Windows runner, give only that check a Windows-specific tolerance and keep the job blocking.

### C11: Bot difficulty
**Decision:** DEFAULT ("this is fine").

**Instruction:** Keep rifle DPS against a strafing target at beginner 3, intermediate 6, advanced 10.5, expert 18.

### C12: Hidden-player info (NET-10)
**Decision:** DEFAULT ("ok").

**Instruction:**
- Don't send health, weapon, firing or bomb info for players the client can't see.
- Cull unseen projectiles.
- In hide and seek, send the seeker nothing about hiders during the freeze.
- Spectators still receive everything.

### C13: Launcher update file cleanup
**Decision:** Changed: "delete retired game files, dont delete configs, custom maps, demos, etc also if demos need to be updated when version gets updated do that" and "convert it to the new update, and leave an old original copy also".

**Instruction:**
1. **Deleting files:** on update, delete only retired game files, i.e. files an earlier official package installed that the new one doesn't include. Decide this from a manifest of installed files. Never delete these:
   - configs;
   - custom or user maps;
   - demos;
   - screenshots;
   - anything the player created.

   When in doubt, keep the file.
2. **Demos:** when an update changes the demo format so older demos wouldn't play, convert existing demos on update (or on the first start after it). Keep each original as a backup, e.g. under `demos/old/<old-version>/`. If a demo can't be converted, leave it, log it, and show "recorded with version X" instead of failing.

### C14: Tourney seeding
**Decision:** DEFAULT ("keep").

### C15: Ranked match lock
**Decision:** Changed: "1v1 vanilla, 1v1 lg vampire, and ffa instagib (ranked modes) are the only locked down ones".

**Instruction:** The "no votes, no map or mode changes while a ranked match is live" lock applies to exactly those three ladders. The viaduct race is not locked: it has no rating, only best times, and quitting submits nothing.

### C16: Other items
None.
