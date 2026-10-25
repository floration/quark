# Next (testing builds since 1.19.0)

What's on `testing` since production 1.19.0. The release PR renames this file to `changelogs/<VERSION>.md`.

## Testing builds
- **Testing builds are now numbered `1.19.0.testing.<N>`** (tag `test-1.19.0.testing.<N>`) instead of a new version each time: they keep the production version they follow and count up. The launcher's TESTING channel and the testing servers see them as newer than 1.19.0 and older than the next release, so nothing to do as a player.
- **Server owners, once:** a testing server's updater from before this change doesn't know the new tags and keeps the testing server stopped. Run `sudo sh tools/server/install.sh` once from a checkout of `v1.19.0` or newer (docs/OPERATIONS.md).

## Grapple
- **Everyone now wears a grapple launcher on their left arm**, in first person (low on the left, like the gun on the right) and on other players. The rope comes out of its barrel, and the hook's claw flies out to where it latched; while you're hooked your arm lifts toward it. It follows the viewmodel's HIDE setting and size, and isn't shown in modes without the grapple.

## Shots and explosions
- **Impact sparks everywhere**: hitscan shots (rifle, sniper, shotgun, laser) now spark where they hit a wall, not only on enemies, and other players' shots spark too. Settings > GAMEPLAY > SHOTS > IMPACT SPARKS turns them off.
- **Muzzle flash** on your gun and everyone else's when they fire. Settings > GAMEPLAY > SHOTS > MUZZLE FLASH turns it off.
- **Your explosions show at once**: your rockets and bolts blow up on your screen the moment they hit a wall or an enemy, and your stickies the moment you detonate, instead of a round trip later. Damage and deaths still come from the server, so nobody looks dead who isn't.
- **Weapon model opacity**: Settings > GAMEPLAY > VIEW > WEAPON MODEL OPACITY makes your first-person gun and grapple launcher see-through (1 = solid, 0 = gone).
