# Next (testing builds since 1.19.0)

What's on `testing` since production 1.19.0. The release PR renames this file to `changelogs/<VERSION>.md`.

## Testing builds
- **Testing builds are now numbered `1.19.0.testing.<N>`** (tag `test-1.19.0.testing.<N>`) instead of a new version each time: they keep the production version they follow and count up. The launcher's TESTING channel and the testing servers see them as newer than 1.19.0 and older than the next release, so nothing to do as a player.
- **Server owners, once:** a testing server's updater from before this change doesn't know the new tags and keeps the testing server stopped. Run `sudo sh tools/server/install.sh` once from a checkout of `v1.19.0` or newer (docs/OPERATIONS.md).

## Grapple
- **Everyone now wears a grapple launcher on their left arm**, in first person (low on the left, like the gun on the right) and on other players. The rope comes out of its barrel, and the hook's claw flies out to where it latched; while you're hooked your arm lifts toward it. It follows the viewmodel's HIDE setting and size, and isn't shown in modes without the grapple.
