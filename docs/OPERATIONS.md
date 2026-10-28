# Operations: automatic updates, and keeping secrets secret

There are three things to keep up to date after a release, and all three can update themselves:
- the dedicated servers;
- the Discord bot;
- the ranking server (the Cloudflare Worker and its D1 database).

This page says how to switch each one on and check it works. Then it covers every key and secret the project has: where each one lives, how to keep it out of git, logs and chats, and what to do if one leaks.

Every automatic update follows **production releases only**, meaning a `v<x.y.z>` tag on the default branch. Testing builds never reach the bot or the Worker. The second dedicated server on each box follows testing builds on purpose.

---

## 1. Dedicated servers (automatic since 1.17.0)

`tools/server/install.sh` sets everything up: a production server and a testing server on the next port, each with an update timer.

**Once per box** (or again after pulling a new `install.sh`):
```
git clone https://github.com/floration/quark /opt/quark-src   # (any folder; install.sh copies what it needs)
cd /opt/quark-src && sudo sh tools/server/install.sh            # --no-testing: production only; --port N
```
Forward the UDP port, and the next port up for the testing server.

**What the updater does:**
- **Production server:** every minute, it updates to the newest production release.
- **Testing server:** every 2 minutes, it updates to the newest testing build. It only runs while that build is newer than production. Testing builds are `<VERSION>.<N>` (1.19.0 to 1.19.1: `<VERSION>.testing.<N>`) with `VERSION` at the last production release, so they sort above it; the build a production release was merged from counts as caught up too (its commit is in the production tag). When production catches up (same update), it stops after its current match and starts again by itself with the next newer testing build (`/var/lib/quark-testing/active`, `quark-testing-active.path`).
  - **Once, for the 1.19.1 numbering switch:** an updater from before it only knows `test-X.Y.Z.testing.N` tags, so it never sees a `test-X.Y.Z.N` build: it keeps the testing server on its last build, and once production 1.19.1 is out (newer than that build) it stops the testing server for good (a testing updater never builds production). On each box run `sudo sh tools/server/install.sh` once from a checkout of the `testing` branch (to run 1.19.1.1 before the release) or of `v1.19.1` or newer: it re-clones both instances, and the testing updater then follows the new builds. **This is the last time** (owner, 2026-10-28: "once install.sh is run a single time, you should not have to rerun it"): from this version on the updater reads any `test-X.Y.Z<suffix>` tag (the signed manifest is what's checked), and a testing updater installs a production release that is newer than every testing build (the testing server stays stopped), so its own code never falls behind production. Only a change to the systemd units themselves would need install.sh again; keep those stable.
  - **Once, for the 1.19.0 numbering switch:** a testing server's updater runs from its own checkout (`/opt/quark-testing`), which sits on the last testing build it built. An updater from before the switch only knows `test-X.Y.Z-test.N` tags, so it never sees a `test-X.Y.Z.testing.N` build and keeps the testing server stopped. On each box, after production 1.19.0 is out, run `sudo sh tools/server/install.sh` once from a checkout of `v1.19.0` (or newer): it re-clones both instances from that commit, and the testing updater then follows the new builds.
- **Each update:**
  - It checks that the release's manifest is signed with the release key, so a release nobody signed is never installed.
  - It builds that exact commit and restarts the server between matches.
  - It never moves to an older version.

**Check it:**
```
systemctl list-timers 'quark*'                       # quark-update.timer, quark-testing-update.timer
journalctl -u quark-update -u quark-testing-update -n 50
```

**Alerts:** put a Discord webhook in `/etc/quark/update.env` as `QUARK_ALERT_WEBHOOK=...`. A stuck update (bad signature, failed build, a release incomplete for over an hour) posts there, and is logged at critical priority (`journalctl -p crit -t quark-update`). The file is mode 600. A webhook URL is a secret (section 4).

**GitHub rate limit (optional):** `GITHUB_TOKEN=...` in the same file raises GitHub's limit of 60 requests an hour per address.
- Use a fine-grained token with **no permissions at all**. Public repositories need none, and a token that can't do anything can't be misused.
- Give it an expiry date.

**Pause updates:** `sudo systemctl disable --now quark-update.timer` (`quark-testing-update.timer` for the testing server).

## 2. Discord bot (automatic once you re-run its installer)

The bot's code lives on the **`discord-bot` branch** (its files at the top level), not in the game's branches. Its
`install.sh` installs `quark-discord-update.timer`. Every 10 minutes it runs `autoupdate.sh`, which:
1. finds what to follow: the head of the `discord-bot` branch (its default, `QUARK_BOT_FOLLOW=branch:discord-bot`);
2. if the bot's files are different there, checks it out (never going backwards) and reinstalls the hash-pinned requirements if they changed;
3. runs the bot's offline tests and a dry run, which reads live data, posts nothing and gets no secrets;
4. restarts the bot.

If step 2 or 3 fails, it goes back to the commit it was on, logs `ERROR`, and the bot keeps running the old code.
Only push to `discord-bot` what you want running.

**Once, on the bot's box: move the bot to the branch.** Before the first production release without
`tools/discordbot/` (1.17.1). Until then the old checkout keeps working; after that its updater can't find the bot's
folder at the new tag, logs `ERROR` every 10 minutes and keeps the old code running. As the user the bot runs as:
```
git clone -b discord-bot https://github.com/floration/quark ~/quark-discord-bot
cp ~/quark/tools/discordbot/state.json ~/quark-discord-bot/   # the bot's memory (and config.json, if you made one)
```
Then:
```
cd ~/quark-discord-bot && sudo ./install.sh   # Enter at each prompt keeps the saved token / server id / BOT_KEY;
                                              # it points both services at this folder and restarts the bot
journalctl -u quark-discord -f                # "ready as ..."
```
Optional: `QUARK_REPO=/home/<user>/quark` in `/etc/quark-discord.env` lets the bot read the INFO pages (`game.cpp`)
from a local game checkout instead of downloading them. Once it runs from the new folder:
`rm -rf ~/quark/tools/discordbot/.venv ~/quark/tools/discordbot/state.json`.

**Check it:**
```
systemctl list-timers quark-discord-update.timer
journalctl -u quark-discord-update -n 50       # "updated to origin/discord-bot and restarted", or ERROR lines
```

**Tests that need both:** `quark_tests`' cross-language check compares the bot's copies of the queues, protocol
constants and mode names with the game. CI can't see the bot, so run `QUARK_BOT_DIR=<discord-bot checkout>
build/quark_tests` before releasing a protocol, mode or queue change (and `QUARK_REPO=<game checkout> python -m
unittest test_quarkdata` in the bot's checkout).

**Pause updates:** `sudo systemctl disable --now quark-discord-update.timer`.

The bot's secrets stay in `/etc/quark-discord.env` (mode 600). The updater never reads them.

## 3. Cloudflare: the ranking server (automatic once you add the keys to GitHub)

Until now each version meant pasting `worker.js` into the dashboard and running its SQL by hand. Now the release workflow has a `deploy-worker` job. It runs after every production release and calls `tools/master/deploy.sh`, which:
1. applies the D1 migrations in `tools/master/migrations/` that haven't been applied yet;
2. deploys `worker.js` with its D1 binding (`DB`) and its cron (`*/15 * * * *`);
3. checks that the Worker answers.

The job does nothing until you switch it on.

**Switching it on** (about 10 minutes, once):
1. **An API token for CI.** Cloudflare dashboard → your profile → **API Tokens** → **Create Token** → **Custom token**:
   - **Permissions:** `Account · Workers Scripts · Edit` and `Account · D1 · Edit`. Nothing else.
   - **Account resources:** your account only.
   - **TTL:** an end date, for example a year, with a calendar reminder to replace it.
   - Copy the token **once, straight into GitHub** (step 3). Don't save it anywhere else.
2. **Collect three values that aren't secret.**
   - The **Account ID**, on the Workers overview page.
   - The **D1 database ID**: Storage & Databases → D1 → `quark`.
   - The Worker's **compatibility date**: the Worker → Settings → Runtime. Keep the date it has; a new date can change how the Worker behaves.
3. **GitHub.** Repository → Settings:
   - **Environments → `production` → Environment secrets:** `CLOUDFLARE_API_TOKEN` (the token) and `CLOUDFLARE_ACCOUNT_ID`.
   - **Secrets and variables → Actions → Variables** (repository variables, not secrets):
     - `QUARK_D1_ID` = the database ID;
     - `QUARK_WORKER_COMPAT_DATE` = the date;
     - `DEPLOY_WORKER` = `true`.
4. **First deploy by hand,** to see it work before CI does it. On your machine, with Node 18 or newer:
   ```
   read -rs CLOUDFLARE_API_TOKEN && export CLOUDFLARE_API_TOKEN   # paste; it isn't shown or saved in history
   export CLOUDFLARE_ACCOUNT_ID=... QUARK_D1_ID=... QUARK_WORKER_COMPAT_DATE=YYYY-MM-DD
   sh tools/master/deploy.sh
   ```
   - It applies `0001_baseline.sql`, which changes nothing and only records the starting point, then deploys the same `worker.js` that is live.
   - `ADMIN_KEY` and `BOT_KEY` stay set on the Worker; a deploy keeps them.
   - **Before running it:** if you ever added another binding or variable in the dashboard, add it to `deploy.sh` first. A deploy makes the Worker match the script, apart from secrets and variables, which it keeps.

**From then on:**
- **Each production release deploys the Worker automatically,** right after the release is published. If it fails, the release itself is unaffected; fix and run the job again from the Actions tab.
- **A schema change** is a new file `tools/master/migrations/0002_<what>.sql` (then 0003, and so on). Make changes additive (new tables, columns, indexes) so the Worker that is still running keeps working while the migration applies. Update `schema.sql` to match, and run the Worker tests with `node tools/master/test.mjs` (Node 22.5 or newer).
- **The Worker must keep answering older games,** because players update over hours or days. It already checks the version each game sends.
- **Rolling back:**
  - the Worker: Cloudflare → the Worker → **Deployments** → roll back;
  - the database: D1 **Time Travel** (`npx wrangler d1 time-travel restore quark --timestamp=<before>`).

Things that stay manual: the Worker's own secrets (`ADMIN_KEY`, `BOT_KEY`, below) and moving the Worker to a new address (`site/master.txt`, signed on your own machine: tools/master/README.md step 6).

---

## 4. Keeping secrets secret

### Every secret, where it lives, and how to replace it

| Secret | What it protects | Where it lives (only there) | Replace it (if it leaked, or on a schedule) |
|---|---|---|---|
| `QUARK_SIGNING_KEY` | Every release. The launcher, dedicated servers and games trust only files signed with it. **The most important one.** | GitHub environment secrets (`production`, `testing`), plus an offline backup in your password manager. Never on a server, never in a file in the repo. | docs/SIGNING.md "Making a new key": a new keypair, its public key built into the game, the secret replaced. Installed launchers trust only the old key, so every player downloads the new launcher once from the website. |
| `CLOUDFLARE_API_TOKEN` | Deploying the Worker and changing D1 | GitHub `production` environment secret | Cloudflare → API Tokens → roll or delete it, make a new one (section 3), update the GitHub secret. |
| `ADMIN_KEY` | The Worker's dispute endpoints | The Worker's secrets (Cloudflare dashboard) and your password manager | `openssl rand -hex 32` → the Worker → Settings → Variables and Secrets → edit. |
| `BOT_KEY` | The Worker's bot-only endpoints (queues, link codes) | The Worker's secrets, and `/etc/quark-discord.env` on the bot's box | Make a new one, set it on the Worker, then re-run `sudo ./install.sh` in the bot's checkout (the `discord-bot` branch) on the bot's box and type the new one. |
| `DISCORD_TOKEN` | Acting as the bot in your Discord server | `/etc/quark-discord.env` on the bot's box | Discord developer portal → the app → Bot → **Reset Token** → re-run the bot's `install.sh`. |
| `QUARK_ALERT_WEBHOOK`, any Discord webhook URL | Posting into that channel | `/etc/quark/update.env` on each dedicated server | Discord → the channel → Integrations → Webhooks → delete it and make a new one. |
| `GITHUB_TOKEN` on servers (optional) | Nothing, if it has no permissions (section 1) | `/etc/quark/update.env` | GitHub → Settings → Developer settings → tokens → revoke, make a new one. |
| Players' `quark.key` | A player's ranked identity | The player's own machine, next to `quark.cfg`. Only ever sent to the Worker. | (Theirs.) The repo ignores `*.key` so a developer's own never gets committed. |

The Cloudflare account ID, the D1 database ID and the Worker's address aren't secret. Keep them out of the repo anyway when there's no need for them there; they're repository variables (section 3).

### Rules that keep secrets out of git

1. **Turn on the pre-commit check in every clone** (once):
   ```
   git config core.hooksPath tools/githooks
   ```
   Then every commit runs `tools/check_secrets.py --staged`. It refuses commits that contain:
   - private key blocks, Discord bot tokens or webhook URLs, GitHub tokens or cloud API keys;
   - a long value assigned to one of the names above (`BOT_KEY=...`, and so on);
   - files that must never be committed: `quark_signing_key.txt`, `quark.key`, `*.pem`, `.env` / `*.env`, the bot's `config.json` and `state.json`.

   It never prints the secret itself. CI runs the same check on every push (the `meta` job), so a miss still fails the build. By then the secret has reached GitHub, which is what the hook is there to prevent.
2. **Turn on GitHub's own protection:** repository → Settings → **Code security**:
   - **Secret scanning** on, and **Push protection** on: GitHub refuses a push that contains a known token format;
   - **Dependabot alerts** on.
3. **`.gitignore` already covers** keys (`*.key`), configs, the bot's `config.json` / `state.json` / `*.env` / `.venv/`, and the generated `wrangler.toml`. Don't force-add an ignored file (`git add -f`).
4. **If a secret was committed,** even on a branch, even if the commit was undone right away: **replace it** (the table above). Deleting it from history isn't enough, because clones, forks, CI logs and caches may already have it.

### In CI (GitHub Actions)

- **Secrets are environment secrets,** given only to the jobs that need them:
  - `release`, `pages` and `deploy-worker` run in `production`;
  - `release-testing` runs in `testing`.
  - Keep it that way. Never move a secret to the repository level, and never pass one to a build or test job.
- **Make the `production` environment ask you first:** Settings → Environments → `production` → **Required reviewers**: yourself. A tag push then waits for your click before anything can use the signing key or the Cloudflare token. It costs one click per release and protects against a stolen push credential.
- **Protect the tags:** Settings → Rules → **Rulesets** → New tag ruleset, targeting `v*` and `test-*`:
  - restrict creation, update and deletion;
  - only you on the bypass list.

  A `v*` tag is a production release, so only you should be able to make one.
- **Workflow rules:**
  - never `echo` or `cat` a secret (GitHub masks known secrets in logs, but not text derived from them);
  - keep `persist-credentials: false` on every checkout;
  - never use `pull_request_target`, which would run fork code with secrets;
  - pin actions by commit SHA, as `release.yml` already does;
  - Dependabot keeps the SHAs current.

### On the servers

- **Secret files** are `/etc/quark-discord.env` and `/etc/quark/update.env`. They are mode 600 and owned by the service's user, and systemd reads them with `EnvironmentFile=`, so a secret never appears on a command line or in `ps`.
- **When you type a secret,** use `read -rs NAME` (silent, not in history). Never `export KEY=value` on the command line.
- **Don't paste a server's `journalctl` or `systemctl show` output publicly** without reading it first. The scripts never print secrets, but other software on the box might.
- **Server logins:** SSH keys only (`PasswordAuthentication no`), and keep the OS updated (`unattended-upgrades` on Ubuntu / Debian).

### With Claude (and any other assistant)

- **Never paste a key, token or webhook URL into a chat,** or into an issue or pull request. If a step needs one, the step tells you where to put it (GitHub settings, Cloudflare, `install.sh`'s silent prompt). Claude never needs to see it.
- **The committed `.claude/settings.json`** tells Claude Code never to read the secret files in a checkout or on the servers: `quark_signing_key.txt`, `*.key`, `*.env`, `/etc/quark-discord.env`, `/etc/quark/*`. Keep those rules if you edit the file.
- **Before letting a session push,** run `python3 tools/check_secrets.py`. It's quick, and the pre-commit hook runs it for every commit anyway.

### If something leaked

1. **Replace it now** (the table above). Revoking comes first, before cleaning up anything else.
2. **Check what it was used for:**
   - Cloudflare → **Audit Logs**;
   - GitHub → Settings → **Security log** (and the Actions runs);
   - Discord → Server Settings → **Audit Log**.
3. **For the signing key,** follow docs/SIGNING.md "Making a new key" at once, and treat any release you didn't make as hostile: delete it from GitHub Releases and tell players.
4. Then remove it from wherever it leaked (a commit, a log, a chat), knowing it's already burned.
