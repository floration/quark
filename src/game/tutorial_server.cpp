// TUTORIAL (1.19.9): the server's side of the movement tutorial (ServerConfig::tutorial; tutorial.h). The host's game
// sends "tutorial <lesson> <epoch>": you go to that lesson's start and the GHOST, a bot whose input is the lesson's
// script (tut::ghostCmd), plays it in a loop from the epoch (server ms) in the lane beside yours, put back at its start
// each loop. The game shows the keys the script is pressing at the same server time, so they match what's drawn.
// Nobody touches anybody (gamemodes.cpp nativeCanDamage, damagePlayer): only your own blasts throw you.
#include "server.h"
#include "tutorial.h"
#include <algorithm>
#include <cmath>
#include <istream>

void Server::tutorialStart() {
    cfg_.autoStart = false;   // (warmup for good: no match, no clock, no scores)
    cfg_.balanceBots = false;
    tutGhost_ = addBot(TEAM_A, BOT_BEGINNER);
    if (tutGhost_ < 0) return;
    ServerPlayer& p = players_[tutGhost_];
    p.name = "GHOST";
    p.model = 0;
    GameEvent e; e.type = EV_PLAYERINFO; e.a = (uint8_t)tutGhost_; e.b = (uint8_t)(p.model | (p.soundPack << 4)); e.c = (int16_t)p.nameColor; e.text = p.name;
    broadcastEvent(e);
    bots_[tutGhost_].script = [](Server& s, int id) { return s.tutorialGhostCmd(id); };
}

bool Server::tutorialSpawnAt(int id, Vec3& pos, float& yaw) const {
    const int l = std::max(tutLesson_, 0);
    if (id == tutGhost_) pos = tut::ghostStart(l);
    else if (tutLesson_ >= 0) pos = tut::playerStart(l);
    else return false;   // (before the game has picked a lesson: a spawn of the map's)
    yaw = tut::kStartYaw;
    return true;
}

void Server::tutorialGhostReset(int id) {
    ServerPlayer& p = players_[id];
    if (!p.alive) { spawnPlayer(id); return; }   // (tutorialSpawnAt puts it at its start)
    for (auto& pr : projectiles_)
        if (pr.active && pr.owner == id) pr.active = false;   // (its last loop's stickies and rockets)
    p.move = MoveState();
    p.move.pos = tut::ghostStart(std::max(tutLesson_, 0));
    p.move.flags = MF_ONGROUND;
    p.yaw = tut::kStartYaw;
    p.pitch = 0;
    p.health = lifeMaxHealth();
    p.nextFire = time();   // (it fires on its script's tick)
    p.fireLocked = false;
    p.fireQueuedUntil = -1;
    if (tut::lesson(std::max(tutLesson_, 0)).weapon < WPN_COUNT) p.weapon = p.selectedWeapon = tut::lesson(std::max(tutLesson_, 0)).weapon;
}

UserCmd Server::tutorialGhostCmd(int id) {
    if (tutLesson_ < 0) { UserCmd c; c.yaw = tut::kStartYaw; return c; }
    const int64_t loop = std::max(1, tut::loopMs(tutLesson_));
    const int64_t t = (int64_t)timeMs_ - (int64_t)tutEpoch_;
    const int64_t n = t < 0 ? -1 : t / loop;
    if (n != tutLoop_ || t < 0) { tutLoop_ = n; tutorialGhostReset(id); }   // (a new loop: back to the start)
    return tut::ghostCmd(tutLesson_, t < 0 ? -1 : (int)(t % loop));
}

// "tutorial <lesson> [epoch]": to that lesson (an id or a number), the ghost's loop from the epoch (server ms; the game
// sends its view time plus a moment); "tutorial reset": back to your lesson's start
std::string Server::tutorialCommand(int from, const std::string& arg, std::istream& rest) {
    if (!cfg_.tutorial) return "tutorial: only in the tutorial's own game";
    if (from < 0 || from >= kMaxPlayers || !players_[from].connected || players_[from].isBot) return "tutorial: players only";
    ServerPlayer& p = players_[from];
    if (arg != "reset") {
        const int l = tut::lessonFromName(arg.c_str());
        if (l < 0) return "tutorial <lesson 0-" + std::to_string(tut::L_COUNT - 1) + " or its name> [epoch ms] | reset";
        long long epoch = -1;
        rest >> epoch;
        tutLesson_ = l;
        // (from a moment ago to a few seconds on: a bad number can't freeze the ghost or start it mid loop)
        tutEpoch_ = epoch < 0 ? timeMs_ : (uint64_t)std::clamp<long long>(epoch, (long long)timeMs_ - 1000, (long long)timeMs_ + 3000);
        tutLoop_ = -2;
        if (tut::lesson(l).weapon < WPN_COUNT) p.selectedWeapon = tut::lesson(l).weapon;
    }
    if (p.team == TEAM_SPECTATOR) return "tutorial: spectating";
    spawnPlayer(from);   // (at the lesson's start, a fresh state; EV_SPAWN starts the game's prediction there)
    return std::string();
}
