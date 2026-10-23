#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "protocol.h"

static int16_t angQ(float a) { return (int16_t)std::lround(std::remainder(a, 2 * PI) * (32767.0f / PI)); }
static float angD(int16_t q) { return q * (PI / 32767.0f); }

void writeSnapshotClient(Writer& w, const Snapshot& s) {
    w.u32(s.tick);
    w.u32((uint32_t)s.timeMs);
    w.u16(s.tickRate);
    w.u16(s.snapRate);
    w.u32(s.ackCmdSeq);
    w.u16(s.ackStringCmd);
    w.u8(s.clientId);
    // own movement state, exact (prediction replays from it); the grapple / dash parts only while active
    const MoveState& m = s.self;
    uint8_t opt = ((m.flags & MF_HOOKED) ? 1 : 0) | (m.dashTimer > 0 ? 2 : 0) | (m.dashCharges != kDashCharges || m.dashCooldownMs > 0 ? 4 : 0) | (m.hookCooldown > 0 ? 8 : 0) |
                  ((m.flags & MF_HOOKED) && m.hookTarget ? 16 : 0) | ((m.flags & MF_PULLED) ? 32 : 0) |
                  (m.jumpBuffer > 0 || m.jumpGrace > 0 ? 64 : 0) | (m.dashTimer > 0 && m.dashBounces > 0 ? 128 : 0);
    w.vec(m.pos); w.vec(m.vel); w.u8(m.flags); w.u8(m.mods); w.u8(opt);
    if (opt & 1) w.vec(m.hookPoint);
    if (opt & 2) { w.f32(m.dashTimer); w.f32(m.dashDirX); w.f32(m.dashDirZ); w.f32(m.dashExitSpeed); w.f32(m.dashUp); }
    if (opt & 4) { w.u8(m.dashCharges); w.u16(m.dashCooldownMs); }   // (42: charges + the cooldown in ms, when not 2 / running; 41: the charges alone; before: the cooldown f32)
    if (opt & 8) w.f32(m.hookCooldown);
    if (opt & 16) w.u8(m.hookTarget);    // (38: the grapple latched onto a player, and the one it holds)
    if (opt & 32) w.vec(m.pullPoint);
    if (opt & 64) { w.f32(m.jumpBuffer); w.f32(m.jumpGrace); }   // (protocol 39; older writers never set these bits)
    if (opt & 128) w.u8(m.dashBounces);
    w.u8(s.selfHealth); w.u8(s.selfWeapon); w.u8(s.knifeAway); w.u8(s.syringeAway);
    w.u32(s.baseTick);
}

bool readSnapshotClient(Reader& r, Snapshot& s, uint16_t proto) {
    s.tick = r.u32();
    s.timeMs = r.u32();
    s.tickRate = r.u16();
    s.snapRate = r.u16();
    s.ackCmdSeq = r.u32();
    s.ackStringCmd = r.u16();
    s.clientId = r.u8();
    MoveState& m = s.self;
    m = MoveState();
    m.pos = r.vec(); m.vel = r.vec(); m.flags = r.u8(); m.mods = r.u8();
    uint8_t opt = r.u8();
    if (opt & 1) m.hookPoint = r.vec();
    if (opt & 2) { m.dashTimer = r.f32(); m.dashDirX = r.f32(); m.dashDirZ = r.f32(); m.dashExitSpeed = r.f32(); m.dashUp = r.f32(); }
    if (opt & 4) {
        if (proto >= kSnapDashChargesSince) { m.dashCharges = r.u8(); if (proto >= kSnapDashCooldownSince) m.dashCooldownMs = r.u16(); }
        else m.dashCooldownMs = (uint16_t)std::min(60000.0f, std::max(0.0f, std::ceil(r.f32() * 1000.0f)));   // (older: the cooldown alone, as an f32 in s)
    }
    if (opt & 8) m.hookCooldown = r.f32();
    if (opt & 16) m.hookTarget = r.u8();   // (older demos never set these bits)
    if (opt & 32) m.pullPoint = r.vec();
    if (m.hookTarget > kMaxPlayers) m.hookTarget = 0;
    if (opt & 64) { m.jumpBuffer = r.f32(); m.jumpGrace = r.f32(); }
    if (opt & 128) m.dashBounces = r.u8();
    s.selfHealth = r.u8(); s.selfWeapon = r.u8(); s.knifeAway = r.u8(); s.syringeAway = proto >= kSnapSyringeSince ? r.u8() : 0;
    s.baseTick = r.u32();
    if (s.clientId >= kMaxPlayers || s.selfWeapon >= WPN_COUNT) return false;   // (ids index fixed arrays on the client)
    return !r.bad();
}

static void writeMode(Writer& w, const ModeState& m) {
    // timers in tenths of a second (the HUD shows whole seconds): the mode state then changes at most
    // 10 times a second instead of every tick, so most snapshots send it as "unchanged" (1 byte)
    auto ds = [&](float t) { w.u16((uint16_t)clampf(std::round(t * 10.0f), 0, 65535)); };
    w.u8(m.mode); w.u8(m.phase); ds(m.timeLeft); w.u8(m.round); w.u8(m.roundsToWin);
    w.i16(m.scoreA); w.i16(m.scoreB); w.u8(m.attackers); w.u8(m.flags);
    w.u8(m.bombState); w.u8(m.bombCarrier); w.vecq(m.bombPos); ds(m.bombTimer);
    w.u8((uint8_t)clampf(std::round(m.actionProgress * 255.0f), 0, 255)); w.u8(m.actionPlayer);
    w.u8(m.hillOwner); w.i16(m.target);
    w.f32(m.payloadDist); w.f32(m.payloadLen); // exact: the cart is solid and prediction must match
    w.u8(m.point); w.u8(m.pointCount); w.u8((uint8_t)clampf(std::round(m.capture * 100.0f), 0, 100));
    w.u8(m.voteActive); w.u8(m.voteYes); w.u8(m.voteNo); w.u8(m.voteVoters); ds(m.voteTimeLeft); w.u8(m.duelMod); w.u8(m.arenaVariant); w.u16(m.weaponMask);
}
static bool readMode(Reader& r, ModeState& m) {
    m.mode = r.u8(); m.phase = r.u8(); m.timeLeft = r.u16() / 10.0f; m.round = r.u8(); m.roundsToWin = r.u8();
    m.scoreA = r.i16(); m.scoreB = r.i16(); m.attackers = r.u8(); m.flags = r.u8();
    m.bombState = r.u8(); m.bombCarrier = r.u8(); m.bombPos = r.vecq(); m.bombTimer = r.u16() / 10.0f;
    m.actionProgress = r.u8() / 255.0f; m.actionPlayer = r.u8();
    m.hillOwner = r.u8(); m.target = r.i16();
    m.payloadDist = r.f32(); m.payloadLen = r.f32();
    m.point = r.u8(); m.pointCount = r.u8(); m.capture = r.u8() / 100.0f;
    m.voteActive = r.u8(); m.voteYes = r.u8(); m.voteNo = r.u8(); m.voteVoters = r.u8(); m.voteTimeLeft = r.u16() / 10.0f; m.duelMod = r.u8(); m.arenaVariant = r.u8(); m.weaponMask = r.u16();
    return m.mode < MODE_COUNT;   // (indexes the per-mode HUD / controls tables)
}

// Player field bits. P_FULL: every field follows (new player, or visibility changed). P_HIDDEN is
// state, not a field: a hidden player carries only scoreboard fields.
enum : uint16_t {
    P_FLAGS = 1 << 0, P_POS = 1 << 1, P_YAW = 1 << 2, P_PITCH = 1 << 3, P_HEALTH = 1 << 4, P_WEAPON = 1 << 5,
    P_KILLS = 1 << 6, P_DEATHS = 1 << 7, P_SCORE = 1 << 8, P_PING = 1 << 9, P_CHECK = 1 << 10, P_HOOK = 1 << 11,
    P_HIDDEN = 1 << 12, P_STATS = 1 << 13, P_KEYS = 1 << 14, P_FULL = 1 << 15,
};
// The delta base's players by id, and its projectiles by id (an open-addressed table: at most 48 of them), so each
// entry finds its base in O(1) instead of a scan of the base (32 x 32 / 48 x 48 compares per client per snapshot).
namespace {
struct BaseIndex {
    const PlayerSnap* player[kMaxPlayers] = {};
    static constexpr int kSlots = 256;   // a power of two, well over the 48 projectiles a snapshot carries (any count works)
    int16_t proj[kSlots];
    const std::vector<ProjectileSnap>* projs = nullptr;
    explicit BaseIndex(const Snapshot* base) {
        if (!base) return;
        for (auto& o : base->players) if (o.id < kMaxPlayers && !player[o.id]) player[o.id] = &o;
        projs = &base->projectiles;
        if (projs->size() >= (size_t)kSlots) { projs = nullptr; return; }   // (never: then each lookup scans)
        std::memset(proj, 0xFF, sizeof proj);
        for (size_t k = 0; k < projs->size(); k++) {
            int h = slot((*projs)[k].id);
            while (proj[h] >= 0 && (*projs)[proj[h]].id != (*projs)[k].id) h = (h + 1) & (kSlots - 1);
            if (proj[h] < 0) proj[h] = (int16_t)k;   // (a duplicate id keeps the first, like the scan did)
        }
    }
    static int slot(uint16_t id) { return (int)((id * 40503u) >> 8) & (kSlots - 1); }
    const ProjectileSnap* projectile(const Snapshot* base, uint16_t id) const {
        if (!base) return nullptr;
        if (!projs) { for (auto& o : base->projectiles) if (o.id == id) return &o; return nullptr; }
        for (int h = slot(id); proj[h] >= 0; h = (h + 1) & (kSlots - 1))
            if ((*projs)[proj[h]].id == id) return &(*projs)[proj[h]];
        return nullptr;
    }
};
}

static int16_t q16(float v) { return (int16_t)clampf(std::round(v * 64.0f), -32767, 32767); }
// (equal floats quantize equal: most players and props haven't moved since the base, and each q16 / angQ is a libm
// call, per player per client per snapshot)
static bool samePos(const Vec3& a, const Vec3& b) {
    return (a.x == b.x && a.y == b.y && a.z == b.z) || (q16(a.x) == q16(b.x) && q16(a.y) == q16(b.y) && q16(a.z) == q16(b.z));
}
static bool sameAng(float a, float b) { return a == b || angQ(a) == angQ(b); }

void writeSnapshotWorld(Writer& w, Snapshot& s, const Snapshot* base) {
    // mode state: 0 = same as base, 1 = full copy follows
    uint8_t mb[96], bb[96];
    Writer mw(mb, sizeof mb), bw(bb, sizeof bb);
    writeMode(mw, s.ms);
    if (base) writeMode(bw, base->ms);
    bool modeSame = base && mw.size() == bw.size() && memcmp(mb, bb, mw.size()) == 0;
    w.u8(modeSame ? 0 : 1);
    if (!modeSame) w.bytes(mb, mw.size());

    const BaseIndex bi(base);
    w.u8((uint8_t)s.players.size());
    for (auto& p : s.players) {
        const PlayerSnap* b = p.id < kMaxPlayers ? bi.player[p.id] : nullptr;
        uint16_t m = p.hidden ? P_HIDDEN : 0;
        if (!b || b->hidden != p.hidden) m |= P_FULL | 0x0FFF | P_STATS | P_KEYS;
        else {
            if (b->flags != p.flags) m |= P_FLAGS;
            if (!p.hidden && !samePos(b->pos, p.pos)) m |= P_POS;
            if (!p.hidden && !sameAng(b->yaw, p.yaw)) m |= P_YAW;
            if (!p.hidden && !sameAng(b->pitch, p.pitch)) m |= P_PITCH;
            if (b->health != p.health) m |= P_HEALTH;
            if (b->weapon != p.weapon) m |= P_WEAPON;
            if (b->kills != p.kills) m |= P_KILLS;
            if (b->deaths != p.deaths) m |= P_DEATHS;
            if (b->score != p.score) m |= P_SCORE;
            if (b->dmg != p.dmg || b->obj != p.obj || b->acc != p.acc) m |= P_STATS;
            if (b->ping != p.ping) m |= P_PING;
            if (b->keys != p.keys) m |= P_KEYS;
            if (b->checkpoint != p.checkpoint) m |= P_CHECK;
            if (!p.hidden && (b->hooked != p.hooked || (p.hooked && !samePos(b->hookPoint, p.hookPoint)))) m |= P_HOOK;
        }
        if (p.hidden) m &= ~(P_POS | P_YAW | P_PITCH | P_HOOK | P_KEYS | P_HEALTH | P_WEAPON);   // (out of sight: nor health, weapon)
        w.u8(p.id); w.u16(m);
        if (m & P_FLAGS) w.u8(p.flags);
        if (m & P_POS) w.vecq(p.pos);
        if (m & P_YAW) w.i16(angQ(p.yaw));
        if (m & P_PITCH) w.i16(angQ(p.pitch));
        if (m & P_HEALTH) w.u8(p.health);
        if (m & P_WEAPON) w.u8(p.weapon);
        if (m & P_KILLS) w.i16(p.kills);
        if (m & P_DEATHS) w.i16(p.deaths);
        if (m & P_SCORE) w.i16(p.score);
        if (m & P_STATS) { w.i16(p.dmg); w.i16(p.obj); w.u8(p.acc); }
        if (m & P_PING) w.u16(p.ping);
        if (m & P_KEYS) w.u16(p.keys);
        if (m & P_CHECK) w.u8(p.checkpoint);
        if (m & P_HOOK) { w.u8(p.hooked); if (p.hooked) w.vecq(p.hookPoint); }
    }
    // projectiles: id, bits (1 = new: type and owner follow, 2 = velocity follows, 4 = PROJ_LOB, 8 = PROJ_STUCK,
    // 16 = out of sight: nothing follows, 32 = PROJ_ON_PLAYER: u8 player, i8 x, u8 y, i8 z of its attachOffset follow
    // the position; protocol 38), position always (in sight). Velocity only when it changed by more than
    // 0.5 m/s since what the client has (the client only uses it to extrapolate a few ms); the snapshot we keep as the
    // next base records the velocity the client has. One back in sight is sent as new.
    w.u8((uint8_t)s.projectiles.size());
    for (auto& p : s.projectiles) {
        if (p.hidden) { w.u16(p.id); w.u8(16); continue; }   // (16: out of sight)
        const ProjectileSnap* b = bi.projectile(base, p.id);
        if (b && b->hidden) b = nullptr;
        bool vel = !b || length(p.vel - b->vel) > 0.5f;
        const bool on = (p.flags & PROJ_ON_PLAYER) != 0;
        w.u16(p.id); w.u8((b ? 0 : 1) | (vel ? 2 : 0) | (uint8_t)((p.flags & (PROJ_LOB | PROJ_STUCK)) << 2) | (on ? 32 : 0));
        if (!b) { w.u8(p.type); w.u8(p.owner); }
        w.vecq(p.pos);
        if (on) {   // (quantizeAttachOffset's grid: exact)
            w.u8(p.onPlayer);
            w.i8((int8_t)std::lround(clampf(p.attachOffset.x / kPlayerHalfWidth, -1, 1) * 127));
            w.u8((uint8_t)std::lround(clampf(p.attachOffset.y / kPlayerHeight, 0, 1) * 255));
            w.i8((int8_t)std::lround(clampf(p.attachOffset.z / kPlayerHalfWidth, -1, 1) * 127));
        }
        if (vel) w.vecq(p.vel);
        else p.vel = b->vel;
    }
}

bool readSnapshotWorld(Reader& r, Snapshot& s, const Snapshot* base) {
    if (r.u8()) { if (!readMode(r, s.ms)) return false; }
    else if (base) s.ms = base->ms;
    else return false;
    const BaseIndex bi(base);
    s.players.resize(r.u8());
    static_assert(kMaxPlayers <= 32, "one bit per player id");
    uint32_t seen = 0;   // player ids index fixed kMaxPlayers arrays on the client: in range, and each once
    for (auto& p : s.players) {
        p = PlayerSnap();
        p.id = r.u8();
        if (p.id >= kMaxPlayers || (seen >> p.id & 1)) return false;
        seen |= 1u << p.id;
        uint16_t m = r.u16();
        if (!(m & P_FULL)) {
            const PlayerSnap* b = bi.player[p.id];
            if (!b) return false; // delta against a player the base doesn't have
            p = *b;
        }
        p.hidden = (m & P_HIDDEN) != 0;
        if (m & P_FLAGS) p.flags = r.u8();
        if (m & P_POS) p.pos = r.vecq();
        if (m & P_YAW) p.yaw = angD(r.i16());
        if (m & P_PITCH) p.pitch = angD(r.i16());
        if (m & P_HEALTH) p.health = r.u8();
        if (m & P_WEAPON) p.weapon = r.u8();
        if (m & P_KILLS) p.kills = r.i16();
        if (m & P_DEATHS) p.deaths = r.i16();
        if (m & P_SCORE) p.score = r.i16();
        if (m & P_STATS) { p.dmg = r.i16(); p.obj = r.i16(); p.acc = r.u8(); }
        if (m & P_PING) p.ping = r.u16();
        if (m & P_KEYS) p.keys = r.u16();
        if (m & P_CHECK) p.checkpoint = r.u8();
        if (m & P_HOOK) { p.hooked = r.u8(); if (p.hooked) p.hookPoint = r.vecq(); }
        if (p.hidden) p.hooked = 0;
        if (p.weapon >= WPN_COUNT) return false;
    }
    s.projectiles.resize(r.u8());
    for (auto& p : s.projectiles) {
        p = ProjectileSnap();
        p.id = r.u16();
        uint8_t bits = r.u8();
        if (bits & 16) { p.hidden = true; continue; }   // out of sight: still flying, nothing else known
        p.flags = (uint8_t)((bits >> 2) & (PROJ_LOB | PROJ_STUCK));
        const ProjectileSnap* b = nullptr;
        if (bits & 1) { p.type = r.u8(); p.owner = r.u8(); if (p.owner >= kMaxPlayers) return false; }
        else {
            b = bi.projectile(base, p.id);
            if (!b || b->hidden) return false;
            p.type = b->type; p.owner = b->owner;
        }
        p.pos = r.vecq();
        if (bits & 32) {   // stuck to a player (protocol 38: older demos never set it)
            p.flags |= PROJ_ON_PLAYER;
            p.onPlayer = r.u8();
            if (p.onPlayer >= kMaxPlayers) return false;
            float x = r.i8() / 127.0f, y = r.u8() / 255.0f, z = r.i8() / 127.0f;
            p.attachOffset = {clampf(x, -1, 1) * kPlayerHalfWidth, y * kPlayerHeight, clampf(z, -1, 1) * kPlayerHalfWidth};
        }
        if (bits & 2) p.vel = r.vecq();
        else if (b) p.vel = b->vel;
        else return false;
    }
    return !r.bad();
}

void writeSnapshotEvents(Writer& w, const std::vector<GameEvent>& events) {
    w.u8((uint8_t)events.size());
    for (auto& e : events) {
        w.u32(e.id); w.u8(e.type); w.u8(e.a); w.u8(e.b); w.i16(e.c); w.str(e.text);
    }
}

void writeSnapshotEvents(Writer& w, const GameEvent* const* events, int n) {
    w.u8((uint8_t)n);
    for (int k = 0; k < n; k++) {
        const GameEvent& e = *events[k];
        w.u32(e.id); w.u8(e.type); w.u8(e.a); w.u8(e.b); w.i16(e.c); w.str(e.text);
    }
}

bool parseBracket(const std::string& text, std::vector<std::vector<int>>& rounds, int& round, int& match) {
    rounds.clear();
    round = match = 0;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        std::string line = text.substr(p, e == std::string::npos ? std::string::npos : e - p);
        p = e == std::string::npos ? text.size() : e + 1;
        if (!line.empty() && line[0] == '@') { if (sscanf(line.c_str() + 1, "%d,%d", &round, &match) != 2) round = match = 0; continue; }
        std::vector<int> r;
        for (size_t q = 0; q < line.size();) {
            size_t bar = line.find('|', q);
            if (bar == std::string::npos) bar = line.size();
            int id = atoi(line.substr(q, bar - q).c_str());
            r.push_back(id >= -2 && id < kMaxPlayers ? id : -2);
            q = bar + 1;
        }
        if (rounds.size() >= 8 || r.size() > 2 * (size_t)kMaxPlayers) { rounds.clear(); return false; }
        rounds.push_back(std::move(r));
    }
    round = std::max(0, std::min(round, (int)rounds.size()));
    match = std::max(0, std::min(match, 2 * kMaxPlayers));
    return !rounds.empty();
}

bool readSnapshotEvents(Reader& r, Snapshot& s) {
    s.events.resize(r.u8());
    for (auto& e : s.events) {
        e.id = r.u32(); e.type = r.u8(); e.a = r.u8(); e.b = r.u8(); e.c = r.i16(); e.text = r.str();
    }
    return !r.bad();
}

GameEvent propEvent(const StuckProp& p) {
    GameEvent e;
    e.type = EV_PROP;
    e.a = p.type; e.b = p.player; e.c = p.owner;
    uint8_t buf[16];
    Writer w(buf, sizeof buf);
    if (p.player == PROP_WORLD) w.vec(p.pos);
    else {   // (the grid quantizeAttachOffset rounds to)
        const Vec3 o = quantizeAttachOffset(p.pos);
        w.i8((int8_t)std::lround(o.x / kPlayerHalfWidth * 127)); w.u8((uint8_t)std::lround(o.y / kPlayerHeight * 255));
        w.i8((int8_t)std::lround(o.z / kPlayerHalfWidth * 127));
    }
    w.i16(angQ(std::atan2(p.dir.x, p.dir.z))); w.i16(angQ(std::asin(clampf(p.dir.y, -1, 1))));
    e.text.assign((const char*)buf, (size_t)w.size());
    return e;
}

GameEvent propClearEvent(int player) {
    GameEvent e;
    e.type = EV_PROP;
    e.a = PROP_CLEAR; e.b = (uint8_t)player;
    return e;
}

bool readPropEvent(const GameEvent& e, StuckProp* p) {
    if (e.type != EV_PROP || e.a == PROP_CLEAR || !leavesProp(e.a)) return false;
    const bool world = e.b == PROP_WORLD;
    if ((!world && e.b >= kMaxPlayers) || e.text.size() != (world ? 16u : 7u)) return false;
    Reader r((const uint8_t*)e.text.data(), (int)e.text.size());
    p->type = e.a; p->player = e.b; p->owner = (uint8_t)e.c;
    if (world) {
        p->pos = r.vec();
        if (!(std::fabs(p->pos.x) < 1e5f && std::fabs(p->pos.y) < 1e5f && std::fabs(p->pos.z) < 1e5f)) return false;
    } else {
        float x = r.i8() / 127.0f, y = r.u8() / 255.0f, z = r.i8() / 127.0f;
        p->pos = quantizeAttachOffset({x * kPlayerHalfWidth, y * kPlayerHeight, z * kPlayerHalfWidth});
    }
    const float yaw = angD(r.i16()), pitch = angD(r.i16());
    p->dir = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    return !r.bad();
}
