// Game mode rules: VS, BOMB, RACE, KOTH, PAYLOAD, FFA.
#pragma once
#include <string>
#include <vector>
class Server;
struct Map;
struct SpawnPoint;

bool modeIsTeamBased(int mode);
void rulesReset(Server& s);                       // back to warmup
void rulesTick(Server& s);                        // once per server tick
void rulesForceStart(Server& s);                  // start the match now, even short-handed
void rulesOnDeath(Server& s, int victim, int killer);
void rulesOnLeave(Server& s, int id);
void rulesEndMatch(Server& s, const std::string& text, int winner); // end the match now (a ranked leaver decided it)
void rulesOnSpectate(Server& s, int id);          // a player is about to switch to spectator
bool rulesTeamLocked(const Server& s);            // no joining a side now (a tag match is being played)
float rulesRespawnDelay(const Server& s);         // < 0 means no respawn
bool rulesCanDamage(const Server& s, int attacker, int victim);
bool rulesFrozen(const Server& s, int id);         // movement locked (freeze time / planting)
int rulesSpawnType(const Server& s, int id);       // SPAWN_*
// The spawn type (SPAWN_*) a player of `team` (TEAM_*) draws from in `mode` on map m; attackers: the attacking team
// (payload, multi point); tourneyMatch: a tourney match is being played. (rulesSpawnType for a server's player.)
int modeSpawnType(const Map& m, int mode, int team, int attackers, bool tourneyMatch);
// The spawns Server::spawnPlayer picks from for spawn type `want`: a staged type uses the latest stage reached
// (pointsTaken: points / cart checkpoints taken) that has spawns for that side; an empty pool falls back to the
// `any` spawns, then to every spawn. The map tests check every offered mode's pools with this (REVIEW MAP-2).
std::vector<const SpawnPoint*> spawnPool(const Map& m, int want, int pointsTaken);
