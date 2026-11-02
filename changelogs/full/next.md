# next (full)

Every change since 1.19.7, with what changed and why.

## Build and release (CI)

- **The macOS build goes to an Intel Mac first, with arm64 as the fallback.** On 2026-11-02, from 13:39 to 23:08 UTC, GitHub's macOS arm64 pool was full. Every `macos-latest` job sat queued about 15 minutes, then ended with "The job was not acquired by Runner of type hosted even after multiple attempts". That turned every macos run red (runs 187-205), and those testing builds and releases 1.19.5 and 1.19.6 went out without macOS files. A probe at 22:55 UTC showed the split: `macos-14` got a runner in 3 s, `macos-15-intel` in 19 s, `macos-latest` and `macos-26` only after 13 min, and `macos-15` never did. The build steps moved to `.github/actions/macos-build`. The `macos` job now runs them on `macos-15-intel`, and the new `macos-arm` job (`macos-latest`) runs them only when the Intel job didn't build. Both make the same universal (arm64 + x86_64, macOS 10.15+) files, and `publish` takes whichever one built. The trade-off: when no Intel Mac picks the job up, the arm64 fallback starts about 15 min later and the run still shows that first job as failed. GitHub plans to retire `macos-15-intel` in August 2027; switch the order back before then.

## Tests (quark_tests)

- **modelockstep2: no more "winner at N Hz: no hooks".** The lockstep cases still had a winner stays case. Winner stays was retired (1v1 rotates the line itself now), so a server started in it plays 1v1 (`retiredTo`). Logic compiled for winner stays never hooks into a 1v1 server, which made it fail at 1000, 250 and 125 Hz, on 1.19.6 too. The case is gone, and 1v1's case covers the line.
- **The 3D flying checks (rules flymod, vamp3d) no longer fail under load.** `pump()` counts wall-clock time, and under load the server's own clock falls behind it, so 0.5 s after `addBot` the flymod bot often hadn't spawned yet. With eight busy cores beside it, the check failed in 8 of 8 runs; now it fails in 0 of 8. Both tests now pump server time and wait (for up to 5 s of game time) for the spawn and for the fly flag's snapshot.
- **The movement group's air drift checks follow `kAirDriftAccel`.** The owner set air drift to 0 on 2026-11-01 (protocol 63). The checks still expected 8 m/s² of drift: a standing jump + W at about 7 m/s, a long fall reaching 9.5 m/s, and a sideways push past 3 m/s. That made 3 checks fail. With drift at 0, they now check the strafe push alone: 2.30 m/s for W-only and 2.53 m/s for A/D. If drift comes back, the old numbers apply again.
