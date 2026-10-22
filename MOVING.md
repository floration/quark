# Moving the bot to its own private repository

The owner wants the bot's code where only they can see it. This folder is self-contained (it imports nothing from
the game and reads no game files; `test_quarkdata.SelfContained` checks that), so it can become its own repository
with its history.

**Read first:** the bot's code up to now is already public in `floration/quark`'s history. A private repo hides
everything from the move on; it does not un-publish old commits. That's fine: the bot keeps no secrets in code (the
token and `BOT_KEY` live in `/etc/quark-discord.env` on the server), and the secrets are rotated below anyway. Don't
rewrite the public repo's history for this: every clone and fork already has it.

## 1. Make the private repo, with the folder's history

On a machine with a clone of `floration/quark` (with every commit that should go along, i.e. after these fixes are
merged):

```
cd quark
git subtree split --prefix=tools/discordbot -b discordbot-only    # a branch whose root is this folder, its history kept
```

Create an empty **private** repo on GitHub (New repository -> owner floration -> name e.g. `quark-discord-bot` ->
**Private**, no README / licence / .gitignore so it stays empty). Then:

```
git push git@github.com:floration/quark-discord-bot.git discordbot-only:main
git branch -D discordbot-only
```

Check it: `git clone git@github.com:floration/quark-discord-bot.git /tmp/qbot && cd /tmp/qbot && git log --oneline | head`
shows the bot's commits, and `ls` shows `bot.py`, `quarkdata.py`, ... at the top level.
Settings -> Collaborators: nobody else. (Branch protection on `main` is optional for a one-person repo.)

Tests in the new repo:
```
python3 -m venv .venv && .venv/bin/pip install --require-hashes -r requirements.txt
.venv/bin/python -m unittest test_quarkdata               # 2 tests that need the game's files are skipped
QUARK_REPO=~/quark .venv/bin/python -m unittest test_quarkdata   # with a game checkout: those run too
```

## 2. Remove it from the game repo

In `floration/quark`, on the testing branch (it reaches main through the normal release flow):

```
git rm -r tools/discordbot
```
and in the same commit:
- `tests/tests.cpp` `testCrossLanguageConstants`: see "The game's cross-language test" below (as it stands it would
  silently skip *all* of its checks, including the ones that don't involve the bot).
- `src/client/community.cpp` line 2 mentions `(tools/discordbot)`: say "the Discord bot" instead.
- Old changelogs and `docs/REVIEW.md` / `docs/FIXES_HANDOFF.md` / `docs/OWNER_DECISIONS.md` mention the path; they're
  history, leave them.

## 3. Move the running bot on the server

The service currently runs from the game checkout (`WorkingDirectory=.../quark/tools/discordbot`). Its settings
(`/etc/quark-discord.env`) aren't in either repo and stay where they are.

1. Give the server read access to the private repo: GitHub -> the new repo -> Settings -> Deploy keys -> Add deploy
   key (read-only), with the server's public key (`ssh-keygen -t ed25519 -f ~/.ssh/quark_bot -N ''` as the bot's
   user, paste `~/.ssh/quark_bot.pub`). Then, as that user:
   ```
   GIT_SSH_COMMAND='ssh -i ~/.ssh/quark_bot' git clone git@github.com:floration/quark-discord-bot.git ~/quark-discord-bot
   git -C ~/quark-discord-bot config core.sshCommand 'ssh -i ~/.ssh/quark_bot'
   ```
2. Keep the bot's memory (last announced release, message and channel ids) so it doesn't repost or duplicate channels:
   `cp ~/quark/tools/discordbot/state.json ~/quark-discord-bot/` (and `config.json` if you made one).
3. `cd ~/quark-discord-bot && sudo ./install.sh` - press Enter at each prompt to keep the saved values. It rewrites
   the systemd unit to point at the new folder, makes a fresh venv with the hash-checked requirements and restarts
   the bot.
4. `journalctl -u quark-discord -f`: look for `ready as ...`.
5. Optional: the bot fetches the INFO pages (`game.cpp`) from GitHub when there's no local game checkout. To keep
   using a local one, add `QUARK_REPO=/home/<user>/quark` to `/etc/quark-discord.env`.
6. Once it runs from the new folder: `rm -rf ~/quark/tools/discordbot/.venv ~/quark/tools/discordbot/state.json`.

Updating later: `cd ~/quark-discord-bot && git pull && sudo ./install.sh` (Enter at the prompts).

## 4. Rotate the secrets

Older installs passed the token and key on a `sudo env ...` command line (visible in `ps`). After the redeploy:
- Discord developer portal -> the app -> Bot -> **Reset Token**.
- Cloudflare -> the master worker -> Settings -> Variables and Secrets -> set a new **BOT_KEY**
  (`python3 -c "import secrets;print(secrets.token_hex(32))"`).
- `sudo ./install.sh` again and paste both new values (the token prompt is hidden).

## What the bot copies from the game (keep in sync by hand)

| In the bot | From the game | Changes when |
|---|---|---|
| `quarkdata.py` `PROTOCOL_MAGIC`, `PKT_INFO_REQUEST`, `PKT_INFO_REPLY`, `INFO_REQUEST_BYTES` | `src/game/gamedefs.h`, `src/game/protocol.h` | the network protocol changes |
| `quarkdata.py` `parse_info_reply` layout | `server.cpp` info reply | the info reply changes |
| `quarkdata.py` `MODE_NAMES`, `MODE_TITLES` | `gamedefs.h` `modeName` / `modeTitle` | a mode is added or renamed |
| `bot.py` `QUEUES` | `src/client/community.cpp` `kQueues`, `tools/master/worker.js` `QUEUE_SIZES` | queues change |
| `quarkdata.py` `RANKED_LADDERS`, `RACE_MAP` | ranked ladders, the time-trial map | ranked changes |
| GitHub releases / tag format (`v1.2.3`, prerelease `v1.2.3-test.N`) | `.github/workflows/release.yml` | the release process changes |

## The game's cross-language test

`tests/tests.cpp` `testCrossLanguageConstants` reads `tools/discordbot/bot.py` and `tools/discordbot/quarkdata.py`
and today **returns early if any source is missing**, so after the move it would also stop checking
`community.cpp` against `worker.js`. When removing the folder, change it to:
1. always run the `community.cpp` vs `worker.js` queue check (both stay in the game repo);
2. run the bot checks (queue titles/sizes in `bot.py`, `PROTOCOL_MAGIC` / `PKT_INFO_*` / `INFO_REQUEST_BYTES`,
   `MODE_NAMES` / `MODE_TITLES` in `quarkdata.py`, and the live info-reply layout) only when an env var such as
   `QUARK_BOT_DIR=<private checkout>` points at the bot, printing "skipped" otherwise. `readSource` would read
   `$QUARK_BOT_DIR/bot.py` and `$QUARK_BOT_DIR/quarkdata.py` instead of `tools/discordbot/...`.
CI in the public repo can't see the private repo, so there the bot half is skipped; run
`QUARK_BOT_DIR=~/quark-discord-bot build/quark_tests` locally before releasing a protocol or mode change, and
`QUARK_REPO=~/quark python -m unittest test_quarkdata` in the bot repo.
