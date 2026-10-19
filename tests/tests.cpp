// Headless tests: movement, map parsing, and real client/server sessions over UDP
// on localhost, including every game mode.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "game/server.h"
#include "game/netclient.h"
#include "game/gamemodes.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

// Pump a server and some clients for `seconds` of real time, feeding each client an input.
template <class F>
static void pump(Server& s, std::vector<NetClient*>& cls, double seconds, F input) {
    double end = timeNow() + seconds;
    double nextTick = timeNow();
    while (timeNow() < end) {
        s.frame();
        for (auto* c : cls) c->poll();
        if (timeNow() >= nextTick) {
            for (size_t i = 0; i < cls.size(); i++) cls[i]->tick(input((int)i));
            nextTick += kTickDt;
        }
        sleepMs(1);
    }
}

static void testMovement() {
    printf("movement\n");
    Map m;
    std::string err;
    CHECK(m.parse("box -50 -1 -50 50 0 50\nbox 5 0 -50 6 3 50\nbox -3 0 -50 -2 0.4 50\nspawn any 0 0 0\n", &err));
    MoveState s;
    s.pos = {0, 2, 0};
    UserCmd c;
    for (int i = 0; i < 120; i++) pmove(s, c, m, kTickDt);
    CHECK(std::fabs(s.pos.y) < 0.01f);
    CHECK(s.flags & MF_ONGROUND);
    // walk +x into the wall
    c.yaw = -PI / 2; // facing +x
    c.forward = 127;
    for (int i = 0; i < 240; i++) pmove(s, c, m, kTickDt);
    CHECK(s.pos.x < 5.0f - kPlayerHalfWidth + 0.01f && s.pos.x > 4.0f);
    // walk -x over the small step
    c.yaw = PI / 2;
    for (int i = 0; i < 180; i++) pmove(s, c, m, kTickDt);
    CHECK(s.pos.x < -3.5f);
    // jump
    c.forward = 0;
    c.buttons = BTN_JUMP;
    float maxY = 0;
    for (int i = 0; i < 60; i++) { pmove(s, c, m, kTickDt); maxY = std::max(maxY, s.pos.y); }
    CHECK(maxY > 1.0f);
}

static void testMaps() {
    printf("maps\n");
    Map arena, sprint;
    std::string err;
    CHECK(arena.load("arena", &err));
    if (!err.empty()) printf("  %s\n", err.c_str());
    CHECK(sprint.load("sprint", &err));
    for (int m : {MODE_VS, MODE_BOMB, MODE_KOTH, MODE_PAYLOAD, MODE_FFA}) CHECK(arena.supportsMode(m));
    CHECK(sprint.supportsMode(MODE_RACE));
    CHECK(arena.payloadLength() > 20);
}

static UserCmd idle(int) { return UserCmd(); }

static void testSession(int mode, const char* mapName, uint16_t port) {
    printf("session: %s on %s\n", modeName(mode), mapName);
    Server s;
    ServerConfig cfg;
    cfg.map = mapName;
    cfg.mode = mode;
    cfg.port = port;
    cfg.roundsToWin = mode == MODE_BOMB ? 2 : 1;
    cfg.timeLimit = 3;
    cfg.scoreLimit = 3;
    s.setLog([](const std::string&) {});
    std::string err;
    if (!s.start(cfg, &err)) { printf("  server start failed: %s\n", err.c_str()); g_fail++; return; }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "alpha");
    b.connect("127.0.0.1:" + std::to_string(port), "bravo");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 0.5, idle);
    CHECK(a.state() == ConnState::Connected);
    CHECK(b.state() == ConnState::Connected);
    CHECK(s.playerCount() == 2);
    CHECK(a.hasSnapshot() && b.hasSnapshot());
    if (!a.hasSnapshot() || !b.hasSnapshot()) { s.stop(); return; }
    CHECK(a.playerName(b.clientId()) == "bravo");
    CHECK(a.mode().phase == PHASE_WARMUP || a.mode().phase == PHASE_FREEZE);

    // Warmup countdown (5s) then freeze (3s); then live.
    pump(s, cls, 8.6, idle);
    CHECK(s.ms_.phase == PHASE_LIVE);
    CHECK(a.mode().phase == PHASE_LIVE);

    // Prediction must agree with the server for a moving player.
    auto walk = [](int i) { UserCmd c; c.forward = i == 0 ? 127 : 0; c.side = 40; return c; };
    pump(s, cls, 1.0, walk);
    pump(s, cls, 0.3, idle);
    const ServerPlayer& sp = s.players_[a.clientId()];
    if (sp.alive) CHECK(length(sp.move.pos - a.predicted().pos) < 0.05f);

    // Mode specific: make something happen.
    if (mode == MODE_VS || mode == MODE_BOMB || mode == MODE_FFA || mode == MODE_PAYLOAD) {
        int victim = b.clientId();
        s.damagePlayer(victim, a.clientId(), 500, WPN_RIFLE, {});
        pump(s, cls, 0.3, idle);
        bool sawKill = false;
        for (auto& e : a.takeEvents()) if (e.type == EV_KILL && e.b == victim) sawKill = true;
        CHECK(sawKill);
    }
    if (mode == MODE_VS) {
        pump(s, cls, 0.2, idle);
        CHECK(s.ms_.phase == PHASE_ROUND_END || s.ms_.phase == PHASE_MATCH_END);
        CHECK(s.ms_.scoreA + s.ms_.scoreB == 1);
    }
    if (mode == MODE_FFA) {
        pump(s, cls, 3.0, idle); // time limit 3s
        CHECK(s.ms_.phase == PHASE_MATCH_END);
    }
    if (mode == MODE_KOTH) {
        // Put alpha on the hill, alone.
        auto& p = s.players_[a.clientId()];
        p.move.pos = s.map_.hill.center();
        p.move.pos.y = s.map_.hill.mn.y;
        pump(s, cls, 3.5, idle);
        CHECK(s.ms_.phase == PHASE_MATCH_END);
    }
    if (mode == MODE_BOMB) {
        // bravo was killed; wait for next round then plant with the carrier.
        pump(s, cls, 4.5 + 3.2, idle);
        CHECK(s.ms_.phase == PHASE_LIVE);
        int carrier = s.ms_.bombCarrier;
        CHECK(carrier != 255);
        if (carrier != 255) {
            auto& p = s.players_[carrier];
            Vec3 c = s.map_.bombSites[0].center();
            p.move.pos = {c.x, 0.1f, c.z};
            NetClient* cc = carrier == a.clientId() ? &a : &b;
            auto plant = [&](int i) { UserCmd u; if (cls[i] == cc) u.buttons = BTN_USE; return u; };
            pump(s, cls, 3.5, plant);
            CHECK(s.ms_.bombState == BOMB_PLANTED);
            CHECK(cc->mode().bombState == BOMB_PLANTED);
        }
    }
    if (mode == MODE_PAYLOAD) {
        pump(s, cls, 4.5 + 3.2, idle); // round 2 starts after the kill ended round 1? (no: respawn mode)
    }
    if (mode == MODE_RACE) {
        auto& p = s.players_[a.clientId()];
        for (size_t i = 0; i < s.map_.checkpoints.size(); i++) {
            const AABB& c = s.map_.checkpoints[i];
            p.move.pos = {c.center().x, c.mn.y + 0.01f, c.center().z};
            p.move.vel = {};
            pump(s, cls, 0.1, idle);
        }
        CHECK(s.ms_.phase == PHASE_ROUND_END || s.ms_.phase == PHASE_MATCH_END);
        CHECK(s.players_[a.clientId()].score == 1);
    }

    // String commands + chat delivered reliably.
    a.sendCommand("say hello there");
    pump(s, cls, 0.3, idle);
    bool chat = false;
    for (auto& e : b.takeEvents()) if (e.type == EV_CHAT && e.text == "hello there") chat = true;
    CHECK(chat);

    a.disconnect();
    pump(s, cls, 0.2, idle);
    CHECK(s.playerCount() == 1);
    s.stop();
}

static void testPayloadPush(uint16_t port) {
    printf("payload push\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "arena"; cfg.mode = MODE_PAYLOAD; cfg.port = port;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "a");
    b.connect("127.0.0.1:" + std::to_string(port), "b");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 0.4, idle);
    s.command("forcestart");
    pump(s, cls, 3.2, idle);
    CHECK(s.ms_.phase == PHASE_LIVE);
    int att = -1;
    for (int i = 0; i < kMaxPlayers; i++) if (s.players_[i].connected && s.players_[i].team == s.ms_.attackers) att = i;
    CHECK(att >= 0);
    if (att >= 0) {
        for (int k = 0; k < 20; k++) {
            s.players_[att].move.pos = s.map_.payloadPoint(s.ms_.payloadDist);
            pump(s, cls, 0.1, idle);
        }
        CHECK(s.ms_.payloadDist > 2.0f);
    }
    s.stop();
}

static void testPacketLoss(uint16_t port) {
    printf("lag compensation hitscan\n");
    Server s;
    ServerConfig cfg;
    cfg.map = "arena"; cfg.mode = MODE_FFA; cfg.port = port;
    s.setLog([](const std::string&) {});
    if (!s.start(cfg)) { g_fail++; return; }
    NetClient a, b;
    a.connect("127.0.0.1:" + std::to_string(port), "shooter");
    b.connect("127.0.0.1:" + std::to_string(port), "target");
    std::vector<NetClient*> cls = {&a, &b};
    pump(s, cls, 2.5, idle); // warmup spawn
    auto& sa = s.players_[a.clientId()];
    auto& sb = s.players_[b.clientId()];
    CHECK(sa.alive && sb.alive);
    sa.move.pos = {9, 0, 3}; sb.move.pos = {9, 0, -3};
    sa.move.vel = sb.move.vel = {};
    pump(s, cls, 0.4, idle);
    // Aim from a's eye toward b's chest, using the interpolated view a sees.
    auto fire = [&](int i) {
        UserCmd c;
        if (i == 0) {
            c.yaw = 0; // looking down -z
            const PlayerSnap* t = nullptr;
            auto ip = a.interpolatedPlayers();
            if (ip[b.clientId()].valid) t = &ip[b.clientId()].snap;
            Vec3 eye = a.predicted().pos + Vec3(0, kEyeHeight, 0);
            Vec3 tgt = t ? t->pos + Vec3(0, 1.0f, 0) : Vec3(0, 2, -3);
            Vec3 d = normalize(tgt - eye);
            c.pitch = std::asin(d.y);
            c.yaw = std::atan2(-d.x, -d.z);
            c.buttons = BTN_FIRE;
        }
        return c;
    };
    float hp0 = sb.health;
    pump(s, cls, 0.5, fire);
    CHECK(sb.health < hp0 || !sb.alive);
    s.stop();
}

int main(int argc, char** argv) {
    setExecutableDir(argv[0]);
    netInit();
    srand(1234);
    testMovement();
    testMaps();
    uint16_t port = 28500;
    testSession(MODE_VS, "arena", port++);
    testSession(MODE_BOMB, "arena", port++);
    testSession(MODE_KOTH, "arena", port++);
    testSession(MODE_PAYLOAD, "arena", port++);
    testSession(MODE_FFA, "arena", port++);
    testSession(MODE_RACE, "sprint", port++);
    testPayloadPush(port++);
    testPacketLoss(port++);
    netShutdown();
    printf(g_fail ? "%d FAILURE(S)\n" : "all tests passed\n", g_fail);
    return g_fail ? 1 : 0;
}
