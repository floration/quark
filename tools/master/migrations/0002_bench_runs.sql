-- 1.20: FPS benchmark runs (the game's BENCHMARK, POST /bench): one row a finished run, from the player's own game.
-- Additive: a new table and its index.
CREATE TABLE IF NOT EXISTS bench_runs (run INTEGER PRIMARY KEY AUTOINCREMENT, player TEXT NOT NULL, created INTEGER NOT NULL, version TEXT NOT NULL, channel TEXT NOT NULL, build TEXT NOT NULL, cpu TEXT NOT NULL, gpu TEXT NOT NULL, driver TEXT NOT NULL, avg_fps REAL NOT NULL, low1_fps REAL NOT NULL, low01_fps REAL NOT NULL, min_fps REAL NOT NULL, stdev_ms REAL NOT NULL, frames INTEGER NOT NULL, seconds REAL NOT NULL, res TEXT NOT NULL, scale TEXT NOT NULL, aa TEXT NOT NULL, vsync TEXT NOT NULL, fpscap TEXT NOT NULL, demo TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS bench_runs_player ON bench_runs (player, created);
CREATE INDEX IF NOT EXISTS bench_runs_created ON bench_runs (created);
