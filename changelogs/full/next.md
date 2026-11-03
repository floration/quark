# next (full)

Every change since 1.19.7, with what changed and why.

## Build and release (CI)

- **The macOS build goes to an Intel Mac first, with arm64 as the fallback.** On 2026-11-02, from 13:39 to 23:08 UTC, GitHub's macOS arm64 pool was full. Every `macos-latest` job sat queued about 15 minutes, then ended with "The job was not acquired by Runner of type hosted even after multiple attempts". That turned every macos run red (runs 187-205), and those testing builds and releases 1.19.5 and 1.19.6 went out without macOS files. A probe at 22:55 UTC showed the split: `macos-14` got a runner in 3 s, `macos-15-intel` in 19 s, `macos-latest` and `macos-26` only after 13 min, and `macos-15` never did. The build steps moved to `.github/actions/macos-build`. The `macos` job now runs them on `macos-15-intel`, and the new `macos-arm` job (`macos-latest`) runs them only when the Intel job didn't build. Both make the same universal (arm64 + x86_64, macOS 10.15+) files, and `publish` takes whichever one built. The trade-off: when no Intel Mac picks the job up, the arm64 fallback starts about 15 min later and the run still shows that first job as failed. GitHub plans to retire `macos-15-intel` in August 2027; switch the order back before then.

## Tests (quark_tests)

- **modelockstep2: no more "winner at N Hz: no hooks".** The lockstep cases still had a winner stays case. Winner stays was retired (1v1 rotates the line itself now), so a server started in it plays 1v1 (`retiredTo`). Logic compiled for winner stays never hooks into a 1v1 server, which made it fail at 1000, 250 and 125 Hz, on 1.19.6 too. The case is gone, and 1v1's case covers the line.
- **The 3D flying checks (rules flymod, vamp3d) no longer fail under load.** `pump()` counts wall-clock time, and under load the server's own clock falls behind it, so 0.5 s after `addBot` the flymod bot often hadn't spawned yet. With eight busy cores beside it, the check failed in 8 of 8 runs; now it fails in 0 of 8. Both tests now pump server time and wait (for up to 5 s of game time) for the spawn and for the fly flag's snapshot.
- **The movement group's air drift checks follow `kAirDriftAccel`.** The owner set air drift to 0 on 2026-11-01 (protocol 63). The checks still expected 8 m/s² of drift: a standing jump + W at about 7 m/s, a long fall reaching 9.5 m/s, and a sideways push past 3 m/s. That made 3 checks fail. With drift at 0, they now check the strafe push alone: 2.30 m/s for W-only and 2.53 m/s for A/D. If drift comes back, the old numbers apply again.

## Voting (owner, 2026-11-01: "just one VOTE button, then you toggle on options")

- **The builder's GAMEMODE / MODIFIERS / MAP toggles moved onto the VOTE page itself**, with CHOOSE naming the first step: one click and one page fewer. The page keeps its height by pairing START/RESTART, RESTART/5 MORE MINUTES and RANKED/SHUFFLE TEAMS. BACK walks one step back (summary > map > modifiers > gamemode > VOTE).
- **New modifiers FIRE RATE (x0.25-4) and PROJECTILE SPEED (x0.25-3)** apply to every weapon through the custom rules' existing `weapon=` multipliers, so there is no protocol change; the server's `mods` whitelist gained `firerate` / `projspeed` (an older server answers "bad modifier"). Player size and ammo limits, also on q's list, don't exist as gameplay yet and were not invented.
- The UI test now reaches the builder's modifiers and summary pages (scoped builder pickers, one context per toggle combination); before, they were never tested.

## Acid and lava (owner, 2026-10-27: "it should destroy, so should lava, yes screen tint, and pulse and sizzle")

- **Projectiles that enter lava, a deadly liquid or acid are removed there**: no blast, hit or prop, and no explosion revealed to players who couldn't see it. The server's 1 ms step and the client's own copies use the same `Map::dissolveAlong` test, so nothing blows up or reports a hit past the surface; a vanished projectile at the surface plays a hiss.
- **In acid:** a green screen tint (0.12 alpha, pulsing +-0.04 while it hurts; steady with REDUCE FLASHING), the health bar pulses acid green, and the new SND_SIZZLE plays on contact and every 0.4 s. SIZZLE is generated in code like the other default sounds and added after LOCATOR, so recorded kits' ids are unchanged; override name SIZZLE.

## Netcode and server

- **NET-14: projectile ids never repeat while one is in flight.** `newProjId` skips 0 and any id a live projectile holds, so a long-lived sticky never shares its id after the u16 wrap (a wrong trail or missed explosion). Server only, no protocol change; one scan of 128 slots per shot.
- **COR-8: interpolation is clamped to kMaxInterpMs + 0.1 s behind the newest snapshot**, and the bracket search stops at a server clock reset; it could draw poses up to ~1 s old.
- **Visibility rays scale with the target's size** (a MOVE_BIG giant's head is twice as high), so a giant whose head sticks out over cover is sent. Same ray budget.
- **breakGlass's shard scratch list is a heap buffer that is never freed** instead of a thread_local object: a thread_local with a destructor crashed at thread exit under MinGW on hosted servers. Still no allocation per break.
- **install.sh keeps a testing install that is ahead of the checkout**, runs the testing updater during the install, and sets `StartLimitIntervalSec=0` on the server units. Re-cloning had put quark-testing on the older production build, which couldn't load the newer maps; systemd then gave up before the updater rebuilt (~10 min).

## Tests and tools

- Timing tests notice every host pause (ratePump's sleeps and the loop's own off-CPU time), allow up to 5 attempts and only re-run attempts the host paused, so a real regression still fails; the grapple latch prediction check is a timing test now.
- A test server that fails to start always prints a FAIL line (`START_SERVER`).
- release_check.sh reads its alone groups from `quark_tests groups alone` instead of a copy.
- A CRLF demo versions.txt reads correctly (test added); the material table check allows the ball wall.
- ZONESTATS builds link on MinGW (no thread_local init function); pmove's internal CmdCtx no longer clashes with the map tool's; unused constants removed; SIGNING.md no longer points at a deleted key file.
