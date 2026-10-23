# The bot's move out of the game repository (done)

The bot used to live in the game's `tools/discordbot/`. On 2026-10-23 it moved to this branch, `discord-bot` of
`floration/quark` (split with its history; public like the rest of the repository). Its secrets never were in git:
they stay in `/etc/quark-discord.env` on the bot's server.

- **Moving the running bot to this branch** (once, on the bot's box): the game's `docs/OPERATIONS.md` section 2.
  `autoupdate.sh` here follows this branch's head by default (`QUARK_BOT_FOLLOW=branch:discord-bot`).
- **Constants copied from the game** (`quarkdata.py`: protocol, info reply, mode names; `bot.py`: queues): re-copy them
  by hand when the game changes them. The game's `quark_tests` checks them when run with
  `QUARK_BOT_DIR=<checkout of this branch>`; CI can't, so run that before releasing a protocol, mode or queue change.
- **Tests here:** `python -m unittest test_quarkdata`; with `QUARK_REPO=<game checkout>` the tests that need the
  game's files run too.
