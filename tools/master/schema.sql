CREATE TABLE IF NOT EXISTS servers (addr TEXT PRIMARY KEY, version TEXT NOT NULL, seen INTEGER NOT NULL, token TEXT);
CREATE INDEX IF NOT EXISTS servers_seen ON servers (seen);
-- ranked (0.24.0+)
CREATE TABLE IF NOT EXISTS players (id TEXT PRIMARY KEY, mu REAL NOT NULL, sigma REAL NOT NULL, games INTEGER NOT NULL, updated INTEGER NOT NULL, ver INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS matches (id TEXT PRIMARY KEY, result TEXT NOT NULL, players INTEGER NOT NULL, status TEXT NOT NULL, created INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS reports (match TEXT NOT NULL, reporter TEXT NOT NULL, result TEXT, dispute INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (match, reporter));
CREATE INDEX IF NOT EXISTS matches_created ON matches (created);
-- leaderboard names (0.28.0+): the latest name each player reported with
CREATE TABLE IF NOT EXISTS names (id TEXT PRIMARY KEY, name TEXT NOT NULL, updated INTEGER NOT NULL);
-- race time trials (1.1.0+): each player's best run per map, with its checkpoint log (ms at each checkpoint)
CREATE TABLE IF NOT EXISTS race_times (map TEXT NOT NULL, id TEXT NOT NULL, ms INTEGER NOT NULL, splits TEXT NOT NULL, version TEXT NOT NULL, created INTEGER NOT NULL, PRIMARY KEY (map, id));
CREATE INDEX IF NOT EXISTS race_times_ms ON race_times (map, ms);
-- Discord link, queues (1.12.0+): the Discord name a player linked in game; queues shared by the game and the bot
CREATE TABLE IF NOT EXISTS links (id TEXT PRIMARY KEY, discord TEXT NOT NULL, updated INTEGER NOT NULL, verified INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS queue (mode TEXT NOT NULL, who TEXT NOT NULL, name TEXT NOT NULL, discord TEXT NOT NULL, joined INTEGER NOT NULL, PRIMARY KEY (mode, who));
CREATE TABLE IF NOT EXISTS filled (id INTEGER PRIMARY KEY AUTOINCREMENT, mode TEXT NOT NULL, members TEXT NOT NULL, created INTEGER NOT NULL);
-- lifetime stats (1.15.0+): every kill and death a player has had, across all sessions, versions and modes
CREATE TABLE IF NOT EXISTS lifetime (id TEXT PRIMARY KEY, kills INTEGER NOT NULL, deaths INTEGER NOT NULL, updated INTEGER NOT NULL, hour_start INTEGER NOT NULL DEFAULT 0, hour_kills INTEGER NOT NULL DEFAULT 0, hour_deaths INTEGER NOT NULL DEFAULT 0);
-- match tickets, provisional accounts, verified Discord links (anti-cheat update; see README "Match tickets")
CREATE TABLE IF NOT EXISTS tickets (id TEXT PRIMARY KEY, kind TEXT NOT NULL, server TEXT NOT NULL, ladder TEXT NOT NULL, map TEXT NOT NULL, players TEXT NOT NULL, created INTEGER NOT NULL, status TEXT NOT NULL, result TEXT, leavers TEXT, resulted INTEGER);
CREATE INDEX IF NOT EXISTS tickets_server ON tickets (server, created);
CREATE TABLE IF NOT EXISTS accounts (id TEXT PRIMARY KEY, tgames INTEGER NOT NULL, created INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS link_codes (code TEXT PRIMARY KEY, discord TEXT NOT NULL, created INTEGER NOT NULL);
-- signed ranked ids, leavers and disputes, atomic writes, pruning (1.17; README "Ranked keys, leavers, hardening")
CREATE TABLE IF NOT EXISTS id_moves (old TEXT PRIMARY KEY, new TEXT NOT NULL, moved INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS rate_guard (ok INTEGER NOT NULL CHECK (ok = 1));
CREATE INDEX IF NOT EXISTS players_skill ON players ((mu - 3 * sigma));
CREATE INDEX IF NOT EXISTS tickets_created ON tickets (created);
CREATE INDEX IF NOT EXISTS matches_status ON matches (status, created);
CREATE INDEX IF NOT EXISTS queue_joined ON queue (joined);
CREATE INDEX IF NOT EXISTS filled_created ON filled (created);
CREATE INDEX IF NOT EXISTS link_codes_created ON link_codes (created);
CREATE INDEX IF NOT EXISTS lifetime_kills ON lifetime (kills);
-- FPS benchmark runs (1.20+, migrations/0002_bench_runs.sql): POST /bench, one row a finished run
CREATE TABLE IF NOT EXISTS bench_runs (run INTEGER PRIMARY KEY AUTOINCREMENT, player TEXT NOT NULL, created INTEGER NOT NULL, version TEXT NOT NULL, channel TEXT NOT NULL, build TEXT NOT NULL, cpu TEXT NOT NULL, gpu TEXT NOT NULL, driver TEXT NOT NULL, avg_fps REAL NOT NULL, low1_fps REAL NOT NULL, low01_fps REAL NOT NULL, min_fps REAL NOT NULL, stdev_ms REAL NOT NULL, frames INTEGER NOT NULL, seconds REAL NOT NULL, res TEXT NOT NULL, scale TEXT NOT NULL, aa TEXT NOT NULL, vsync TEXT NOT NULL, fpscap TEXT NOT NULL, demo TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS bench_runs_player ON bench_runs (player, created);
CREATE INDEX IF NOT EXISTS bench_runs_created ON bench_runs (created);
-- player numbers (1.20+, migrations/0003_player_numbers.sql): POST /number, the default name "player<N>"
CREATE TABLE IF NOT EXISTS player_numbers (n INTEGER PRIMARY KEY AUTOINCREMENT, id TEXT NOT NULL UNIQUE, created INTEGER NOT NULL);
