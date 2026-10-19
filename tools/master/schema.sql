CREATE TABLE IF NOT EXISTS servers (addr TEXT PRIMARY KEY, version TEXT NOT NULL, seen INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS servers_seen ON servers (seen);
-- ranked (0.24.0+)
CREATE TABLE IF NOT EXISTS players (id TEXT PRIMARY KEY, mu REAL NOT NULL, sigma REAL NOT NULL, games INTEGER NOT NULL, updated INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS matches (id TEXT PRIMARY KEY, result TEXT NOT NULL, players INTEGER NOT NULL, status TEXT NOT NULL, created INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS reports (match TEXT NOT NULL, reporter TEXT NOT NULL, PRIMARY KEY (match, reporter));
CREATE INDEX IF NOT EXISTS matches_created ON matches (created);
-- leaderboard names (0.28.0+): the latest name each player reported with
CREATE TABLE IF NOT EXISTS names (id TEXT PRIMARY KEY, name TEXT NOT NULL, updated INTEGER NOT NULL);
