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
- **New map material: NOGRAPPLE** (slate blue with diagonal stripes). It is solid like a normal wall for players, shots and sight, but your grapple hits it and lets go instead of latching on. In the editor it comes after CLIP on the M key and the material list. Maps saved by this version are format 3; older games cannot open maps that use it.
- **New map material: ACID** (glowing toxic green). You wade into it like lava, but instead of killing you at once it takes 10% of your max health when you touch it, then 10% a second while you stay in, so anyone lasts about 9 seconds whatever the mode's health. Your health drops at once on your screen; the death still comes from the server ("dissolved" in the kill feed), and whoever knocked you in gets the kill however long it takes. Regeneration waits while you're in it, and bots walk round it when they can. In the editor it comes after NOGRAPPLE. Maps saved by this version are format 4; this is a new network protocol (45), so servers and players need this version.
- **The head hitbox is just the head**: a player is now hit as two boxes, the body (full width, from the feet up to the bottom of the head) and the head on top. The space beside the head, above the shoulders, is no longer part of the hitbox, so a shot past someone's ear misses instead of counting as a body hit. The same for every weapon, projectile, the knife and the grapple, crouched or any size. Blasts measure to the body or the head too, and bots aim a little lower so they keep hitting the body. This is a new network protocol (46), so servers and players need this version.
- **Fixed missing box faces**: some faces of stairs and other stacked boxes were invisible in game. The check that hides faces buried inside other boxes was approximate and could hide a face that was still partly visible; it is now exact, and invisible clip boxes no longer hide the faces behind them.

## Sounds
- **The bomb going off now makes a sound**: a deep, long boom at the bomb that everyone hears. Before, the round just ended in silence.
- **Tag passes are heard**: being tagged plays a stab and a sting, tagging someone plays a heavy hit sound (with HITSOUND on), and everyone else hears the new "it" get hit.
- **Race practice checkpoints ding** (CHECKPOINT and FINISH), like in the race itself. Before, practice laps before the race were silent.
- **Demos: the recorded player's own shots are heard** again. Before, their gunfire was silent in playback.

## HUD
- **Edit the HUD on screen**: Settings > HUD > EDIT ON SCREEN shows every HUD element where it goes, with the score box, kill feed and chat too. Click one to select it, drag it to move it (it snaps to the screen's edges and centre and to the other elements, with guide lines; hold Alt to place it freely), and drag a corner, use the wheel or press + / - to size it. Arrow keys nudge it (Shift: 10 at a time), Tab picks the next element, Delete hides or shows it, and Ctrl+Z / Ctrl+Y undo and redo. A small panel beside it has the element's settings and its style, plus RESET ELEMENT, RESET ALL and DONE.
- **HUD styles (theme.cfg)**: give any HUD element a background, border, rounded corners, padding, opacity, text size and line spacing, from the on-screen editor or by writing `theme.cfg` (next to `hud.cfg`, in a short CSS-like format). It changes live in a running game. With no theme the HUD looks exactly as before. In ranked, a theme can dim an element to 30% at least, never hide it.
- **The score box, kill feed and chat are now HUD elements**: move, size, recolour and style them like the others (Settings > HUD and EDIT ON SCREEN).
- **Fixed the HUD tab's footer covering the weapon list**: BACK and RESET WHOLE HUD now sit higher, clear of the HUD's bottom row.
- **The HUD and menus draw a little cheaper**: the HUD costs slightly less CPU per frame than before even with all of the above, and every piece of text and every box in the UI sends a third less data to the GPU.
