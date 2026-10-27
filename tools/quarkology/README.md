# tools/quarkology: one-release hand-over

Quarkology (the Supervisor Board app) and every editor it installs moved to their own private repository,
[floration/quarkology](https://github.com/floration/quarkology) (2026-10-27). Its README says what moved and why; its
`quarkology/SPEC.md` is the app's spec now. Work on Quarkology or an editor happens there, not here.

This folder stays for one release so the installs on q's PCs move by themselves:

- It is a copy of the new app's code (`server.py`, `qo/`, `web/`, `shell/`, `setup.ps1`, ...) with a two-entry
  `installs.json` (`"sourcesIn": "game"`: Quarkology itself and Electron). An installed Quarkology from before the
  move updates itself from it, restarts with this code, and that code clones floration/quarkology into
  `Quark\quarkology-repo\` (hidden, with the PC's git or gh sign-in), switches to that clone's `installs.json` and
  reinstalls every editor from it once.
- While the clone can't be made, Quarkology keeps running and updating itself from here, with a red item on the
  Supervisor Board saying what to do.
- `tools/project_manager/` (the hand-over from the app's old name, Quark Project) runs `setup.ps1` from here.

Next release: delete this folder and `tools/project_manager/` (docs/HANDOVER.md "To delete next release").
Don't edit these files here: change floration/quarkology. The copy is refreshed only if a fix has to reach a PC that
can't clone yet.
