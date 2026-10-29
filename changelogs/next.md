# Next (testing builds since 1.19.1)

What's on `testing` since production 1.19.1. The release PR renames this file to `changelogs/<VERSION>.md`.

## Performance
- **New Settings > VIDEO > PRIORITIZE GAME (Windows, on by default)**: while the game has focus, Windows runs its GPU work before other programs' (a YouTube tab, Discord or other hardware-accelerated apps) and gives it a higher CPU priority. Before, a video playing in the background could take a big share of every frame. Alt-tab and the other programs get their normal share back.

- **Blown-open brick and cobblestone walls no longer cut your FPS**: after holes were blown in a wall (or the whole wall fell), every brick and cobblestone pixel on screen kept checking every hole on the map, so a room of brick could drop to a tenth of the frame rate. Each pixel now checks only the holes near it.

## Maps
- **Wedges and corners**: two new map pieces besides boxes. A wedge is a ramp (one top edge kept), a corner a pyramid with one top corner kept; the slope is whatever the box gives, so any angle. They can stand on the floor, hang upside down from a ceiling or lie on their side (an angled wall). Slopes up to 45 degrees are walkable (no hopping), steeper ones are walls you slide off or along; a dash into one rides up it (or down it). Shots, the grapple and light all follow the slope, and bots walk up gentle ones. In the map editor, N cycles a box through them and rotate / mirror turn them any way; Quarkology's map editor has Wedge and Corner buttons. Wedges and corners can be grouped with boxes, and the seams between pieces of one group go away, slopes included. Maps with them are format 5 (older maps load unchanged); older game versions can't join servers running this one (protocol 48).
- **Fewer stray outlines**: where a box's edge was only partly against a flush neighbour (a taller step next to a shorter one, a wall meeting a longer wall), the whole edge kept its line; now only the uncovered part does. In hand-made maps, outlines now follow groups: parts of the same group join with no line, even across materials, and separate groups or ungrouped boxes keep theirs. The official maps keep joining same-material boxes.

## Weapons
- **Rifle: +1 damage a shot**: 6 body (was 5), 8.5 head (was 7.5). A kill takes 17 body shots in 2.4 s (was 20 in 2.9 s), or 12 headshots in 1.7 s.
- **Bubble gun fires 20% faster**: 6 bubbles a second (was 5), so 10 pops kill in 1.5 s (was 1.8 s).
- **Bubble gun alt-fire: up to 25 bubbles** a stream from 30 m away (was 15). Still 5 when the nearest enemy is within 5 m, and the same 1.8 s reload.
- **33% more knockback** from the shotgun (a full blast shoves 8 m/s, was 6) and the crossbow (21.3 m/s a bolt, was 16).
- **Sniper: 33% more knockback** (1.33 m/s a hit, was 1).

## Movement
- **Dashing up goes higher**: looking straight up, 50% of a dash goes upward (was 40%).
- **Air dashes add more speed the slower you are**: a dash in the air now adds 35% of the gap between your speed and the 32 m/s cap. From 10 m/s you leave at 17.7 (was 11), from 25 at 27.5 (was 26), from a standstill at 11.2. Dashes on the floor and super dashes (hypers) are unchanged.

## Downloads
- **Smaller game downloads in the launcher (coming)**: every release now also publishes a compressed copy of the game package, about half the size (Windows: about 40 MB instead of 72). The launcher starts using it once its own update ships; until then nothing changes, and the old package stays for older launchers.
- **Releases no longer wait for a Mac**: GitHub often has no Mac free for hours, which held releases back (1.19.1 waited for one). Windows and Linux now go out as soon as they're built, and the macOS files follow when a Mac is free.

- Human-made assets: 16.1%, unchanged (no asset changes).
- **Surf**: a new mode. Race rules (checkpoints in order, the first to the finish wins the round) on a course of steep ramps, with no dash and no grapple: you ride a ramp's face by strafing into it. Its map is **Cascade**, four stages of clean 52 and 60 degree ridges down a frozen gorge: gaps to fly across, a turn, zig-zags and two kickers that throw you up onto a ledge if you're fast enough. A dash into any slope now keeps its whole speed and turns along it. In surf you only hold the knife and nobody can hurt anybody (warmup too). Ramps are half as slippery: on a ramp's face you fall (and slide down and off it) at half speed. Strafing into a ramp you touch grips 2x harder (the air speed cap is unchanged).
- **New materials**: ice (slippery underfoot), rubber (you bounce off it, floors, walls and ceilings, and it grips: faster to speed up and to stop), and sand, wood, plastic, grass, steel and diamond plate (they play like base, each with its own look). Metal now looks like brushed metal with a sheen, and water, lava and acid have moving noise across them, so lava reads as lava. Every look is made by the shader: no texture files, no extra memory.
- Rockets and stickies bounce off rubber; stickies and a dropped bomb slide on ice, and a dropped bomb bounces on rubber. The grapple holds on ice as before.
- Map editor: a new box is one grid cell (grid 0.25 m: a 0.25 m cube).
- **More materials**: brick (classic red), glass, asphalt, mud, cobblestone, concrete, tile, marble, corrugated metal, hazard stripes, snow, stone blocks, crystal (glowing edges), circuit (pulsing traces), rust and terracotta. Grass is now green on top and down the top 1/4 m of its sides wherever nothing sits on it. Asphalt is fast and grippy, mud is sticky (slower, stops you hard, weaker jumps), and a dash started on ice is only 25% as strong.
- **Glass breaks**: it's see-through and solid (you can stand on it and stickies sit on it) until it breaks. A shot or the knife leaves a crater and breaks the pane into 10-20 big shards, the ones near the crater flying on through it; a bolt or a blast (not the bubble) shatters the whole pane into 12-40 big pieces flying away from the blast. Dashing through shatters it along the dash, and grappling to it shatters it as you arrive; both cost you a flat 10 and its shards spare you. Grapple a pane while standing or moving away and the shards come back at you. Every flying shard or piece does 10 damage. Blast shards fly 3x as far, dash shards 2x, and the rest 1.5x; once they land they're harmless, and they vanish once they're out of view. Your own breaks show at once.
- **Blow holes in walls**: brick or cobblestone no thicker than 1/8 m blows open from a rocket or sticky, a 2.5 m hole of broken bricks from a direct hit, smaller the further away the blast was (1 m away: 1.5 m). Nothing else breaks it.
- Broken glass and holes come back every round, at a new match and on a new map (FFA keeps them until then).
- Lava, water, acid and the patterned surfaces no longer shimmer seen from a low angle or far off: detail finer than about 2 pixels fades into its average.
- **Your hits count as you saw them**: hitscan shots, rockets and crossbow bolts now do damage where they hit on your screen. Before, the server checked them again, so at high ping a rocket or bolt that clearly hit could do nothing. The only exception is when you die on the server before your hit reaches it. Your own rocket on yourself (rocket jumps) and the shotgun's ricochet are still the server's, and so are stickies, bubbles, bots and the grapple.
- **Tetra: a new map piece**: a three-sided pyramid, with three base corners and its peak over one of them (a wedge corner cut in half along its diagonal). It walks, collides, lights and renders like the other slopes, and bots walk on gentle ones. In the map editor, N cycles through it after the corners, and rotate / mirror turn it any way.
- **Cut box: a new map piece**: a box cut by up to six flat planes at any angle. Wedges, corners and tetras are simple cases of it. A round dome is now exact: each face is one cut box, with no gaps and nothing sticking out. It walks, collides, lights and renders like the other slopes, and the map editor's turn / mirror keys turn it too.
- **Coat in the map editor**: select a box, look at one of its sides and press K to lay a 1/8 m layer of the current material on it (with several boxes selected, on that side of each). Good for a thin brick or glass skin on a wall.
- Fixed: your rocket bouncing off rubber went invisible on your screen until the server's next update; it's now drawn along its bounce. Shooting a pane now brings the whole pane down in visible pieces (only the shards blown out of the bullet hole hurt), instead of a few tiny shards flying away from you.
- **Walls come down**: once 80% of a thin brick or cobblestone wall has been blown away, the rest of it falls in a burst of chunks. It's back next round, like the holes.
- **Brighter glow**: glow trim, crystal edges and circuit traces are brighter.
