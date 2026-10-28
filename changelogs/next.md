# Next (testing builds since 1.19.1)

What's on `testing` since production 1.19.1. The release PR renames this file to `changelogs/<VERSION>.md`.

## Performance
- **New Settings > VIDEO > PRIORITIZE GAME (Windows, on by default)**: while the game has focus, Windows runs its GPU work before other programs' (a YouTube tab, Discord or other hardware-accelerated apps) and gives it a higher CPU priority. Before, a video playing in the background could take a big share of every frame. Alt-tab and the other programs get their normal share back.

## Maps
- **Wedges and corners**: two new map pieces besides boxes. A wedge is a ramp (one top edge kept), a corner a pyramid with one top corner kept; the slope is whatever the box gives, so any angle. They can stand on the floor, hang upside down from a ceiling or lie on their side (an angled wall). Slopes up to 45 degrees are walkable (no hopping), steeper ones are walls you slide off or along; a dash into one rides up it (or down it). Shots, the grapple and light all follow the slope, and bots walk up gentle ones. In the map editor, N cycles a box through them and rotate / mirror turn them any way; Quarkology's map editor has Wedge and Corner buttons. Maps with them are format 5 (older maps load unchanged); older game versions can't join servers running this one (protocol 48).
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
