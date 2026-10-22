# Owner decisions

Everything from the adversarial review ([REVIEW.md](REVIEW.md)) that the fixers can't settle on their own, because it changes game design, the release process, deployment or player data. Each entry says what the choice is, why it matters, the options and a recommendation. The IDs point at REVIEW.md, and [FIXES_HANDOFF.md](FIXES_HANDOFF.md) has the batch plan and the fix log.

State: branch `review-fixes`, after batch 1 (16 fixes, 2026-10-22). Nothing on this branch has been released.

---

## 1. Needed now (before or at the next push)

### 1.1 How this branch reaches players (release process)
- **Why:** `.github/workflows/release.yml` runs on a push to *any* branch. It publishes GitHub release `v<VERSION>` with `make_latest: true` and redeploys the website (REVIEW SUP-2). `VERSION` on this branch is still 1.15.1, older than the live v1.16.0. Any normal push of it would re-upload v1.15.1's assets and mark them latest, which **auto-downgrades every launcher**. This branch was pushed with `[skip ci]` on its head commit for that reason.
- **Decide:**
  1. When to release these fixes, and as which version (1.16.x / 1.17.0). At that point we bump `VERSION` and write the changelog, per CLAUDE.md.
  2. Whether to merge `main` / the remote's 1.16.0 work first. The remote default branch is 4 commits ahead of `2ce3a44`, ending at v1.16.0 (macOS builds re-enabled).
  3. Whether to change the trigger (SUP-2, see 2.2) before anything else is pushed.
- **Recommendation:** merge 1.16.0 into this branch, then bump to 1.17.0 and release once. Make the trigger change soon, so a stray push can't downgrade players.

### 1.2 Discord bot: redeploy without Mention Everyone (BOT-1)
- **Why:** the code fix (no mentions unless a message opts in, names escaped) only helps once the bot runs it. The bot's invite also grants Mention Everyone, which it doesn't need.
- **Decide:** redeploy the bot from this branch, and re-invite it (or edit its role) without Mention Everyone.
- **Recommendation:** yes, both. The bot only pings its own roles and members.

### 1.3 Bot test dependencies
- **Why:** `python -m unittest test_quarkdata` needs discord.py and aiohttp, which aren't installed on the dev machine. With them stubbed out, the new safety tests and 5 of 8 parsing tests pass. The other 3 call into `bot.py` and haven't run.
- **Decide:** may the fixer `pip install -r tools/discordbot/requirements.txt` into a scratch venv? The alternative is running the tests wherever the bot is deployed.
- **Recommendation:** allow the venv. It's a local, throwaway install.

### 1.4 Map edit cost (NET-3 rate limit → MAP-12)
- **Why:** each map edit op re-serializes the whole map on the server's tick (several ms on Midtown). The editor legitimately sends up to 1024 ops at once (one custom group), so a per-op rate limit would desync honest editors. Batch 1 caps edits at 2 per player per tick (the rest are resent), which bounds the stall but slows big group edits to ~2 ops a tick.
- **Decide:** schedule MAP-12 (incremental grid updates, serialize only when saving), or accept the cap.
- **Recommendation:** schedule MAP-12 with batch 5.

### 1.5 Map loading speed (new finding, not in REVIEW)
- **Why:** `Map::parse` takes ~100 ms for Midtown and ~13 ms for Bastion, mostly from building an `istringstream` per line. It runs on every map load: the server tick on a map change, the client on join, votes before batch 1. This is separate from the 13-14 s lightmap prepass (PERF-2).
- **Decide:** approve a hand-written line parser (`strtof`) as a performance item.
- **Recommendation:** yes. Map changes are a tick stall today, and performance comes first (CLAUDE.md).

---

## 2. Release process and supply chain

### 2.1 Signed updates (SUP-1)
- **Why:** the launcher installs any asset named `quark-<platform>.qpak` with no hash or signature check. A leaked token or a compromised account or Action runs code on every player's machine.
- **Options:**
  - (a) Ed25519-sign each qpak in a protected release environment; embed the public key in the launcher and verify before extracting.
  - (b) Publish SHA256SUMS plus build attestations only.
  - (c) Leave it.
- **Needs from the owner:** creating and keeping a signing key (offline or a GitHub environment secret); Authenticode for the exes is optional.
- **Recommendation:** (a). The code can be prepared without the key.

### 2.2 Tag-only releases (SUP-2)
- **Why:** any push to any branch publishes `make_latest` releases with no concurrency group. v0.26.1 and v0.26.3 shipped assets built from other commits. There's no branch protection. CLAUDE.md currently says "every push builds and publishes".
- **Options:**
  - (a) Release only on `push: tags: ['v*']` from the default branch, with a concurrency group and "fail if the release exists"; protected main.
  - (b) Keep every push releasing, but only from the default branch.
  - (c) Leave it.
- **Recommendation:** (a), and update CLAUDE.md to match.

### 2.3 CI permissions and pinning (SUP-3, SUP-4)
- **Why:** every job has `contents: write`, and the token sits in `.git/config` during builds. No action is SHA-pinned. The pages job runs `eval()` on `gamedefs.h` with the write token.
- **Change:**
  - `contents: read` by default, `write` only in release and pages;
  - `persist-credentials: false`;
  - actions pinned by SHA (plus Dependabot);
  - a regex or `ast.literal_eval` in `site_changelog.py`.
  This is compatible with today's process.
- **Decide:** approve the workflow edit.
- **Recommendation:** approve. Low risk.

### 2.4 Dedicated-server auto-update (SUP-8)
- **Why:** servers build whatever is on the branch, as root, with no signed-tag check. A failed `--ff-only` merge silently stops updates. The hard-coded platform list froze every server on 1.14.1 for hours.
- **Decide:**
  - signed tags;
  - a dedicated user;
  - systemd hardening;
  - derive platforms from the release;
  - alert on a stalled update.
  This needs server operators to reinstall.
- **Recommendation:** yes, with the next server-side release.

### 2.5 Where player data lives (SUP-11)
- **Why:** `quark.key` (the ranked identity), `quark.cfg`, HUDs, controls, demos and so on live in the install folder, and uninstall deletes them.
- **Decide:** move them to per-user config and data directories (with a one-time migration) or keep them.
- **Recommendation:** move them, with a migration that copies and doesn't delete.

### 2.6 Shipping the launcher fixes (SUP-5, SUP-6, SUP-7)
- **What:** path-escape checks in the extractor, entry-size caps, atomic install, joining the thread, skipping prereleases, no auto-downgrade, DLL search order. All of it is code the fixers can write.
- **Decide:** when to ship a launcher update (players must get the new launcher).
- **Recommendation:** with the next release. The auto-downgrade guard also protects against the 1.1 risk.

### 2.7 Windows CI job (ENG-1) and the MSVC build (ENG-4)
- **Why:** the tests never run on Windows in CI, the main player platform. The MSVC CMake branch doesn't compile.
- **Decide:**
  - (a) add a Windows test job;
  - (b) fix the MSVC branch and add an MSVC job, or delete the MSVC branch. MinGW is what ships.
- **Recommendation:** (a) yes; for (b), delete the MSVC branch, keeping a global `NOMINMAX` and a correct build-type default.

### 2.8 Repo and build hygiene (ENG-6) and licence
- **Options:**
  - generate the 20 MB `voice_recorded.h` / `sfx_recorded.h` at build time;
  - Git LFS for the 36 MB of WAVs;
  - drop the global `QUARK_VERSION` define (every bump recompiles everything);
  - LTO;
  - CTest.
- **Licence:** the repo has no LICENSE file, so contributors have no stated terms.
- **Decide:** which of these to do, and which licence.
- **Recommendation:** add a LICENSE; generate the headers at build time; LTO after measuring the 1% / 0.1% lows.

---

## 3. Ranked and online services (Cloudflare Worker)

These need Worker deployments, and some change how ranked works. The code and local tests can be prepared (`tools/master/test.mjs`); deploying is the owner's.

### 3.1 Match tickets (WEB-1, WEB-2, WEB-3, WEB-4, NET-12)
- **Why:**
  - Anyone can farm rating to #1 with no game played (160 requests in a local mock).
  - Anyone who knows a match id can void it, and losers can void their own losses.
  - The host decides results.
  - Race times are self-reported.
  - Ranked ids are first-come.
- **Options:**
  - (a) Worker-issued match tickets at ranked match start, binding the player set and server. Only those players' reports count; a lone dissent becomes a dispute, not a void; leaving counts as a loss. Ranked ids prove the key by signing a server nonce.
  - (b) Lighter: only rate-limit and require established accounts.
- **Recommendation:** (a). It changes the ranked flow, hence an owner call.

### 3.2 Server list, quotas and data (WEB-5 … WEB-13)
- **Changes:**
  - per-IP list caps;
  - never trust `?ip=`;
  - POST-only writes without CORS `*`;
  - Cloudflare rate limits and cached GETs (the free plan's 100k requests a day can be exhausted);
  - atomic rating writes and queue fills;
  - indexes;
  - pruning;
  - a privacy note for `/crowns` and `/filled`.
- **Decide:** approve the Worker changes and deploy them.
- **Recommendation:** approve. Mostly hardening, no design change.

### 3.3 Worker URL and account (WEB-11)
- **Why:** the client fetches the Worker URL from the website's `master.txt`, which any push redeploys. The live value is a personal `*.workers.dev` subdomain, and whoever controls it receives every player's ranked secret.
- **Decide:**
  1. Confirm the project controls that Cloudflare account.
  2. Pin the URL in the binary, or sign `master.txt`.
- **Recommendation:** confirm ownership now, then pin the URL with a signed override.

### 3.4 Ranked rules (GAME-7)
- **Why:**
  - In 1v1, the caller's own yes passes a map change or "ranked off" mid-fight unless the opponent presses F2.
  - There's no rating decay.
  - Queues are FIFO with no rating window.
  - The placement threshold is 5 games in the client but 3 in the Worker.
  - On a listen server the host has ~0 ping.
- **Options:**
  - absolute-majority votes;
  - no map or ranked votes while a match is live;
  - rating decay;
  - rating-banded queues;
  - one placement threshold;
  - prefer dedicated servers for ranked.
- **Recommendation:** majority votes plus no votes while live (small, server-side); one threshold; decide decay and banding separately.

---

## 4. Server administration

### 4.1 Admin rights (NET-8)
- **Why:** admin is "the source IP is 127.x". Behind any local relay or tunnel, every remote player becomes admin.
- **Options:**
  - (a) an rcon-style admin password;
  - (b) admin only for the in-process host;
  - (c) both.
- **Recommendation:** (c): the listen-server host is admin, and a dedicated server sets a password.

---

## 5. Game design (weapon and movement numbers are the owner's)

### 5.1 Payload overtime (GAME-4)
- **Why:** a cart being pushed at 0:00 loses. In neutral payload, whoever is ahead of the middle at time-out wins, so a 1 m lead and a turtle wins.
- **Proposal:** overtime while the cart is touched by the team that would gain, as multi point does when it's fixed (GAME-3, batch 2).

### 5.2 The syringe (GAME-2b)
- **Why:**
  - It self-heals 10 hp/s against the 5 hp/s VS stalemate drain, so a cornered last player never dies.
  - A thrown syringe resets *every* cooldown (refire, dash, hook, knife), so two teammates get unlimited healing and dashes.
  - The heal cap per life (GAME-2a) is a bug fix in batch 2; the rest is design.
- **Proposal:**
  - ban the syringe in round modes and duel;
  - throws reset only dash and hook;
  - the stalemate drain must out-pace any heal (or drain by damage dealt).

### 5.3 Tuning items (GAME-6, GAME-8, GAME-9)
- **Knockback (GAME-6):** knockback stacks with no cap (six stickies give ~86 m/s, a ~170 m apex). In race and time trial it's free. Proposal: the strongest single knock per tick, a cap on self-knock, and a defined race loadout.
- **Knife throw (GAME-8):** the throw has no cooldown, so a throw plus a crossbow headshot is an instant 105. Proposal: a shared cooldown.
- **Size modifiers (GAME-9):** they scale the eyes and hitbox but not the hull ("double size" puts the eye at 3.2 m on a 1.8 m hull, so the camera goes inside low ceilings). Proposal: scale the hull.

### 5.4 Movement model (REVIEW §5.3)
- **Why:** auto-bhop, unbounded strafing, a dash that brakes above 29 m/s and a grapple that brakes on catch fight each other. Speed turns into height (11.9 m dash-ups), with no fall damage. Map grading doesn't model any of it.
- **Proposal:**
  - one speed model with a soft cap;
  - hook and dash *add* along their direction;
  - jump-timing bhop;
  - the real movement envelope in the map metrics.

### 5.5 Weapon balance (REVIEW §5.4)
- **Why:**
  - The time-to-kill band is flat: 1.5-3.0 s for every weapon.
  - The rifle (hitscan to 300 m, no spread or falloff) dominates mid and long range, and the sniper is strictly worse.
  - Bubble has the top DPS and is in the vanilla ranked mask, although the comment lists ids 0-7.
  - The "no single shot over 50" comment is stale.
  - The hitbox is axis-aligned, and the head zone is the full-width top 20% (shoulders count as headshots).
- **Decide:** the target TTK per weapon and role, whether bubble belongs in vanilla ranked, and the hitbox shape.

### 5.6 Smaller mode and design notes (REVIEW §5.2, §5.5)
- **KOTH:** one contester freezes the clock indefinitely.
- **Hide and seek:** rounds are `clamp(players, 3, 6)` in slot order (with 7+ some never seek), and hiders get +2 just for surviving.
- **Tourney:** a disconnect is an instant walkover (no reconnect grace), and seeding is random.
- **Feedback:** there's no headshot hit cue, the damage flash isn't directional, and remote players are silent in the air.
- **Bots:** perfect information (360° scan, exact leads), and their difficulty depends on the tick rate.

Each is a small design call; none blocks the bug batches.

---

## 6. Maps

- **Lodestone (MAP-6):** it offers every mode but is graded only for points. In bomb, site D: attackers arrive 10.8 s first. **Decide:** grade every offered mode (and fix the map), or offer fewer modes.
- **Midtown timing (MAP-8):** the defender lead is +12.7 s at D and +0.9 s at E, outside its own 2-10 s band. **Decide:** re-stage the spawns, and grade the worst point rather than the average (the latter is a metric fix, batch 5).
- **Editor vs generator (MAP-9):** the editor and the server autosave write the same `.qmap` that `make_maps.py` owns. **Decide:** generator output becomes a build artifact and editor work a per-map overlay of edit ops, or keep one of them as the source of truth.
- **Map storage and identity (MAP-13):** maps are identified by name + CRC32 and stored three ways (files, embedded hex, qpak). **Decide:** sha256 ids; shipped maps read-only; user maps, downloads and caches in a user directory (ties in with 2.5).
- **Generator structure (MAP-14):** **Decide:** split `make_maps.py` into `prefabs.py` plus one module per map, with `--map` and a keep-clear registry. Regenerating is byte-reproducible today, so this is about maintainability.

---

## 7. Discord bot operations

- **Channel flood (BOT-3):** one text and one voice channel per listed server, up to 200, while a category holds 50. One failed create aborts every tick. **Decide:** a cap on live channels and a minimum uptime before a server gets one. The per-server error handling is a code fix.
- **Install secrets (BOT-4):** `install.sh` passes the token and key through `sudo` arguments, visible in process lists, and the README's alternative unit file is world-readable. **Decide:** switch to `EnvironmentFile=` with mode 600 and rotate the bot token and key once redeployed.
- **Least privilege (BOT-5):** drop Mention Everyone (1.2) and Manage Roles beyond the bot's own roles; pin dependencies with hashes; extend `.gitignore` (`config.json`, `state.json`, `.venv`).
