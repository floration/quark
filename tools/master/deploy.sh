#!/bin/sh
# Deploys the ranking server (docs/OPERATIONS.md "Cloudflare"): applies new D1 migrations (migrations/), then deploys
# worker.js with its D1 binding and cron. Run by CI after each production release (release.yml, job deploy-worker,
# when the repository variable DEPLOY_WORKER is true) or by hand from this folder.
# Needs (environment): CLOUDFLARE_API_TOKEN (secret: Workers Scripts edit + D1 edit, this account only),
#   CLOUDFLARE_ACCOUNT_ID, QUARK_D1_ID (the D1 database id), QUARK_WORKER_COMPAT_DATE (the worker's current
#   compatibility date: dashboard -> the worker -> Settings -> Runtime; keep it, a new date can change behaviour).
# Secrets the worker reads (ADMIN_KEY, BOT_KEY) are NOT here: they stay set on the worker and a deploy keeps them.
set -eu
cd "$(dirname "$0")"
: "${CLOUDFLARE_API_TOKEN:?set CLOUDFLARE_API_TOKEN}" "${CLOUDFLARE_ACCOUNT_ID:?set CLOUDFLARE_ACCOUNT_ID}"
: "${QUARK_D1_ID:?set QUARK_D1_ID}" "${QUARK_WORKER_COMPAT_DATE:?set QUARK_WORKER_COMPAT_DATE}"
NAME=${QUARK_WORKER_NAME:-quark-master}
DB=${QUARK_D1_NAME:-quark}
WRANGLER="npx --yes wrangler@${WRANGLER_VERSION:-4.20.0}"   # (pinned; bump it on purpose)
# wrangler.toml is written here, not committed (it names this account's database); it holds no secrets
cat > wrangler.toml <<TOML
name = "$NAME"
main = "worker.js"
compatibility_date = "$QUARK_WORKER_COMPAT_DATE"
workers_dev = true          # the game's pinned address is the workers.dev one (kPinnedMasterUrl)
keep_vars = true            # keep anything set in the dashboard
[triggers]
crons = ["*/15 * * * *"]
[[d1_databases]]
binding = "DB"
database_name = "$DB"
database_id = "$QUARK_D1_ID"
migrations_dir = "migrations"
TOML
echo "== D1 migrations not yet applied to $DB"
$WRANGLER d1 migrations apply "$DB" --remote
echo "== deploy $NAME"
$WRANGLER deploy
echo "== check"
curl -fsS "https://$NAME.$(echo "${QUARK_WORKERS_SUBDOMAIN:-jamesrandallmcamis}").workers.dev/list?v=0" >/dev/null && echo "worker answers"
