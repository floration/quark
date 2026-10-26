# Next (testing builds since 1.19.0)

What's on `testing` since production 1.19.0. The release PR renames this file to `changelogs/<VERSION>.md`.

## Testing builds
- **Testing builds are now numbered `1.19.0.testing.<N>`** (tag `test-1.19.0.testing.<N>`) instead of a new version each time: they keep the production version they follow and count up. The launcher's TESTING channel and the testing servers see them as newer than 1.19.0 and older than the next release, so nothing to do as a player.
- **Server owners, once:** a testing server's updater from before this change doesn't know the new tags and keeps the testing server stopped. Run `sudo sh tools/server/install.sh` once from a checkout of `v1.19.0` or newer (docs/OPERATIONS.md).

## Grapple
- **Everyone now wears a grapple launcher on their left arm**, in first person (low on the left, like the gun on the right) and on other players. The rope comes out of its barrel, and the hook's claw flies out to where it latched; while you're hooked your arm lifts toward it. It has its own HUD > GRAPPLE MODEL setting and isn't shown in modes without the grapple.
- **The gun and the grapple launcher now sit further back in the bottom corners** by default, covering less of the screen, and in the same place at every aspect ratio (16:9, 4:3, ultrawide) and FOV.
- **New HUD > GRAPPLE MODEL setting**: move, size, hide and colour the launcher on its own, separately from the WEAPON MODEL.
- **Tilt for both models**: PITCH, YAW and ROLL sliders (-90 to 90 degrees) next to x / y / depth, so each can be placed on all 6 axes. Your shots and the rope leave the moved, tilted barrels.

## Shots and explosions
- **Impact sparks everywhere**: hitscan shots (rifle, sniper, shotgun, laser) now spark where they hit a wall, not only on enemies, and other players' shots spark too. Settings > GAMEPLAY > SHOTS > IMPACT SPARKS turns them off.
- **Muzzle flash** on your gun and everyone else's when they fire. Settings > GAMEPLAY > SHOTS > MUZZLE FLASH turns it off.
- **Your explosions show at once**: your rockets and bolts blow up on your screen the moment they hit a wall or an enemy, and your stickies the moment you detonate, instead of a round trip later. Damage and deaths still come from the server, so nobody looks dead who isn't.
- **Screenshots no longer overwrite each other**: the screenshot key (F12) now saves to a `screenshots` folder in your game data folder, named by date and time, and the notice says the file name. Before, every launch started again at screenshot_0.png and replaced the last session's.
- **Weapon model opacity**: Settings > GAMEPLAY > VIEW > WEAPON MODEL OPACITY makes your first-person gun and grapple launcher see-through (1 = solid, 0 = gone).
- **More detailed weapon models**: every first-person weapon except the locator is rebuilt with more parts (sights, rails, grips, glowing details), up to 20 boxes each. Their size and where they sit on screen are unchanged.
- **New player models**: WIZARD, ZOMBIE and FIGHTER, plus SIMPLE (just the hitbox: a body box and a head box), which replaces SCOUT (anyone who picked SCOUT now gets SIMPLE). STANDARD, HEAVY, ROBOT and KNIGHT and every projectile (rocket, sticky, bolt, knife, syringe, bubble, orb) are rebuilt with more detail. Every model still has the same hitbox.
- **More player models**: PIRATE, NINJA and BANDIT.
