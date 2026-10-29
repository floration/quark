#pragma once
// Breakable glass and blown-open brick (owner, 2026-10-28): the event that tells every client, and the shards a broken
// pane throws. The server and the clients make the same shards from the event's seed: the server's hurt players
// (kGlassShardDamage / kGlassPieceDamage while they fly), the clients' are drawn.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "gamedefs.h"
#include "map.h"
#include "protocol.h"

// DESTRUCT_GLASS_LIST / DESTRUCT_HOLE_LIST: what a player joining finds already broken (no shards): text = up to 60
// u32 brushes / up to 20 hole blasts (3 x f32), the holes in the order they were blown (each cuts what the last left).
enum : uint8_t { DESTRUCT_RESET = 0, DESTRUCT_GLASS = 1, DESTRUCT_HOLE = 2, DESTRUCT_GLASS_LIST = 3, DESTRUCT_HOLE_LIST = 4 };
constexpr int kGlassListMax = 60, kHoleListMax = 20;
// EV_DESTRUCT: a = the kind, b = the GlassHow, c = who broke it (-1: nobody), text = the u32 brush, at and dir (3 x f32
// each) and the u32 seed. A hole sends only at (where the blast was: Map::punchHole works the rest out).
struct DestructEv {
    uint8_t kind = DESTRUCT_RESET, how = 0;
    int by = -1;
    uint32_t brush = 0, seed = 0;
    Vec3 at, dir;
};
GameEvent destructEvent(const DestructEv& d);
bool readDestructEvent(const GameEvent& e, DestructEv* d);   // the single ones (reset, glass, hole)
std::vector<GameEvent> destructListEvents(const std::vector<uint32_t>& glass, const std::vector<Vec3>& holes);
bool readDestructList(const GameEvent& e, std::vector<uint32_t>* glass, std::vector<Vec3>* holes);   // (appends)

struct GlassShard { Vec3 pos, vel; };
inline uint32_t glassRand(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
inline float glassRandF(uint32_t& s) { return (float)(glassRand(s) & 0xFFFFFFu) / (float)0x1000000; }
inline bool glassCrater(int how) { return how == GLASS_SHOT || how == GLASS_HOOK_BACK; }
inline float glassShardDamage(int how) { return glassCrater(how) ? kGlassShardDamage : kGlassPieceDamage; }
// whoever broke it doesn't get its shards: the dasher and the grappler pulled through take kGlassBreakDamage instead
inline bool glassSparesBreaker(int how) { return how == GLASS_DASH || how == GLASS_HOOK_THROUGH; }
inline int glassThinAxis(const AABB& b) {
    const Vec3 e = b.mx - b.mn;
    return e.x <= e.y && e.x <= e.z ? 0 : e.y <= e.z ? 1 : 2;
}
// The shards of a pane broken `how` at `at` toward `dir`: a shot's crater 10-20 shards on along the shot (or, grappled,
// back at you); a blast, a dash or a pull the whole pane in hundreds of pieces (about 120 a square metre, 100-300),
// away from the blast's side / along the dash.
inline void glassShards(const AABB& pane, int how, const Vec3& at, const Vec3& dir, uint32_t seed, std::vector<GlassShard>& out) {
    out.clear();
    uint32_t s = seed | 1u;
    const int a = glassThinAxis(pane);
    const Vec3 ext = pane.mx - pane.mn;
    auto rnd3 = [&] { return Vec3(glassRandF(s) * 2 - 1, glassRandF(s) * 2 - 1, glassRandF(s) * 2 - 1); };
    if (glassCrater(how)) {
        const int n = 10 + (int)(seed % 11);
        for (int i = 0; i < n; i++) {
            Vec3 p = at + rnd3() * 0.2f;
            p[a] = std::clamp(at[a], pane.mn[a], pane.mx[a]);
            Vec3 v = dir + rnd3() * 0.45f;
            const float l = length(v);
            out.push_back({p, (l > 1e-4f ? v / l : dir) * (5.0f + 5.0f * glassRandF(s))});
        }
        return;
    }
    const float area = std::max(ext.x * ext.y, std::max(ext.y * ext.z, ext.x * ext.z));
    const int n = std::clamp((int)(area * 120.0f), 100, 300);
    Vec3 away;
    if (how == GLASS_SHATTER) away[a] = at[a] < pane.center()[a] ? 1.0f : -1.0f;
    else away = dir;
    for (int i = 0; i < n; i++) {
        const Vec3 p = {pane.mn.x + ext.x * glassRandF(s), pane.mn.y + ext.y * glassRandF(s), pane.mn.z + ext.z * glassRandF(s)};
        Vec3 v = away * (3.0f + 7.0f * glassRandF(s)) + rnd3() * 1.5f;
        if (how == GLASS_SHATTER) { Vec3 r = p - at; r[a] = 0; v += r * 1.5f; }   // (and out from under the blast)
        out.push_back({p, v});
    }
}
