# Internet server list (Cloudflare Worker + D1, free tier)

Public servers announce themselves here every minute. The Join screen lists them, then checks each one
directly over UDP, so it shows only servers it can reach, with their real ping. Nothing runs on a VPS.

## Setup (dashboard, about 5 minutes)
1. **Create the database.** Cloudflare dashboard → **Storage & Databases → D1 SQL Database → Create**.
   Name it `quark`. Open it → **Console**, paste the contents of `schema.sql`, and run it.
2. **Create the worker.** **Compute (Workers) → Workers & Pages → Create → Create Worker** (or **Start with Hello World**).
   Name it `quark-master` → **Deploy**.
3. **Paste the code.** Open the worker → **Edit code**. Replace everything with `worker.js`, then **Deploy**.
4. **Bind the database.** Worker → **Settings → Bindings → Add → D1 database**.
   Variable name `DB`, database `quark` → **Save/Deploy**.
5. **Check it.** Open `https://quark-master.<your-subdomain>.workers.dev/list?v=0.13.0`.
   An empty page with no error means it works. `…/announce?port=27500&v=test`, then `…/list?v=test`, should show your IP.
6. **Point the game at it.** Put that base URL (`https://quark-master.<your-subdomain>.workers.dev`, no trailing
   slash) as the only line of `site/master.txt` and push. Games read it from the website, so no new build is needed.

The free tier allows 100k requests a day and 100k D1 writes a day. That is about 70 servers announcing around the clock.
For testing, the `QUARK_MASTER` environment variable overrides the URL.

## Ranked (0.24.0+)
The same Worker stores ranked ratings. To turn it on for an existing setup:
1. D1 → `quark` → **Console**: paste the `-- ranked` part of `schema.sql` (the three `CREATE TABLE` lines and the index) and run it.
2. Worker → **Edit code**: replace everything with the new `worker.js` → **Deploy**.
`https://quark-master.<you>.workers.dev/ratings?ids=` should answer with an empty page.

Each player's game makes a random key once (`quark.key` next to `quark.cfg`); their id is a hash of it, and the
key itself only goes to this Worker. After a ranked match every player's game posts its own report, and the
match is rated (OpenSkill, Plackett-Luce) only when all of them agree. Test locally: `node tools/master/test.mjs`.

## Race time trials (1.1.0+)
Solo Viaduct runs go to the same Worker. Each player keeps only their best time, along with the time at every checkpoint.
1. D1 → `quark` → **Console**: paste the `-- race time trials` part of `schema.sql` (the `race_times` table and index) and run it.
2. Worker → **Edit code**: replace everything with the new `worker.js` → **Deploy**.

`…/raceboard?map=viaduct` should answer with an empty page.

**Checking or removing a suspicious time** (D1 console):
```sql
SELECT r.id, n.name, r.ms, r.splits, r.version, datetime(r.created / 1000, 'unixepoch') FROM race_times r
  LEFT JOIN names n ON n.id = r.id WHERE r.map = 'viaduct' ORDER BY r.ms LIMIT 50;
DELETE FROM race_times WHERE map = 'viaduct' AND id = '<id>';
```
`splits` is the run time in ms at each checkpoint, so a gap that's impossible to cover stands out.
