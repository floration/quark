# Next (testing builds since 1.19.5)

What's on `testing` since production 1.19.5. The release PR renames this file to `changelogs/<VERSION>.md`.

**The network protocol is now 68 (1.19.5 used 67) and maps are format 14 (the ball wall)**: update games and servers together. Older maps convert by themselves.

## Maps
- **New map, Stadium** (built for ball): a football pitch under floodlights, a net goal at each end, stands round it. Kickoff: both teams line up facing each other and nobody fires for the first 3 s.
- **Ball** is now on Express (a goal in each canyon gap) and Railyard (the front of each team's building), as well as Bridge, QK8 and Stadium.
- **Bomb** is on Museum: site A in Mammals, site B in Minerals; attackers from the entrance hall, defenders from the service hall.
- **Neutral payload** is on Suspension Bridge: the cart starts mid-span and weaves through the deck's traffic to each end. The median has three new gaps where the track crosses it.
- **Black Hole**: four slim pillars on the floor (landmarks to read your height by while flying, too thin to hide behind), and more out in space: two jets from the hole's poles, a broken debris ring round the accretion disc, and stray rocks drifting toward the hole.

## Map editor
- **Ball wall** (MATERIAL: BALL WALL, with the feature blocks): an invisible block only the ball bounces off. Players, shots and grapples go through it. Build it as a wall round a pitch (it has no lid). The editor shows it see-through like clip.

## Bots
- Team bots no longer have an edge as team A: in power, team A bots won about 60% of matches on every map; now it's even.

## Modes
- WINNER STAYS is no longer listed on any map's modes (1v1 is winner stays now).
