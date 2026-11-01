-- 1.20: player numbers (POST /number): every player gets the next number the first time the Worker sees them, and a
-- new player's game takes "player<N>" as its default name. Additive: a new table, then the players already known
-- (the names table, oldest row first) numbered 1, 2, 3... in that order, so the next new player continues after them.
CREATE TABLE IF NOT EXISTS player_numbers (n INTEGER PRIMARY KEY AUTOINCREMENT, id TEXT NOT NULL UNIQUE, created INTEGER NOT NULL);
INSERT OR IGNORE INTO player_numbers (id, created) SELECT id, updated FROM names
  WHERE id NOT IN (SELECT id FROM player_numbers) ORDER BY rowid;
