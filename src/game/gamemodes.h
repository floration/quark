// Game mode rules: VS, BOMB, RACE, KOTH, PAYLOAD, FFA.
#pragma once
#include <string>
class Server;

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
