// wpnrange: a weapon fired at still or moving dummy targets on the game's own Server (BotSim: the real simulate(), the
// real fireWeapon / hitscan / projectiles / damage / weapon set logic), on the built-in range or any map. README.md.
//
// How it is wired (no game code changed):
//  * BotSim starts a free for all with the shooter and the targets as bots, then the request's rules go on the server
//    (wpn::applyToServer: cfg_.custom, the server thread's setActiveRules).
//  * The targets keep their bot slots, but each one's input comes from BotBrain::script (the game's scripted-player
//    hook): no AI, it stands (or crouches) facing the shooter, and is put on its path every tick (a moving target
//    slides along x or z: base + amplitude x sin(speed / amplitude x t)). Their respawn is ours: `respawn` s after a
//    death (the server's own is cancelled), back at the target's spot with the target's health.
//  * The shooter's slot is turned into a player (isBot = false) whose commands this file queues one per tick, exactly
//    as a client's arrive (Server::drainCmds -> runPlayerCmd), view time = now (no rewind). Being a player, it gets
//    the game's hit feedback: EV_HIT (attacker, victim, damage, "h" = a headshot) and EV_KILL (its headshot flag),
//    which is where the per shot list comes from. Damage, kills and first kills come from the targets' health.
//  * aim perfect / body: the body's centre; head: the head box's centre; projectiles lead the target along its known
//    path and aim above it for the drop (iterated flight time). aim bot: the game's bot (botThinkDirect) picks the
//    target, aims (its skill's error, reaction, tracking) and decides to fire; its movement keys are dropped.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <random>
#include <thread>
#include "botsim.h"
#include "maptool.h"
#include "wpn.h"
#include "common/net.h"
#include "common/u8file.h"
#include "game/bots.h"
#include "game/gamemodes.h"
#include "game/pmove.h"
#include "game/server.h"

namespace wpn {
namespace {
namespace fs = std::filesystem;

std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
double r3(double x) { return std::round(x * 1000.0) / 1000.0; }
double r2(double x) { return std::round(x * 100.0) / 100.0; }
Json v3(const Vec3& v, double q = 1000.0) { Json a = Json::array(); for (float x : {v.x, v.y, v.z}) a.push(std::round(x * q) / q); return a; }

// ---- the built-in range (the page's mockrange.js builtinRangeMap, in the game's map format) ------------------------
const double kLaneDist[] = {5, 10, 20, 40, 80};
const double kLaneX[] = {-3, 3, -2, 6, 0};   // (staggered: no target on the line from the shooter to another)
std::string rangeMapText() {
    std::string o;
    char b[200];
    o += "# the weapons editor's built-in range (tools/maptool/weapon/wpn_range.cpp): made in code, not a shipped map\n";
    o += "version " + std::to_string(kMapFormatVersion) + "\n";
    o += "title Range\nsetting a firing range\nfor ffa\nsun 0.4 1 0.25\n";
    o += "box -20 -1 -100 20 0 6\naccent -20 0 -100 -19.5 0.6 6\naccent 19.5 0 -100 20 0.6 6\n";
    for (int i = 0; i < 5; i++) {
        snprintf(b, sizeof b, "trim %g 0 %g %g 0.05 %g\n", kLaneX[i] - 1.2, -kLaneDist[i] - 0.05, kLaneX[i] + 1.2, -kLaneDist[i] + 0.05);
        o += b;
    }
    o += "spawn any 0 0 0 0\nspawn any -8 0 3 0\nspawn any 8 0 3 0\nspawn any -16 0 4 0\nspawn any 16 0 4 0\n";
    return o;
}

// "map": absent / "range" = the built-in range; a name or a path; {name, text} (the page's); {text|file|name|map}
bool loadMap(const Json& req, std::string& text, std::string& label, bool& builtin, std::string& err) {
    const Json& mj = req["map"];
    builtin = false;
    if (mj.isNull() || (mj.isStr() && (mj.str().empty() || lower(mj.str()) == "range")) ||
        (mj.isObj() && !mj.has("text") && !mj.has("file") && !mj.has("brushes") && (mj["name"].isNull() || lower(mj["name"].str()) == "range"))) {
        builtin = true; text = rangeMapText(); label = "range";
        return true;
    }
    Json r = Json::object();
    std::error_code ec;
    if (mj.isStr()) { if (fs::is_regular_file(utf8Path(mj.str()), ec)) r.set("file", mj.str()); else r.set("name", mj.str()); }
    else if (mj.isObj() && mj["text"].isStr()) { r.set("text", mj["text"]); if (mj["name"].isStr()) r.set("mapName", mj["name"]); }
    else if (mj.isObj() && mj["file"].isStr()) r.set("file", mj["file"]);
    else if (mj.isObj() && mj["name"].isStr() && !mj.has("brushes")) r.set("name", mj["name"]);
    else r.set("map", mj);
    Map m;
    if (!loadRequestMap(r, m, err)) return false;
    text = m.sourceText();
    label = m.name.empty() ? (mj["name"].isStr() ? mj["name"].str() : std::string("map")) : m.name;
    return true;
}

// ---- the request --------------------------------------------------------------------------------------------------
enum Aim { AIM_BODY, AIM_HEAD, AIM_BOT };
struct Target {
    Vec3 base;
    bool moving = false; int axis = 0; float speed = 5, amp = 3;
    float health = 0;     // 0: the default
    bool crouch = false;
    Vec3 at(double t) const {
        Vec3 p = base;
        if (moving) { float s = amp * (float)std::sin(speed / amp * t); if (axis == 2) p.z += s; else p.x += s; }
        return p;
    }
    Vec3 vel(double t) const {
        Vec3 v;
        if (moving) { float s = speed * (float)std::cos(speed / amp * t); if (axis == 2) v.z = s; else v.x = s; }
        return v;
    }
};
struct Setup {
    std::string mapText, mapLabel;
    bool builtin = true;
    int weapon = WPN_RIFLE;
    bool alt = false;
    std::vector<Target> targets;
    Vec3 shooter;
    float shooterYaw = 0;
    Aim aim = AIM_BODY;
    int skill = BOT_ADVANCED;
    double seconds = 10, respawn = 1.0, frameEvery = 0.02;
    uint32_t seed = 1;
    int tickRate = 1000;
    float health = 0;     // the targets' default (0: the rules' health)
    bool nearest = false; // order: "nearest" (else "lanes": each target in turn, killed once, then the next)
    bool frames = true;
    int maxShots = 20000;
};

int skillOf(const Json& j) {
    if (j.isNum()) { double x = j.num(); return x <= 1.0 ? std::clamp((int)std::lround(x * (BOT_SKILLS - 1)), 0, BOT_SKILLS - 1) : std::clamp((int)x, 0, BOT_SKILLS - 1); }
    if (j.isStr()) for (int k = 0; k < BOT_SKILLS; k++) if (lower(botSkillName(k)) == lower(j.str())) return k;
    return BOT_ADVANCED;
}

bool parseSetup(const Json& req, Setup& S, std::string& err) {
    if (!loadMap(req, S.mapText, S.mapLabel, S.builtin, err)) return false;
    const Json& wj = req["weapon"];
    S.weapon = wj.isNum() ? wj.i() : wj.isNull() ? WPN_RIFLE : weaponFromName(wj.str());
    if (S.weapon < 0 || S.weapon >= WPN_COUNT) { err = "weapon: one of rifle rocket knife sniper laser shotgun sticky crossbow bubble syringe locator"; return false; }
    S.alt = req["alt"].boolean(false);
    S.seconds = std::clamp(req["seconds"].num(10), 0.1, 120.0);
    S.seed = (uint32_t)req["seed"].num(1);
    S.tickRate = req["tickRate"].i(1000);
    if (S.tickRate != 1000 && S.tickRate != 500 && S.tickRate != 250 && S.tickRate != 125) { err = "tickRate is 1000, 500, 250 or 125"; return false; }
    S.respawn = std::clamp(req["respawn"].num(1.0), 0.0, 30.0);
    S.frameEvery = std::clamp(req["frameEvery"].num(0.02), 0.001, 1.0);
    S.frames = req["frames"].boolean(true);
    S.health = (float)std::clamp(req["health"].num(0), 0.0, 100000.0);
    S.nearest = lower(req["order"].str("lanes")) == "nearest";
    S.maxShots = std::clamp(req["maxShots"].i(20000), 0, 1000000);
    const Json& sh = req["shooter"];
    S.shooter = sh["pos"].isArr() ? toVec(sh["pos"]) : Vec3();
    if (S.shooter.y == 0 && !sh["pos"].isArr() && !S.builtin) { err = "shooter: give pos [x, y, z] on a map (the built-in range's is [0, 0, 0])"; return false; }
    S.shooterYaw = sh["yawDeg"].isNum() ? (float)(sh["yawDeg"].num() * PI / 180.0) : sh["yaw"].f(0);
    std::string aim = lower(sh["aim"].str("perfect"));
    if (aim == "perfect" || aim == "body") S.aim = AIM_BODY;
    else if (aim == "head") S.aim = AIM_HEAD;
    else if (aim == "bot") S.aim = AIM_BOT;
    else { err = "shooter.aim: perfect, body, head or bot"; return false; }
    S.skill = skillOf(sh["skill"].isNull() ? Json(0.7) : sh["skill"]);
    if (req["targets"].isArr()) {
        for (auto& t : req["targets"].items()) {
            Target x;
            if (!t["pos"].isArr()) { err = "targets: each one needs pos [x, y, z]"; return false; }
            x.base = toVec(t["pos"]);
            const Json& mv = t["moving"];
            if (mv.isObj()) {
                x.moving = true;
                x.axis = lower(mv["axis"].str("x")) == "z" ? 2 : 0;
                x.speed = (float)std::clamp(mv["speed"].num(5), 0.0, 100.0);
                x.amp = (float)std::clamp(mv["amplitude"].num(3), 0.1, 100.0);
                if (x.speed <= 0) x.moving = false;
            }
            x.health = (float)std::clamp(t["health"].num(0), 0.0, 100000.0);
            x.crouch = t["crouch"].boolean(false);
            S.targets.push_back(x);
        }
    } else if (S.builtin) {
        for (int i = 0; i < 5; i++) { Target x; x.base = Vec3((float)kLaneX[i], 0, (float)-kLaneDist[i]); S.targets.push_back(x); }
    }
    if (S.targets.empty()) { err = "targets: at least one"; return false; }
    if ((int)S.targets.size() > kMaxPlayers - 1) { err = "targets: at most 31"; return false; }
    return true;
}

// ---- one run --------------------------------------------------------------------------------------------------------
struct TargetResult { int hits = 0, headHits = 0, kills = 0, shots = 0; double damage = 0, firstKill = -1; };

struct RunOut { Json answer; std::string error; };

struct TargetState { double deadSince = -1; bool wasAlive = true; float health = 0; };

// The fire buttons for this tick: what the weapon and fire mode need held. Click weapons (semi-auto, a throw) press on
// every other tick: each press is a new click, the game's 0.15 s click buffer fires it the moment the weapon is ready.
uint16_t fireButtons(int w, bool alt, bool semiAuto, bool pressedLast) {
    const bool throwIt = alt && (w == WPN_KNIFE || w == WPN_SYRINGE);
    if ((semiAuto || throwIt) && pressedLast) return 0;
    if (w == WPN_STICKY) return BTN_FIRE | BTN_ALTFIRE;            // fire, and alt sets the armed ones off at once
    if (!alt) return BTN_FIRE;
    if (altFires(w)) return BTN_ALTFIRE;                           // lob, throw, bounce, stream: a shot of its own
    return BTN_FIRE | BTN_ALTFIRE;                                 // scope (rifle, sniper, crossbow), the laser's pull
}

void runOne(const Setup& S, const Rules& R, const std::string& sharedDir, RunOut& out) {
    const double w0 = maptoolNow();
    BotSimConfig cfg;
    cfg.mapText = S.mapText;
    cfg.mode = MODE_FFA;
    cfg.tickRate = S.tickRate;
    cfg.seed = S.seed;
    cfg.warmup = 0.25;
    cfg.timeLimit = (int)S.seconds + 600;
    cfg.scoreLimit = 10000;
    cfg.sharedDataDir = sharedDir;
    {
        BotSpec sh; sh.skill = S.skill; sh.name = "SHOOTER";
        cfg.bots.push_back(sh);
        for (size_t k = 0; k < S.targets.size(); k++) { BotSpec t; t.skill = BOT_BEGINNER; t.name = "TARGET " + std::to_string(k + 1); cfg.bots.push_back(t); }
    }
    BotSim sim;
    std::string err;
    if (!sim.start(cfg, &err)) { out.error = err; return; }
    Server& s = sim.server();
    applyToServer(s, R);
    const bool support = S.weapon == WPN_SYRINGE || S.weapon == WPN_LOCATOR;   // (no damage: never fired, below)
    if (!support && !s.weaponAllowed(S.weapon)) { out.error = std::string("the rules don't allow the ") + weaponKey(S.weapon) + " (weaponsAllowed / the mode)"; return; }
    // the pool narrowed to the weapon under test (a bot shooter picks what the pool allows: it fires only that one)
    if (!s.cfg_.custom.active()) s.cfg_.custom.name = "wpn-range";
    s.cfg_.custom.weapons = (uint16_t)(1u << S.weapon);
    const std::vector<int>& ids = sim.botIds();
    const int sh = ids[0];
    if (sh < 0) { out.error = "the server refused the shooter"; return; }
    const int N = (int)S.targets.size();
    std::vector<int> tid(N);
    std::vector<int> indexOf(kMaxPlayers, -1);
    for (int k = 0; k < N; k++) { tid[k] = ids[k + 1]; if (tid[k] < 0) { out.error = "the server refused a target"; return; } indexOf[tid[k]] = k; }
    const double dtTick = 1.0 / s.tickRate();
    double runT = 0;   // seconds since the run started (the targets' paths)
    auto targetHealth = [&](int k) { return S.targets[k].health > 0 ? S.targets[k].health : S.health > 0 ? S.health : s.lifeMaxHealth(); };
    // targets: scripted, no AI (they stand, face the shooter, ride their path)
    for (int k = 0; k < N; k++) {
        const Target T = S.targets[k];
        const Vec3 shooterPos = S.shooter;
        s.bots_[tid[k]].script = [T, shooterPos, &runT, dtTick](Server& srv, int id) {
            ServerPlayer& p = srv.players_[id];
            UserCmd c;
            c.weapon = kNoWeaponChange;
            Vec3 d = shooterPos - p.move.pos;
            c.yaw = std::atan2(-d.x, -d.z);
            if (T.crouch) c.buttons |= BTN_CROUCH;
            if (p.alive) { p.move.pos = T.at(runT + dtTick); p.move.vel = T.vel(runT + dtTick); }
            return c;
        };
    }
    auto place = [&](int k) {
        ServerPlayer& p = s.players_[tid[k]];
        if (!p.alive) s.spawnPlayer(tid[k]);
        p.move.pos = S.targets[k].at(runT); p.move.vel = S.targets[k].vel(runT);
        p.health = targetHealth(k);
        p.respawnAt = -1;
    };
    // the shooter: a player whose commands we queue
    ServerPlayer& P = s.players_[sh];
    P.isBot = false;
    P.cmdQueue.clear();
    P.events.clear();
    auto placeShooter = [&] {
        if (!P.alive) s.spawnPlayer(sh);
        P.move.pos = S.shooter; P.move.vel = Vec3();
        P.health = s.lifeMaxHealth();
        P.respawnAt = -1;
    };
    // the round's freeze (3 s after the forced start: nobody moves, no damage): stepped through before the clock starts
    for (int k = 0; k < 20 * s.tickRate() && s.ms_.phase != PHASE_LIVE; k += sim.step(std::max(1, s.tickRate() / 20), false)) {}
    if (s.ms_.phase != PHASE_LIVE) { out.error = "the match didn't go live (phase " + std::to_string((int)s.ms_.phase) + ")"; return; }
    P.cmdQueue.clear();
    P.events.clear();
    placeShooter();
    P.yaw = S.shooterYaw;
    for (int k = 0; k < N; k++) place(k);
    std::vector<TargetState> ts(N);
    std::vector<TargetResult> tr(N);
    for (int k = 0; k < N; k++) { ts[k].health = s.players_[tid[k]].health; ts[k].wasAlive = true; }

    const WeaponDef& wd = weaponDef(S.weapon);   // (this thread's rules: the server's, applyToServer set them)
    const WeaponAlt& wa = weaponAlt();
    const bool hitscanLike = !wd.projectile && !(S.alt && (S.weapon == WPN_KNIFE || S.weapon == WPN_SYRINGE));
    float pSpeed = wd.projSpeed, pGrav = wd.gravity;
    if (S.alt && S.weapon == WPN_ROCKET) { pSpeed *= wa.lobSpeed; pGrav = wa.lobGravity; }
    if (S.alt && S.weapon == WPN_KNIFE) { pSpeed = wa.knifeThrowSpeed; pGrav = wa.knifeThrowGravity; }
    if (S.alt && S.weapon == WPN_BUBBLE) pSpeed = 0.5f * (wa.streamSlow + wa.streamFast) * (wd.projSpeed / baseWeaponDef(WPN_BUBBLE).projSpeed);
    if (S.weapon == WPN_ROCKET && !S.alt) pGrav = 0;   // (the primary flies straight: Projectile::gravMul)
    Json notes = Json::array();
    if (support) notes.push(std::string("the ") + weaponKey(S.weapon) + " does no damage: nothing to hit on the range");
    if (S.weapon == WPN_STICKY) notes.push("stickies are set off as soon as they arm (alt held with fire: the fastest a player could)");
    if (S.alt && S.weapon == WPN_BUBBLE) notes.push("the stream's bubbles fly at different speeds: one aim (led and raised for the middle speed) lands only some of them far out");
    if (S.alt && S.weapon == WPN_SHOTGUN && S.builtin) notes.push("bouncing pellets: the range has no walls to bounce off");
    if (S.weapon == WPN_KNIFE && !S.alt) {
        float reach = wd.range + wa.knifeGrow + kPlayerHalfWidth;
        bool any = false;
        for (auto& t : S.targets) if (length(t.base - S.shooter) <= reach + 1) any = true;
        if (!any) notes.push("every target is out of the knife's reach (" + std::to_string(wd.range).substr(0, 4) + " m): put one closer");
    }
    if (R.hasLogic) notes.push("the set's hit / kill logic runs on the server (damage numbers are after it)");
    notes.push("the shooter can't die or be pushed: its health and spot are reset every tick");

    // ---- the loop
    const int ticks = std::max(1, (int)std::lround(S.seconds * s.tickRate()));
    const uint8_t msec = (uint8_t)(1000 / s.tickRate());
    int ptr = 0;                 // lanes: the target in turn
    bool pressedLast = false;
    uint32_t seq = 1;
    int prevShots = P.shotsFired, prevHits = P.shotsHit;
    // The tally counts shots and hits in one unit, so accuracy = hits / shots means the same for every weapon: a shot is
    // what the game counts in shotsFired (a hitscan / projectile shot, one 1 ms tick of the laser's beam), except the
    // knife's swing, which the game counts once per tick of its cut window (about 120 a swing): one swing here. A hit is
    // a shot (a swing) that took health off that target; shotsHit = shots that hit anyone. (The per hit list below is
    // the game's hit feedback, EV_HIT, which batches the laser's ticks every 50 ms: a feed, not a count.)
    // The knife's throw and the bubble stream aren't in shotsFired at all (and a bubble isn't in shotsHit): there a
    // shot is a projectile the tick launched (the server's projectile ids; only the shooter fires) and a hit is a
    // target that took damage that tick (one knife, one bubble).
    const bool swingUnit = S.weapon == WPN_KNIFE && !S.alt;
    const bool projUnit = S.alt && (S.weapon == WPN_KNIFE || S.weapon == WPN_BUBBLE);
    uint16_t prevProjId = s.nextProjId_;
    int shotsT = 0, shotsHitT = 0;
    bool firingPrev = false, shotLanded = false;
    std::vector<char> hitThisShot(N, 0);
    Json shots = Json::array(), frames = Json::array();
    int shotCount = 0;
    bool shotsCut = false;
    Json frameShots = Json::array();
    double nextFrame = 0;
    long long ticksRun = 0;
    const double simW0 = maptoolNow();
    auto eyeOf = [&]() { return s.shooterEye(sh); };
    auto aimPointOf = [&](int k, double tAhead) {
        const ServerPlayer& q = s.players_[tid[k]];
        Vec3 pos = tAhead > 0 ? S.targets[k].at(runT + tAhead) : q.move.pos;
        AABB bb = playerBox(pos, (q.move.flags & MF_CROUCHED) != 0, s.bodyScale());
        if (S.aim == AIM_HEAD) return headBox(bb).center();
        return bb.center();
    };
    auto focus = [&]() -> int {
        if (S.aim == AIM_BOT) {
            int t = s.bots_[sh].target;
            if (t >= 0 && t < kMaxPlayers && indexOf[t] >= 0 && s.players_[t].alive) return indexOf[t];
        }
        if (!S.nearest && s.players_[tid[ptr % N]].alive && S.aim != AIM_BOT) return ptr % N;
        int best = -1; float bd = 1e30f;
        Vec3 e = eyeOf();
        for (int k = 0; k < N; k++) if (s.players_[tid[k]].alive) { float d = length(s.players_[tid[k]].move.pos - e); if (d < bd) { bd = d; best = k; } }
        return best;
    };
    auto pushShot = [&](double t, bool hit, bool head, double dmg, int target, double dist) {
        shotCount++;
        if ((int)shots.size() >= S.maxShots) { shotsCut = true; return; }
        Json o = Json::object();
        o.set("t", r3(t)); o.set("hit", hit); o.set("head", head); o.set("dmg", r3(dmg)); o.set("target", target); o.set("dist", r2(dist));
        shots.push(std::move(o));
    };
    auto readEvents = [&](double t) {
        int hitTarget = -1;
        for (const GameEvent& e : P.events) {
            if (e.type == EV_HIT && e.a == sh && e.b < kMaxPlayers && indexOf[e.b] >= 0) {
                int k = indexOf[e.b];
                bool head = e.text == "h";
                if (head) tr[k].headHits++;
                pushShot(t, true, head, e.c / 100.0, k, length(s.players_[e.b].move.pos + Vec3(0, kPlayerHeight * 0.5f, 0) - eyeOf()));
                hitTarget = k;
            }
        }
        P.events.clear();
        return hitTarget;
    };
    auto frame = [&](double t, int fk) {
        Json f = Json::object();
        f.set("t", r3(t));
        Json pl = Json::array();
        {
            Json o = Json::object();
            o.set("pos", v3(P.move.pos)); o.set("yaw", r3(P.yaw)); o.set("pitch", r3(P.pitch)); o.set("health", r2(P.health)); o.set("alive", P.alive); o.set("shooter", true);
            o.set("weapon", weaponKey(std::clamp(P.weapon, 0, WPN_COUNT - 1))); o.set("target", fk);
            pl.push(std::move(o));
        }
        for (int k = 0; k < N; k++) {
            const ServerPlayer& q = s.players_[tid[k]];
            Json o = Json::object();
            o.set("pos", v3(q.move.pos)); o.set("yaw", r3(q.yaw)); o.set("pitch", 0); o.set("health", r2(std::max(0.0f, q.health))); o.set("alive", q.alive);
            if (q.move.flags & MF_CROUCHED) o.set("crouched", true);
            pl.push(std::move(o));
        }
        f.set("players", std::move(pl));
        Json pr = Json::array();
        for (const Projectile& p : s.projectiles_)
            if (p.active && p.owner == sh) { Json o = Json::object(); o.set("pos", v3(p.pos, 100.0)); o.set("stuck", p.stuck); o.set("type", weaponKey(std::clamp(p.type, 0, WPN_COUNT - 1))); pr.push(std::move(o)); }
        f.set("projectiles", std::move(pr));
        f.set("shots", std::move(frameShots));
        frameShots = Json::array();
        frames.push(std::move(f));
    };
    for (int i = 0; i <= ticks; i++) {
        runT = i * dtTick;
        // respawns (ours) and the shooter's spot
        for (int k = 0; k < N; k++) {
            ServerPlayer& q = s.players_[tid[k]];
            if (!q.alive) {
                q.respawnAt = -1;
                if (ts[k].deadSince < 0) ts[k].deadSince = runT;
                if (runT - ts[k].deadSince >= S.respawn - 1e-9) { place(k); ts[k].deadSince = -1; ts[k].wasAlive = true; ts[k].health = q.health; }
            }
        }
        placeShooter();
        const int fk = support ? -1 : focus();
        if (S.frames && runT >= nextFrame - 1e-9) { frame(runT, fk); nextFrame += S.frameEvery; }
        if (i == ticks) break;
        // the command
        UserCmd c;
        c.seq = seq++;
        c.msec = msec;
        c.viewTick = (uint32_t)s.timeMs_;
        c.weapon = (uint8_t)S.weapon;
        c.yaw = P.yaw; c.pitch = P.pitch;
        bool want = false;
        if (S.aim == AIM_BOT) {
            UserCmd b = botThinkDirect(s, sh);
            c.yaw = b.yaw; c.pitch = b.pitch;
            want = (b.buttons & (BTN_FIRE | BTN_ALTFIRE)) != 0 && fk >= 0;
        } else if (fk >= 0) {
            Vec3 e = eyeOf(), p = aimPointOf(fk, 0);
            if (!hitscanLike && pSpeed > 0) {   // lead along the path, aim above for the drop (flight time iterated)
                const Vec3 launch = e;
                float tf = length(p - launch) / pSpeed;
                for (int it = 0; it < 6; it++) {
                    Vec3 q = aimPointOf(fk, tf);
                    q.y += 0.5f * pGrav * tf * tf;
                    p = q;
                    tf = length(p - launch) / pSpeed;
                }
            }
            Vec3 d = p - e;
            float l = length(d);
            if (l > 1e-6f) {
                d = d * (1.0f / l);
                c.yaw = std::atan2(-d.x, -d.z);
                c.pitch = std::asin(std::clamp(d.y, -1.0f, 1.0f));
            }
            want = true;
        }
        if (support) want = false;
        uint16_t btn = 0;
        if (want && P.weapon == S.weapon) btn = fireButtons(S.weapon, S.alt, wd.semiAuto, pressedLast);
        pressedLast = btn != 0;
        c.buttons = btn;
        P.cmdQueue.push_back(c);
        for (int k = 0; k < N; k++) ts[k].health = s.players_[tid[k]].health, ts[k].wasAlive = s.players_[tid[k]].alive;
        const Vec3 eye = eyeOf();
        // the tick
        ticksRun += sim.step(1, false);
        const double tNow = runT + dtTick;   // (what happened, happened during this tick)
        const int hitTarget = readEvents(runT);
        const int fired = projUnit ? (uint16_t)(s.nextProjId_ - prevProjId) : P.shotsFired - prevShots;
        const int landed = P.shotsHit - prevHits;
        prevShots = P.shotsFired; prevHits = P.shotsHit; prevProjId = s.nextProjId_;
        const bool newSwing = swingUnit && fired > 0 && !firingPrev;
        firingPrev = fired > 0;
        const int newShots = swingUnit ? (newSwing ? 1 : 0) : fired;
        if (newShots > 0) {
            shotsT += newShots;
            if (fk >= 0) tr[fk].shots += newShots;
            if (swingUnit) { shotLanded = false; std::fill(hitThisShot.begin(), hitThisShot.end(), 0); }
        }
        if (!swingUnit && !projUnit) shotsHitT += landed;
        if (fired > 0 && hitscanLike) {
            for (int m = landed; m < fired; m++) pushShot(runT, false, false, 0, -1, 0);
            if (S.frames && frameShots.size() < 24) {   // (the traces: the aim ray, to the target hit or the first wall)
                Vec3 dir = angleForward(c.yaw, c.pitch);
                float range = wd.range > 0 ? wd.range : 100.0f, bang = -1;
                float wall = s.map_.raycastBullet(eye, dir, range, &bang);
                float end = wall >= 0 ? wall : range;
                if (hitTarget >= 0) end = std::min(end, length(aimPointOf(hitTarget, 0) - eye));
                Json o = Json::object();
                o.set("from", v3(eye, 100.0)); o.set("to", v3(eye + dir * end, 100.0)); o.set("hit", hitTarget >= 0);
                frameShots.push(std::move(o));
            }
        }
        for (int k = 0; k < N; k++) {
            ServerPlayer& q = s.players_[tid[k]];
            if (ts[k].wasAlive) {
                float took = ts[k].health - std::max(0.0f, q.health);
                if (!q.alive) took = std::max(0.0f, ts[k].health);
                if (took > 0) {
                    tr[k].damage += took;
                    if (!swingUnit) { tr[k].hits++; if (projUnit) shotsHitT++; }   // (a tick's damage to a target: one shot's, or one beam tick's)
                    else if (!hitThisShot[k]) { hitThisShot[k] = 1; tr[k].hits++; if (!shotLanded) { shotLanded = true; shotsHitT++; } }
                }
                if (!q.alive) {
                    tr[k].kills++;
                    if (tr[k].firstKill < 0) tr[k].firstKill = tNow;
                    ts[k].deadSince = tNow;
                    q.respawnAt = -1;
                    if (!S.nearest && ptr % N == k) ptr = (ptr + 1) % N;
                }
            }
        }
    }
    // hits still batched (the laser's 50 ms feedback): out now
    for (int k = 0; k < N; k++) s.flushHit(sh, tid[k]);
    readEvents(S.seconds);
    const double simWall = maptoolNow() - simW0;
    const int tickRateRun = s.tickRate();
    std::vector<float> healthOf(N);
    for (int k = 0; k < N; k++) healthOf[k] = targetHealth(k);
    sim.stop();   // (s and P are gone from here)

    Json a = Json::object();
    a.set("ok", true);
    a.set("via", "wpnrange");
    a.set("weapon", weaponKey(S.weapon)); a.set("alt", S.alt); a.set("seconds", S.seconds);
    a.set("map", S.mapLabel); a.set("builtinMap", S.builtin);
    a.set("aim", S.aim == AIM_HEAD ? "head" : S.aim == AIM_BOT ? "bot" : "perfect");
    if (S.aim == AIM_BOT) a.set("skill", botSkillName(S.skill));
    a.set("order", S.nearest ? "nearest" : "lanes");
    a.set("tickRate", tickRateRun);
    a.set("seed", (double)S.seed);
    startJson(a, R);
    Json tj = Json::array();
    double dmgSum = 0; int hitSum = 0, headSum = 0, killSum = 0;
    for (int k = 0; k < N; k++) {
        Json o = Json::object();
        o.set("index", k); o.set("pos", v3(S.targets[k].base));
        o.set("hits", tr[k].hits); o.set("headHits", tr[k].headHits); o.set("damage", r2(tr[k].damage)); o.set("kills", tr[k].kills);
        o.set("firstKill", tr[k].firstKill >= 0 ? Json(r3(tr[k].firstKill)) : Json()); o.set("shots", tr[k].shots);
        o.set("dist", r2(length(S.targets[k].base - S.shooter)));
        o.set("health", healthOf[k]);
        if (S.targets[k].moving) o.set("moving", true);
        tj.push(std::move(o));
        dmgSum += tr[k].damage; hitSum += tr[k].hits; headSum += tr[k].headHits; killSum += tr[k].kills;
    }
    a.set("targets", std::move(tj));
    a.set("shots", std::move(shots));
    if (shotsCut) a.set("shotsCut", shotCount);
    a.set("frames", std::move(frames));
    a.set("frameEvery", S.frameEvery);
    Json tot = Json::object();
    tot.set("shots", shotsT); tot.set("hits", hitSum); tot.set("headHits", headSum); tot.set("damage", r2(dmgSum)); tot.set("kills", killSum);
    tot.set("dps", S.seconds > 0 ? r2(dmgSum / S.seconds) : 0.0);
    if (projUnit) shotsHitT = std::min(shotsHitT, shotsT);   // (a bubble's splash can reach two targets: still one shot that hit)
    tot.set("shotsHit", shotsHitT); tot.set("accuracy", shotsT ? Json((double)shotsHitT / shotsT) : Json());
    a.set("totals", std::move(tot));
    a.set("notes", std::move(notes));
    Json perf = Json::object();
    perf.set("wallSeconds", maptoolNow() - w0); perf.set("simSeconds", simWall); perf.set("ticks", (double)ticksRun);
    perf.set("speedup", simWall > 0 ? S.seconds / simWall : 0.0);
    a.set("perf", std::move(perf));
    out.answer = std::move(a);
}

int runRange(const Json& req, Json& out, CmdCtx&) {
    const double t0 = maptoolNow();
    Setup S;
    std::string err;
    if (!parseSetup(req, S, err)) return cmdError(out, err);
    // the runs: the request's set, and `compare` (another set text, or several) on threads of their own
    std::vector<Rules> rules(1);
    if (!requestRules(req, rules[0], err)) return cmdError(out, err);
    std::vector<Json> cmpReqs;
    if (req["compare"].isStr() || req["compare"].isObj()) cmpReqs.push_back(req["compare"]);
    else for (auto& c : req["compare"].items()) cmpReqs.push_back(c);
    for (auto& c : cmpReqs) {
        Json r2 = Json::object();
        r2.set("weapons", c.isObj() && c.has("weapons") ? c["weapons"] : c);
        if (c.isObj() && c.has("qmode")) r2.set("qmode", c["qmode"]); else if (req.has("qmode")) r2.set("qmode", req["qmode"]);
        if (req.has("weaponsAllowed")) r2.set("weaponsAllowed", req["weaponsAllowed"]);
        Rules r;
        if (!requestRules(r2, r, err)) return cmdError(out, "compare: " + err);
        rules.push_back(r);
    }
    netInit();
    fs::path dir = fs::temp_directory_path() / ("quark_wpnrange_" + std::to_string((long long)(maptoolNow() * 1000)) + "_" + std::to_string(std::random_device()() % 1000000u));
    if (!BotSim::prepareSharedDir(dir.u8string(), S.mapText, &err)) return cmdError(out, err);
    std::vector<RunOut> outs(rules.size());
    {
        // one fresh thread per run (bots.cpp's thread_locals start clean: the same request gives the same answer)
        std::vector<std::thread> th;
        for (size_t k = 0; k < rules.size(); k++) th.emplace_back([&, k] { runOne(S, rules[k], dir.u8string(), outs[k]); });
        for (auto& t : th) t.join();
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    for (auto& o : outs) if (!o.error.empty()) return cmdError(out, o.error);
    out = std::move(outs[0].answer);
    if (outs.size() > 1) {
        Json c = Json::array();
        for (size_t k = 1; k < outs.size(); k++) c.push(std::move(outs[k].answer));
        out.set("compare", std::move(c));
    }
    out.set("ms", (maptoolNow() - t0) * 1000.0);
    return 0;
}

int runRangeMap(const Json&, Json& out, CmdCtx&) {
    out = Json::object();
    out.set("text", rangeMapText());
    Json lanes = Json::array();
    for (int i = 0; i < 5; i++) { Json o = Json::object(); o.set("dist", kLaneDist[i]); o.set("pos", v3(Vec3((float)kLaneX[i], 0, (float)-kLaneDist[i]))); lanes.push(std::move(o)); }
    out.set("lanes", std::move(lanes));
    out.set("shooter", v3(Vec3()));
    return 0;
}

}  // namespace
}  // namespace wpn

MAPTOOL_COMMAND("wpnrange", wpn::runRange, "{weapons?, qmode?, map?, weapon, alt?, targets: [{pos, moving?: {axis, speed, amplitude}, health?, crouch?}], shooter: {pos, aim: perfect|head|bot, skill?}, seconds, seed, order?, compare?} -> per target hits / damage / kills, shots, frames (weapon_editor/native/README.md)");
MAPTOOL_COMMAND("wpnrangemap", wpn::runRangeMap, "{} -> {text, lanes, shooter}: the built-in range wpnrange uses when no map is given");
