# Owner decisions 2 (1.17.0)

Decisions from the owner of quark, for the implementer. Quotes are the owner's own words. Where the owner left a detail open, the default used here is marked **(default, owner may override)**. The current values come from `src/game/gamedefs.h`, `src/game/pmove.cpp` and `src/game/server.cpp` at the latest `main`.

---

## B1. Lodestone map: offered in every mode

**Decision:** "a" (keep every mode and rebalance the map for each mode it offers).

**Implementation:**
1. Lodestone stays available in every game mode it offers today. Do not remove any mode.
2. For each mode, give Lodestone its own spawns and routes where needed, and grade it in every mode (not only multi point).
3. Bomb is the known failure: attackers currently reach site D 10.8 s before defenders. Re-stage it so every bomb site falls within the new band from B2 (−1 to +1 s).
4. Every attack/defend mode on Lodestone must pass the B2 band at its worst site or point.

## B2. Midtown timing, and the attack/defend timing band for all maps

**Decision:** "the game is very fast paced, so really they should probably reach the site nearly at the same time, as if the defenders take over the site its hard for the attackers to ever plant the bomb" → "target between -1 and +1", "yes all maps". On grading: "yes graded by the worst part".

**Implementation:**
1. Replace the design band (currently a 2–10 s defender head-start) with **−1 s to +1 s** for **every attack/defend map and mode** (multi point, bomb, and any other mode with sites or points). Define the value as attacker arrival time minus defender arrival time: positive means defenders arrive first.
2. Re-stage Midtown's spawns so **every point A–E** lands in −1 to +1 s. Today D is at +12.7 s and E at +0.9 s.
3. Change map grading to use the **worst point/site** instead of the average, for all maps. Update the map report, tests and tooling (for example `tests/mapreport.cpp`) to match.
4. Re-grade every official attack/defend map against the new band. Fix any map that fails, including Lodestone per B1.

## B3. In-game editor vs the map generator

**Decision:** "a, edits on official shipped maps are not saved globally only locally"; "for any user that isnt me specifically no you should just ask the user if they want to keep a local save of the edits they made in a new map"; "the in game editor should still stay in future updates, i will just have access to a special more advanced map editor".

**Implementation for 1.17.0:**
1. Official maps come only from the generator (`tools/make_maps.py`). Editor edits never change official maps for anyone else.
2. When a player edits an official map in the in-game editor and saves or leaves, **ask them whether they want to keep their edits as a new local map**:
   - **Yes:** save a copy in their user folder under a new map name.
   - **No:** discard the edits.
   - Do not save a copy silently, as happens today.
3. The normal in-game editor stays in the game for all players, in this and future versions.

**After 1.17.0** (not part of 1.17.0, "this can ship after"):
1. Build a **dev-only advanced map editor**: a separate developer build that only the owner gets. It is never included in public releases, and access is not an in-game account check.
2. It includes "any possible option you can think of that would be useful for making the map… things that general players making a custom map would not need". Examples:
   - numeric position, rotation and scale entry
   - adjustable grid snapping
   - editing spawns, routes and staged spawns per mode
   - a live timing and grade readout using the B2 band at the worst point
   - and similar tools.
3. The dev editor **uploads maps to a git branch named `map-tests`**. Each release then specifies which maps, and which version of each, get pulled from `map-tests` into the official build. That needs a manifest or config listing map name and version/commit, which the generator and build use.

## B4. Knife throw and syringe

**Decision:** "knife throw disables the knife entirely for 4 seconds, it should still have the regular knife swing cooldown"; "throwing syringe… disables the syringe for 4 seconds, and gives a 1 second cooldown"; "change the self use syringe to 10hp, with a 1 second cooldown"; "there is no special shared cooldown, just normal cooldowns with weapon disables". The owner described a regular cooldown as one where "nothing can get used for 1s".

**Implementation:**
1. **Remove** the special 0.75 s delay on the next weapon after a knife throw.
2. **Remove** the 2 s throw cooldown shared by the knife and the syringe.
3. **Knife throw:**
   - Triggers the knife's normal swing cooldown (currently 0.75 s, the KNIFE `refire`). During it, no weapon can be used.
   - The **knife is disabled for 4 s** from the throw.
4. **Syringe throw:**
   - Triggers the syringe's normal cooldown, now **1 s**. During it, no weapon can be used.
   - The **syringe is disabled for 4 s** from the throw.
5. **Syringe self-use:**
   - Each use heals **10 HP**, with a **1 s cooldown**.
   - This replaces the current 1 HP every 0.1 s. Set SYRINGE `refire = 1.0`, heal = 10.
6. Update the comments next to the weapon table and the throw constants (`gamedefs.h` around lines 179 and 232) to match.

## B5. King of the hill contest rule

**Decision:** "a".

**Implementation:** No change. A contested hill (any enemy on it) keeps freezing the clock for both teams.

## B6. Launcher signing rule

**Decision:** "yes, any update starting with 1.17.0 must be signed, including tests, site, launcher. anything below 1.17.0 does not require signing, you do not need to display in the launcher to regular users the old versions are unsigned".

**Implementation:**
1. Keep the rule: the launcher hides any unsigned release that claims to be version 1.17.0 or newer.
2. From 1.17.0 on, everything published must be signed: game releases, **test/pre-release builds**, **the launcher itself**, and **site-distributed artifacts**.
3. Releases at 1.16.x and below stay installable without a signature, and **no unsigned warning** is shown to regular users for them.

## B7. Weapon balance

**Decision:** "decrease the rifle damage by 1 per shot overall"; "i dont want any spread"; "make the laser 24m instead of 32… so the lg has more dps, but is shorter range, unless you can headshot enough then rifle overpowers it"; "make sniper a flat 50".

**Implementation** (`baseWeaponDef` and the constants in `gamedefs.h`):

| Weapon | Setting | Before | After | Other changes |
|---|---|---|---|---|
| Rifle | Body damage | 5.95 | **4.95** | Fire rate unchanged (`0.75*2/9.9` s, ×4/3 when scoped). No spread, no falloff, range 300 m. |
| Rifle | `kRifleHeadDamage` | 8.5 | **7.5** | |
| Laser | Range | 32 m | **24 m** | Damage and tick unchanged (0.04 every 1 ms = 40 DPS). |
| Sniper | Body damage | 34 | **50** | Head zone is the head only (earlier decision). |
| Sniper | Head damage (`kSniperHeadDamage`) | 50 | **50** | Refire stays 1.5 s. |

Update the TTK comments next to the table:
- **Rifle:** 21 shots / about 3.0 s body; 14 / about 2.0 s head.
- **Laser:** 2.5 s, 24 m.
- **Sniper:** 2 shots / 1.5 s.

The resulting DPS relative to the laser: rifle 0.82 body, 1.24 head; sniper 0.83. No other weapon changes.

## B8. Hide and seek and tag

**Decision:** "if a hider survives the whole time they get 1 point"; the seeker gets "1 per person found"; found hiders get 0; "everybody gets to be seeker once"; the RPS tiebreak "only happens if once every round is played there is a tie of points", "best of 3, if more than 2, everyone who loses is out, unless everybody loses then nobody does". The knife must be visible for the seeker and tagger. "the seeker holds it, and the tagger only holds it"; "in tag mode all runners should always be aware of the tagger location, in HNS the hiders should not always be aware of the seeker location"; "model a locator weapon, and locator should not exist in any other gamemode".

**Implementation:**

1. **Hide and seek scoring** replaces the 4-point time-share system:
   - A match has as many rounds as there are players, and every player seeks exactly once.
   - In each round:
     - Each hider who stays unfound until the round ends gets **1 point**.
     - The seeker gets **1 point per hider found**.
     - A hider who is found gets **0**.
     - A hider who dies from a fall gets **0**, and the seeker gets nothing for that hider.
2. **Rock paper scissors tiebreak**, in hide and seek, after all rounds are played, only if players are tied on points. The tie is broken **among the players tied for 1st (default, owner may override)**:
   - **2 players:** best of 3. The first to win 2 throws wins, and a drawn throw is replayed.
   - **3 or more players:**
     - Everyone throws at once.
     - If the throw has a clear loser or losers (exactly two shapes appear), those players are out.
     - If nobody clearly loses (everyone picked the same shape, or all three shapes appear), nobody is out and they throw again.
     - Repeat until 2 remain, then play best of 3 as above.
   - Build it as an in-game minigame: choose rock, paper or scissors with a short timer, then reveal.
3. **Knife visibility:** in hide and seek and in tag, the seeker's or tagger's knife must be visible, both in first person and on their model as other players see it. Only the seeker or tagger has the knife in these modes.
4. **Locator as a weapon:**
   - Today the locator is a `use`-key ability (`"locate"` command in `server.cpp`, HUD in `client/game.cpp`). Turn it into a **selectable weapon with its own modeled viewmodel and world model**.
   - It's held **only by the seeker (hide and seek) and the tagger (tag)**.
   - Keep the behavior: a **5 s cooldown**, and it shows **one random hider or runner for 1 s** (`kLocatorCooldown` / `kLocatorShow`).
   - The locator must **not exist in any other game mode**.
5. **Awareness:**
   - **Tag:** all runners always see the tagger's location, as a marker visible through walls.
   - **Hide and seek:** hiders are never shown the seeker's location.

## B9. Dead players' chat, and a chat toggle

**Decision:** "this doesnt matter because ranked is either 1v1 or ffa instagib so everybody can see everybodies chat, also if there is no disable chat keybind there should be (toggle) and a disable chat option".

**Implementation:**
1. No dead-chat restriction. Everyone keeps seeing everyone's chat in all modes.
2. Add a **"Toggle chat" keybind action** in `client/binds.cpp`. It's **unbound by default (default, owner may override)**. It hides or shows incoming chat and shows a short on-screen message when toggled.
3. Add a **"Disable chat" option** in settings. It persists in the client config and stays in sync with the toggle.

## B10. Other additions for 1.17.0

**Decision:** "there should be a vote for remove player, like if they go afk or are being an ass or something, it will move them to spectator; server OR hoster can kick people from server with command, but that shouldnt be votable". Then: "thats it for now".

**Implementation:**
1. **New vote: remove a player to spectator.** Add it to the existing vote system, for example `callvote spec <player>`.
   - **If it passes,** the target is moved to spectator. They are not disconnected.
   - **Vote rules (default, owner may override):**
     - It uses the existing vote rules and the 30 s timer.
     - The target cannot vote on it.
     - It needs at least 3 connected players, so it's not available in 1v1.
   - **After it passes (default, owner may override):** the target cannot rejoin play until the next map, and cannot be targeted again more than once per map by the same caller.
2. **Kick stays admin-only.** The existing `kick` command (`server.cpp` around line 711) must be usable by the dedicated server console and by the host of a listen or hosted server. Kick must **not** be votable.

---

**UNDECIDED:** none. Every item, B1–B10, was decided.