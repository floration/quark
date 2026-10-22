# Owner answers to docs/OWNER_DECISIONS.md (2026-10-22)

The owner's replies, verbatim; numbers are the sections of OWNER_DECISIONS.md.

```
owner decisions
1.1 - release these fixes as update; and all future fixes/updates should go to a testing branch, (users should be able to play testing branch via the launcher, needs to have a button added on it somewhere; have the game update the launcher for people not on the latest launcher, add a button that will toggle between testing branch and production branch where you can pick versions, should change the list of selectable updates)
1.2 - this has already been fixed, it only pings people with the update ping role
1.3 - allow the venv
1.4 - schedule map12 with batch 5
1.5 - yes performance comes first
2.1 - yes do (a) signed updates, including on testing branch, also test branch should have its own discord channel with its own special testing updates role
2.2 - a, protected main, should only be pushed from an existing testing branch, and requires a production/release/whateverthefuck tag 
2.3 - okay sure
2.4 - yes; also the only reason 1.14.1 got stalled was because github didnt have any macos builders at the time, but release version should always force all of the os options, if no worker available, then i just wont be able to update the release version, but testing shouldnt have this requirement
2.5 - move them to per user config, and this should not be deleted via the uninstall button on launcher, probably put it in registry; so it can be removed by cleaning programs, but not by just the launcher
2.6 - yes full launcher upgrade, but the game update should update the launcher, upgrade from all previous launcher iterations to the new one, and this launcher version should be able to update itself; therefore nobody has to download a new launcher to update it or the game
2.7 - yes, do the recommendation
2.8 - recommended actions, no license yet
3.1 - a, yeah it should use a match ticket i guess, also ensure that only the first leaver gets punished with a loss, the one who stayed, who then has to leave should also not be punished
3.2 - okay do them
3.3 - yes i do own and control that cloudflare account; sure do the other thing too
3.4 - when a 1v1 match has been started votes, gamemode changes, map changes, disabling ranked, etc should be disallowed until the match is over; this does not apply to non ranked matches, no rating decay, dnot need banded queues, anybody can play anybody
4.1 - i think b alone is fine
5.1 - overtime should happen whenever the attacker is on the point at all when timer hits 0, it should also apply when someone had touched it within the last 5 seconds, overtime timer is 5 seconds; this applies for all gamemodes with a site or cart
5.2 - syringe should only exist in respawn based gamemodes, unless otherwise limited
5.3 - knoickback should be unlimited its funny, shared cooldown probably yeah, this also applies for other certain alt fire modes, like syringe; where cooldown reduction timers should also work for these, which it previously didnt, scale the hull
5.4 - auto bhop intentional, unbounded strafing intentional, dash limit intentional, grapple should NOT brake on catch, other than how it tries to bring you towards the connection point, also yeah fix map metrics
5.5 - bubble is insanely hard to do damage with so its fine, shoulders should not count as headshots, sniper should do a flat 50 damage
5.6 - yes thats the idea, fix scoring on hide and seek, seeding should be based on average of the 3 ranked modes (200 if not ranked), headshot damage should be red instead of white, bots need to be redone, all games should have a 10 second recoonect grace, ranked and tourneyus
6 - yes the maps are shit i am aware, generator output becomes a build artifact
7 - limit discord listed servers to 5, install secrets for the discord bot yeah, actually make the discord bot its own secret branch or whatever so i am the only one who can access its code and run it; bot changes peoples roles```
