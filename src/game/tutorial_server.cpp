// TUTORIAL (1.19.9): the server's side of the movement tutorial (ServerConfig::tutorial; tutorial.h). The host's game
// sends "tutorial <lesson> <epoch>": you go to that lesson's start and the GHOST, a bot whose input is the lesson's
// script (tut::ghostCmd), plays it in a loop from the epoch (server ms) in the lane beside yours, put back at its start
// each loop. The game shows the keys the script is pressing at the same server time, so they match what's drawn.
// Nobody touches anybody (gamemodes.cpp nativeCanDamage, damagePlayer): only your own blasts throw you, and in the
// grapple lessons with the PARTNER (a second scripted bot, LessonDef::partner) the one grapple the lesson is about.
#include "server.h"
#include "tutorial.h"
#include <algorithm>
#include <cmath>
#include <istream>

void Server::tutorialStart() {
    cfg_.autoStart = false;   // (warmup for good: no match, no clock, no scores)
    cfg_.balanceBots = false;
    auto named = [&](int id, const char* name) {
        ServerPlayer& p = players_[id];
        p.name = name;
        p.model = 0;
        GameEvent e; e.type = EV_PLAYERINFO; e.a = (uint8_t)id; e.b = (uint8_t)(p.model | (p.soundPack << 4)); e.c = (int16_t)p.nameColor; e.text = p.name;
        broadcastEvent(e);
    };
    tutGhost_ = addBot(TEAM_A, BOT_BEGINNER);
    if (tutGhost_ < 0) return;
    named(tutGhost_, "GHOST");
    bots_[tutGhost_].script = [](Server& s, int id) { return s.tutorialGhostCmd(id); };
    // the PARTNER: watching till a lesson has it in (tutorialPartnerReset)
    tutPartner_ = addBot(TEAM_SPECTATOR, BOT_BEGINNER);
    if (tutPartner_ < 0) return;
    named(tutPartner_, "PARTNER");
    bots_[tutPartner_].script = [](Server& s, int id) { return s.tutorialPartnerCmd(id); };
}

bool Server::tutorialSpawnAt(int id, Vec3& pos, float& yaw) const {
    const int l = std::max(tutLesson_, 0);
    yaw = tut::kStartYaw;
    if (id == tutGhost_) pos = tut::ghostStart(l);
    else if (id == tutPartner_) { pos = tut::partnerStart(l); yaw = tut::kPartnerYaw; }
    else if (tutLesson_ >= 0) pos = tut::playerStart(l);
    else return false;   // (before the game has picked a lesson: a spawn of the map's)
    return true;
}

// The grapple between two players: only in the PARTNER's lessons, only the pair the lesson is about (grapple a player:
// anyone's hook on the partner; dash out of a grapple: the partner's hook on anyone). Nothing else ever hurts or
// touches anyone here (damagePlayer), so this is the grapple's rule alone.
bool Server::tutorialHookAllowed(int attacker, int victim) const {
    if (tutPartner_ < 0 || attacker < 0 || attacker == victim) return false;
    if (tutLesson_ == tut::L_HOOKPLAYER) return victim == tutPartner_;
    if (tutLesson_ == tut::L_BREAKFREE) return attacker == tutPartner_;
    return false;
}

void Server::tutorialGhostReset(int id) {
    ServerPlayer& p = players_[id];
    if (!p.alive) { spawnPlayer(id); return; }   // (tutorialSpawnAt puts it at its start)
    for (auto& pr : projectiles_)
        if (pr.active && pr.owner == id) pr.active = false;   // (its last loop's stickies and rockets)
    endLatch(id);                                  // (its grapple on the partner, the partner's on it)
    if (p.hookedBy >= 0) endLatch(p.hookedBy);
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

// The partner each loop: in (alive at its start) for a lesson that has one, watching for the others
void Server::tutorialPartnerReset() {
    if (tutPartner_ < 0 || !players_[tutPartner_].connected) return;
    ServerPlayer& p = players_[tutPartner_];
    endLatch(tutPartner_);
    if (p.hookedBy >= 0) endLatch(p.hookedBy);
    if (tutLesson_ < 0 || !tut::lesson(tutLesson_).partner) {
        if (p.team != TEAM_SPECTATOR || p.alive) { p.team = TEAM_SPECTATOR; p.alive = false; p.respawnAt = -1; }
        return;
    }
    if (p.team == TEAM_SPECTATOR || !p.alive) { p.team = TEAM_A; spawnPlayer(tutPartner_); return; }
    p.move = MoveState();
    p.move.pos = tut::partnerStart(tutLesson_);
    p.move.flags = MF_ONGROUND;
    p.yaw = tut::kPartnerYaw;
    p.pitch = 0;
    p.health = lifeMaxHealth();
}

void Server::tutorialLoop() {
    if (tutLesson_ < 0) return;
    const int64_t loop = std::max(1, tut::loopMs(tutLesson_));
    const int64_t t = (int64_t)timeMs_ - (int64_t)tutEpoch_;
    const int64_t n = t < 0 ? -1 : t / loop;
    if (n == tutLoop_ && t >= 0) return;
    tutLoop_ = n;   // (a new loop: both back to the start)
    if (tutGhost_ >= 0 && players_[tutGhost_].connected) tutorialGhostReset(tutGhost_);
    tutorialPartnerReset();
}

namespace {
// a scripted command aimed (Seg pitch tut::kAimAt) from the bot's eyes at the other's body (where it is this tick: a
// bot's view time is now)
void aimCmd(const Server& s, int id, int at, UserCmd& c) {
    if (at < 0 || at >= kMaxPlayers || !s.players_[at].connected || !s.players_[at].alive) return;
    tut::aimAt(s.players_[id].move.pos + Vec3(0, kEyeHeight, 0), s.players_[at].move.pos + Vec3(0, 1.0f, 0), c.yaw, c.pitch);
}
}  // namespace

UserCmd Server::tutorialGhostCmd(int id) {
    if (tutLesson_ < 0) { UserCmd c; c.yaw = tut::kStartYaw; return c; }
    tutorialLoop();
    const int64_t t = (int64_t)timeMs_ - (int64_t)tutEpoch_;
    const int tl = t < 0 ? -1 : (int)(t % std::max(1, tut::loopMs(tutLesson_)));
    UserCmd c = tut::ghostCmd(tutLesson_, tl);
    if (tut::ghostAimsAt(tutLesson_, tl)) {
        if (tut::lesson(tutLesson_).partner) aimCmd(*this, id, tutPartner_, c);
        else tut::aimAt(players_[id].move.pos + Vec3(0, kEyeHeight, 0), tut::aimPoint(tutLesson_), c.yaw, c.pitch);   // (the swing's pillar)
    }
    return c;
}

UserCmd Server::tutorialPartnerCmd(int id) {
    UserCmd c; c.yaw = tut::kPartnerYaw;
    if (tutLesson_ < 0 || !tut::lesson(tutLesson_).partner) return c;
    tutorialLoop();
    const int64_t t = (int64_t)timeMs_ - (int64_t)tutEpoch_;
    const int tl = t < 0 ? -1 : (int)(t % std::max(1, tut::loopMs(tutLesson_)));
    c = tut::partnerCmd(tutLesson_, tl);
    if (tut::partnerAimsAt(tutLesson_, tl)) aimCmd(*this, id, tutTry_ ? tutPlayer_ : tutGhost_, c);   // (the demo: the ghost; your turn: you)
    return c;
}

// "tutorial <lesson> [epoch]": to that lesson (an id or a number), the ghost's loop from the epoch (server ms; the game
// sends its view time plus a moment); "tutorial reset": back to your lesson's start; "tutorial try": your turn (the
// PARTNER hooks you, not the ghost)
std::string Server::tutorialCommand(int from, const std::string& arg, std::istream& rest) {
    if (!cfg_.tutorial) return "tutorial: only in the tutorial's own game";
    if (from < 0 || from >= kMaxPlayers || !players_[from].connected || players_[from].isBot) return "tutorial: players only";
    ServerPlayer& p = players_[from];
    tutPlayer_ = from;
    if (arg == "try") { tutTry_ = true; return std::string(); }   // (the game's TRY: the partner's hook is for you now)
    if (arg != "reset") {
        const int l = tut::lessonFromName(arg.c_str());
        if (l < 0) return "tutorial <lesson 0-" + std::to_string(tut::L_COUNT - 1) + " or its name> [epoch ms] | reset";
        long long epoch = -1;
        rest >> epoch;
        tutLesson_ = l;
        tutTry_ = false;   // (the demo first: the partner's hook is for the ghost)
        // (from a moment ago to a few seconds on: a bad number can't freeze the ghost or start it mid loop)
        tutEpoch_ = epoch < 0 ? timeMs_ : (uint64_t)std::clamp<long long>(epoch, (long long)timeMs_ - 1000, (long long)timeMs_ + 3000);
        tutLoop_ = -2;
        if (tut::lesson(l).weapon < WPN_COUNT) p.selectedWeapon = tut::lesson(l).weapon;
    }
    if (p.team == TEAM_SPECTATOR) return "tutorial: spectating";
    spawnPlayer(from);   // (at the lesson's start, a fresh state; EV_SPAWN starts the game's prediction there)
    return std::string();
}
