CREATE TABLE IF NOT EXISTS servers (addr TEXT PRIMARY KEY, version TEXT NOT NULL, seen INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS servers_seen ON servers (seen);
-- ranked (0.24.0+)
CREATE TABLE IF NOT EXISTS players (id TEXT PRIMARY KEY, mu REAL NOT NULL, sigma REAL NOT NULL, games INTEGER NOT NULL, updated INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS matches (id TEXT PRIMARY KEY, result TEXT NOT NULL, players INTEGER NOT NULL, status TEXT NOT NULL, created INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS reports (match TEXT NOT NULL, reporter TEXT NOT NULL, PRIMARY KEY (match, reporter));
CREATE INDEX IF NOT EXISTS matches_created ON matches (created);
-- leaderboard names (0.28.0+): the latest name each player reported with
CREATE TABLE IF NOT EXISTS names (id TEXT PRIMARY KEY, name TEXT NOT NULL, updated INTEGER NOT NULL);
-- race time trials (1.1.0+): each player's best run per map, with its checkpoint log (ms at each checkpoint)
CREATE TABLE IF NOT EXISTS race_times (map TEXT NOT NULL, id TEXT NOT NULL, ms INTEGER NOT NULL, splits TEXT NOT NULL, version TEXT NOT NULL, created INTEGER NOT NULL, PRIMARY KEY (map, id));
CREATE INDEX IF NOT EXISTS race_times_ms ON race_times (map, ms);
-- Discord link, queues (1.12.0+): the Discord name a player linked in game; queues shared by the game and the bot
CREATE TABLE IF NOT EXISTS links (id TEXT PRIMARY KEY, discord TEXT NOT NULL, updated INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS queue (mode TEXT NOT NULL, who TEXT NOT NULL, name TEXT NOT NULL, discord TEXT NOT NULL, joined INTEGER NOT NULL, PRIMARY KEY (mode, who));
CREATE TABLE IF NOT EXISTS filled (id INTEGER PRIMARY KEY AUTOINCREMENT, mode TEXT NOT NULL, members TEXT NOT NULL, created INTEGER NOT NULL);
-- lifetime stats (1.15.0+): every kill and death a player has had, across all sessions, versions and modes
CREATE TABLE IF NOT EXISTS lifetime (id TEXT PRIMARY KEY, kills INTEGER NOT NULL, deaths INTEGER NOT NULL, updated INTEGER NOT NULL);
