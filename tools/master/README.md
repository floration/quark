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
