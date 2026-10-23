-- Baseline (docs/OPERATIONS.md "Cloudflare"). The schema up to 1.17.0 was applied by hand in the D1 console from
-- schema.sql and the per-version SQL in README.md, so this first migration changes nothing: applying it only records
-- the starting point in D1's migration table.
-- From 1.17.1 on, every schema change is a new numbered file here (0002_<what>.sql, ...), additive where possible
-- (new tables / columns / indexes), and schema.sql is updated to match. tools/master/deploy.sh applies the files not yet
-- applied, before it deploys the worker.
SELECT 1;
