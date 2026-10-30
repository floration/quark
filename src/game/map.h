// Block-based maps: a set of axis-aligned solid boxes plus gameplay markers.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "../common/qmath.h"

enum SpawnTeam { SPAWN_A = 0, SPAWN_B = 1, SPAWN_ANY = 2, SPAWN_RACE = 3, SPAWN_STAGED = 4 };
// Multi point spawns ("spawn att2 ..." / "spawn def0 ..."): team = SPAWN_STAGED + 2 * stage + (defenders ? 1 : 0).
// A stage's spawns are used once that many points have fallen (the highest stage reached so far wins).
constexpr int kMaxSpawnStage = 9;
constexpr int stagedSpawn(int stage, bool defenders) { return SPAWN_STAGED + 2 * stage + (defenders ? 1 : 0); }
// Mode spawn sets (map format 11; owner, 2026-10-30: Mine's koth hill gets its own koth spawns, apart from the bomb
// ones): "spawn koth-a ...", "spawn ffa-any ...": spawns only that mode uses, team = SPAWN_MODESET + 3 * mode + side
// (SPAWN_A, SPAWN_B or SPAWN_ANY). A mode starts players on its own set's side when the map has one (modeSpawnType),
// and no other mode uses them. Not for race (its race spawns are its set) or multi point (its staged spawns are).
constexpr int SPAWN_MODESET = stagedSpawn(kMaxSpawnStage, true) + 1;
constexpr int kSpawnModes = 15;   // MODE_COUNT (static_assert in map.cpp)
constexpr int modeSpawn(int mode, int side) { return SPAWN_MODESET + 3 * mode + side; }
constexpr int kMaxSpawnTeam = modeSpawn(kSpawnModes - 1, SPAWN_ANY);
inline bool isStagedSpawn(int team) { return team >= SPAWN_STAGED && team < SPAWN_MODESET; }
inline int spawnStage(int team) { return (team - SPAWN_STAGED) / 2; }
inline bool spawnDefends(int team) { return ((team - SPAWN_STAGED) & 1) != 0; }
inline bool isModeSpawn(int team) { return team >= SPAWN_MODESET && team <= kMaxSpawnTeam; }
inline int spawnSetMode(int team) { return (team - SPAWN_MODESET) / 3; }
// a spawn's side: SPAWN_A, SPAWN_B or SPAWN_ANY for a mode set's, its team for the rest
inline int spawnSide(int team) { return isModeSpawn(team) ? (team - SPAWN_MODESET) % 3 : team; }
// the modes that can have their own set (race and multi point have theirs already)
bool modeSetAllowed(int mode);
// A spawn's team as written in a map file ("a", "att2", "koth-b"), and back (-1: not a team)
std::string spawnTeamWord(int team);
int spawnTeamFromWord(const std::string& w);

constexpr int kMaxCuts = 6;   // a cut box's planes

struct Brush {
    AABB box;
    int material = 0; // (12-19: ice, sand, wood, plastic, grass, rubber, steel, diamond plate: solid, see kMatIce) 0 = base, 1 = accent, 2 = wallbang (dark grey: solid, but hitscan goes through it), 3 = metal, 4 = glow (emissive trim), 5 = far (distant scenery outside the walls: flat-shaded, no lightmap), 6 = water, 7 = lava (unlit surfaces of deadly pits: not solid, you fall through and die), 8 = trim (accent-coloured decoration: lit, but not solid), 9 = clip (invisible: blocks players only, not shots, hooks or sight; tops off the outer walls so nobody leaves the map), 10 = nograpple (solid like base, but the grapple can't latch onto it: it hits it and lets go), 11 = acid (glowing green pools: not solid, you wade in; touching it costs 10% of your max health, then 10% a second while you stay in)
    bool generated = false; // made from another object (an editor ladder's rails and rungs): not saved, not editable
    int group = -1;         // the map group it's a part of (generated from groups[group]), -1 for none: seams (outlines) only between groups
    // Shape (map format 5): a box, or the solid under a sloped top inside the box (its slope is whatever the box gives).
    // In the brush's own frame (u, h, v: see slopeFrame) the shape stands on its base, the box's low-h face:
    //   wedge: one top edge kept; it rises from the base to the far face toward dir (0 +u, 1 -u, 2 +v, 3 -v, as stairs).
    //   corner: one top corner kept, a pyramid over the base with its apex above corner dir (0 -u-v, 1 +u-v, 2 +u+v, 3 -u+v).
    //   tetra: a corner with only three base corners (owner, 2026-10-28: "1 0 0 0 / 1 1 1 0"): a three-sided pyramid on
    //   the base's triangle at corner dir, its apex above that corner; one sloped face, over the base's diagonal.
    // Both are heightfields there: solid where 0 <= h < height * shapeHeight(u, v) (0..1).
    // up turns that frame: 0 standing on the floor (u x, h y, v z: the default), 1 hanging from the ceiling (the same,
    // mirrored upside down), 2 / 3 on its side rising toward +x / -x (u y, h x, v z), 4 / 5 rising toward +z / -z (u x,
    // h z, v y); an odd one is the even one mirrored along h.
    // cut (owner, 2026-10-29: "cut box (any angle)"): the box less what lies past up to kMaxCuts planes at any angle, each
    // a x + b y + c z <= d over the box's own 0..1 fractions (x = (world x - box.mn.x) / its width, ...), so it scales
    // with its box like the others; dir and up stay 0. Wedges, corners and tetras are cut boxes of one or two planes;
    // with several, any convex piece fits a box (a dome's face: its sloped top and the cuts along its ridges).
    uint8_t shape = 0;      // BRUSH_BOX, BRUSH_WEDGE, BRUSH_CORNER, BRUSH_TETRA, BRUSH_CUT
    uint8_t dir = 0;
    uint8_t up = 0;
    uint8_t ncut = 0;       // a cut box's planes
    // a teleporter's (map format 10: "tp <link> <face>" after the box, written only for one): the link (teleporters with
    // the same one are linked) and the face it lets players and projectiles out of (kFace*)
    uint8_t link = 1, exitFace = 2;
    float cut[kMaxCuts][4] = {};
    // In play only (never saved): broken glass, or brick a hole was blown in (its pieces are new generated brushes after
    // it). Out of the grid: nothing collides with it or sees it. Map::resetDestruction brings it back.
    bool gone = false;
    int origin = -1;   // in play only: a hole's piece, the wall it was cut from (its index; -1 not a piece)
};
enum { BRUSH_BOX = 0, BRUSH_WEDGE = 1, BRUSH_CORNER = 2, BRUSH_TETRA = 3, BRUSH_CUT = 4 };
constexpr int kSlopeUps = 6;
// A slope's name in files ("wedge", "corner", "tetra", "cut") and its letter in edit ops and group files ('w', 'c', 't',
// 'x'). A cut's planes follow its count in place of dir + 4 * up: "cut 2 a b c d a b c d" (cutText / readCuts).
inline const char* slopeWord(int shape) { return shape == BRUSH_WEDGE ? "wedge" : shape == BRUSH_TETRA ? "tetra" : shape == BRUSH_CUT ? "cut" : "corner"; }
inline char slopeLetter(int shape) { return shape == BRUSH_WEDGE ? 'w' : shape == BRUSH_TETRA ? 't' : shape == BRUSH_CUT ? 'x' : 'c'; }
inline int slopeFromWord(const std::string& w) {   // -1: not a slope's
    return w == "wedge" || w == "w" ? BRUSH_WEDGE : w == "corner" || w == "c" ? BRUSH_CORNER : w == "tetra" || w == "t" ? BRUSH_TETRA
         : w == "cut" || w == "x" ? BRUSH_CUT : -1;
}
// A cut's count and planes as written after its word or letter: " n a b c d ..." (shortest exact numbers)
std::string cutText(const Brush& b);
// Reads a cut's count and planes (after its word or letter) into b; false on a bad count, number or a plane that cuts
// nothing in any direction (a = b = c = 0).
template <class S> bool readCuts(S& in, Brush& b) {
    int n = -1;
    if (!(in >> n) || n < 1 || n > kMaxCuts) return false;
    b.ncut = (uint8_t)n;
    for (int k = 0; k < n; k++) {
        for (int j = 0; j < 4; j++) if (!(in >> b.cut[k][j]) || !std::isfinite(b.cut[k][j]) || std::fabs(b.cut[k][j]) > 1e6f) return false;
        if (std::fabs(b.cut[k][0]) + std::fabs(b.cut[k][1]) + std::fabs(b.cut[k][2]) < 1e-9f) return false;
    }
    b.dir = b.up = 0;
    return true;
}
// A teleporter's link and exit face after its box ("tp <link 0-255> <face 0-5>"; the word itself already read)
template <class S> bool readTp(S& in, Brush& b) {
    int l = -1, f = -1;
    if (!(in >> l >> f) || l < 0 || l > 255 || f < 0 || f > 5) return false;
    b.link = (uint8_t)l; b.exitFace = (uint8_t)f;
    return true;
}
// A slope's own frame: the world axes of u, h, v, and whether h is mirrored (h = box.mn + box.mx - world).
struct SlopeFrame { int u, h, v; bool flip; };
inline SlopeFrame slopeFrame(const Brush& b) {
    const bool f = (b.up & 1) != 0;
    switch (b.up >> 1) { case 1: return {1, 0, 2, f}; case 2: return {0, 2, 1, f}; default: return {0, 1, 2, f}; }
}
// (u, h, v) as (x, y, z): an even turn of the axes, or an odd one (a mirror): a polygon's corners go round the other way
inline bool slopeFrameMirrors(const SlopeFrame& f) { return (f.u != 0) != f.flip; }
inline Vec3 slopeToLocal(const Brush& b, const SlopeFrame& f, const Vec3& p) {
    return {p[f.u], f.flip ? b.box.mn[f.h] + b.box.mx[f.h] - p[f.h] : p[f.h], p[f.v]};
}
inline Vec3 slopeToWorld(const Brush& b, const SlopeFrame& f, const Vec3& l) {
    Vec3 p; p[f.u] = l.x; p[f.h] = f.flip ? b.box.mn[f.h] + b.box.mx[f.h] - l.y : l.y; p[f.v] = l.z;
    return p;
}
inline Vec3 slopeDirToWorld(const SlopeFrame& f, const Vec3& l) { Vec3 p; p[f.u] = l.x; p[f.h] = f.flip ? -l.y : l.y; p[f.v] = l.z; return p; }
// (per axis, so an empty box (mn > mx somewhere) stays empty: the mirror swaps h's ends)
inline AABB slopeBoxToLocal(const Brush& b, const SlopeFrame& f, const AABB& a) {
    AABB r{{a.mn[f.u], a.mn[f.h], a.mn[f.v]}, {a.mx[f.u], a.mx[f.h], a.mx[f.v]}};
    if (f.flip) { const float s = b.box.mn[f.h] + b.box.mx[f.h]; r.mn.y = s - a.mx[f.h]; r.mx.y = s - a.mn[f.h]; }
    return r;
}
inline AABB slopeBoxToWorld(const Brush& b, const SlopeFrame& f, const AABB& l) {
    AABB r;
    r.mn[f.u] = l.mn.x; r.mx[f.u] = l.mx.x; r.mn[f.v] = l.mn.z; r.mx[f.v] = l.mx.z;
    if (f.flip) { const float s = b.box.mn[f.h] + b.box.mx[f.h]; r.mn[f.h] = s - l.mx.y; r.mx[f.h] = s - l.mn.y; }
    else { r.mn[f.h] = l.mn.y; r.mx[f.h] = l.mx.y; }
    return r;
}
// The brush in its own frame (up 0): what the frame functions below work on.
inline Brush slopeLocal(const Brush& b) {
    const SlopeFrame f = slopeFrame(b);
    Brush l = b;
    l.box = slopeBoxToLocal(b, f, b.box);
    l.up = 0;
    return l;
}

// ---- cut boxes (world space: a cut's dir and up are 0)
// A cut's planes in the world: n . p <= d inside (n unit, pointing out of it); returns how many.
inline int cutPlanes(const Brush& b, Vec3* n, float* d) {
    const Vec3 sz = b.box.mx - b.box.mn;
    int k = 0;
    for (int i = 0; i < b.ncut && i < kMaxCuts; i++) {
        const float* c = b.cut[i];
        Vec3 nn(c[0] / std::max(sz.x, 1e-6f), c[1] / std::max(sz.y, 1e-6f), c[2] / std::max(sz.z, 1e-6f));
        const float dd = c[3] + dot(nn, b.box.mn);
        const float len = std::sqrt(dot(nn, nn));
        if (len < 1e-12f) continue;
        n[k] = nn * (1.0f / len); d[k] = dd / len; k++;
    }
    return k;
}
// Clips convex polygon p (np corners, at most 24 after) to n . x <= d; returns the corners left.
inline int clipPolygon(Vec3* p, int np, const Vec3& n, float d) {
    Vec3 o[24]; int no = 0;
    for (int k = 0; k < np && no < 22; k++) {
        const Vec3 &a = p[k], &b = p[(k + 1) % np];
        const float da = dot(n, a) - d, db = dot(n, b) - d;
        if (da <= 0) o[no++] = a;
        if ((da <= 0) != (db <= 0)) o[no++] = a + (b - a) * (da / (da - db));
    }
    int m = 0;   // (corners closer than 1e-6 to the one before are one corner)
    for (int k = 0; k < no; k++) {
        const Vec3 e = o[k] - (m ? p[m - 1] : o[no - 1]);
        if (m == 0 || dot(e, e) > 1e-12f) p[m++] = o[k];
    }
    if (m > 1) { const Vec3 e = p[m - 1] - p[0]; if (dot(e, e) <= 1e-12f) m--; }
    return m;
}
// The faces of a cut box clipped to `clip` (a box): fn(pts, np, n) for each convex polygon (corners in order round it,
// n its outward normal; up to 6 box faces and a face per plane). Faces narrower than 1e-5 are left out.
template <class F> void cutFaces(const Brush& b, const AABB& clip, F fn) {
    const AABB a{vmax(b.box.mn, clip.mn), vmin(b.box.mx, clip.mx)};
    if (a.mn.x > a.mx.x || a.mn.y > a.mx.y || a.mn.z > a.mx.z) return;
    Vec3 pn[kMaxCuts]; float pd[kMaxCuts];
    const int np = cutPlanes(b, pn, pd);
    auto area = [](const Vec3* p, int n) { Vec3 s; for (int k = 1; k + 1 < n; k++) s += cross(p[k] - p[0], p[k + 1] - p[0]); return length(s) * 0.5f; };
    for (int axis = 0; axis < 3; axis++)
        for (int side = 0; side < 2; side++) {   // the box's faces, each cut by every plane
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            const float w = side ? a.mx[axis] : a.mn[axis];
            Vec3 p[24];
            const float us[4] = {a.mn[u], a.mx[u], a.mx[u], a.mn[u]}, vs[4] = {a.mn[v], a.mn[v], a.mx[v], a.mx[v]};
            for (int k = 0; k < 4; k++) { p[k][axis] = w; p[k][u] = us[side ? k : 3 - k]; p[k][v] = vs[side ? k : 3 - k]; }
            int n = 4;
            for (int j = 0; j < np && n >= 3; j++) n = clipPolygon(p, n, pn[j], pd[j]);
            Vec3 nn; nn[axis] = side ? 1.0f : -1.0f;
            if (n >= 3 && area(p, n) > 1e-10f) fn(p, n, nn);
        }
    const float big = length(a.mx - a.mn) + 1.0f;
    for (int i = 0; i < np; i++) {   // each plane's face: a square on it round the box, cut to the box and the other planes
        const Vec3 c = (a.mn + a.mx) * 0.5f;
        const Vec3 o = c - pn[i] * (dot(pn[i], c) - pd[i]);
        const Vec3 t1 = normalize(std::fabs(pn[i].y) < 0.9f ? cross(pn[i], Vec3(0, 1, 0)) : cross(pn[i], Vec3(1, 0, 0))), t2 = cross(pn[i], t1);
        Vec3 p[24] = {o - t1 * big - t2 * big, o + t1 * big - t2 * big, o + t1 * big + t2 * big, o - t1 * big + t2 * big};
        if (dot(cross(p[1] - p[0], p[2] - p[0]), pn[i]) < 0) std::swap(p[1], p[3]);
        int n = 4;
        for (int axis = 0; axis < 3 && n >= 3; axis++) {
            Vec3 e; e[axis] = 1; n = clipPolygon(p, n, e, a.mx[axis]);
            if (n >= 3) n = clipPolygon(p, n, e * -1.0f, -a.mn[axis]);
        }
        for (int j = 0; j < np && n >= 3; j++) if (j != i) n = clipPolygon(p, n, pn[j], pd[j]);
        if (n >= 3 && area(p, n) > 1e-10f) fn(p, n, pn[i]);
    }
}
// The bounds of a cut box's solid inside `clip`; empty (mn > mx) when nothing of it is, or only a sliver thinner than
// 1e-5 along some axis (touching isn't overlapping).
inline AABB cutBounds(const Brush& b, const AABB& clip) {
    AABB r{Vec3(1e30f, 1e30f, 1e30f), Vec3(-1e30f, -1e30f, -1e30f)};
    cutFaces(b, clip, [&](const Vec3* p, int n, const Vec3&) { for (int k = 0; k < n; k++) { r.mn = vmin(r.mn, p[k]); r.mx = vmax(r.mx, p[k]); } });
    if (r.mx.x - r.mn.x < 1e-5f || r.mx.y - r.mn.y < 1e-5f || r.mx.z - r.mn.z < 1e-5f) return AABB{Vec3(1e30f, 1e30f, 1e30f), Vec3(-1e30f, -1e30f, -1e30f)};
    return r;
}
// The solid's top over the column at (x, z) (box.mn.y when the column has none of it)
inline float cutTopAt(const Brush& b, float x, float z) {
    Vec3 pn[kMaxCuts]; float pd[kMaxCuts];
    const int np = cutPlanes(b, pn, pd);
    float lo = b.box.mn.y, hi = b.box.mx.y;
    for (int k = 0; k < np; k++) {
        const float r = pd[k] - pn[k].x * x - pn[k].z * z;
        if (std::fabs(pn[k].y) < 1e-7f) { if (r < 0) return b.box.mn.y; continue; }
        if (pn[k].y > 0) hi = std::min(hi, r / pn[k].y); else lo = std::max(lo, r / pn[k].y);
    }
    return hi > lo ? hi : b.box.mn.y;
}

// In the brush's own frame (the world for up 0; slopeLocal for the rest): x u, y h, z v.
// A slope's 0..1 fractions toward its high side along x and z (1 where it's high; a wedge rising along x ignores z: 1).
inline void slopeFractions(const Brush& b, float x, float z, float& gx, float& gz) {
    const Vec3 sz = b.box.mx - b.box.mn;
    const float fx = (x - b.box.mn.x) / std::max(sz.x, 1e-6f), fz = (z - b.box.mn.z) / std::max(sz.z, 1e-6f);
    if (b.shape == BRUSH_WEDGE) {
        gx = b.dir == 0 ? fx : b.dir == 1 ? 1 - fx : 1;
        gz = b.dir == 2 ? fz : b.dir == 3 ? 1 - fz : 1;
    } else {
        gx = (b.dir == 1 || b.dir == 2) ? fx : 1 - fx;
        gz = (b.dir == 2 || b.dir == 3) ? fz : 1 - fz;
    }
}
// Own frame: the solid's top at (x, z), inside the box's footprint.
inline float brushTopAt(const Brush& b, float x, float z) {
    if (b.shape == BRUSH_BOX) return b.box.mx.y;
    if (b.shape == BRUSH_CUT) return cutTopAt(b, x, z);
    float gx, gz;
    slopeFractions(b, x, z, gx, gz);
    const float h = std::min(1.0f, std::max(0.0f, b.shape == BRUSH_TETRA ? gx + gz - 1.0f : std::min(gx, gz)));
    return b.box.mn.y + (b.box.mx.y - b.box.mn.y) * h;
}
// Own frame: the slope's box less what's below fractions fx along x and fz along z (gx > fx, gz > fz: slopeFractions).
inline AABB slopeTrim(const Brush& b, float fx, float fz) {
    AABB r = b.box;
    const float sx = b.box.mx.x - b.box.mn.x, sz = b.box.mx.z - b.box.mn.z;
    const bool wedge = b.shape == BRUSH_WEDGE;
    const bool hiX = wedge ? b.dir == 0 : (b.dir == 1 || b.dir == 2), loX = wedge ? b.dir == 1 : !(b.dir == 1 || b.dir == 2);
    const bool hiZ = wedge ? b.dir == 2 : (b.dir == 2 || b.dir == 3), loZ = wedge ? b.dir == 3 : !(b.dir == 2 || b.dir == 3);
    if (hiX) r.mn.x = b.box.mn.x + fx * sx; else if (loX) r.mx.x = b.box.mx.x - fx * sx;
    if (hiZ) r.mn.z = b.box.mn.z + fz * sz; else if (loZ) r.mx.z = b.box.mx.z - fz * sz;
    return r;
}
// Own frame: the highest fractions toward the high side (slopeFractions) anywhere in rectangle [x0, x1] x [z0, z1]
// (clamped to the footprint): the ends nearest the high corner.
inline void slopeFractionsMax(const Brush& b, float x0, float x1, float z0, float z1, float& gx, float& gz) {
    const bool wedge = b.shape == BRUSH_WEDGE;
    const bool hiX = wedge ? b.dir == 0 : (b.dir == 1 || b.dir == 2), hiZ = wedge ? b.dir == 2 : (b.dir == 2 || b.dir == 3);
    const float x = std::min(std::max(hiX ? x1 : x0, b.box.mn.x), b.box.mx.x), z = std::min(std::max(hiZ ? z1 : z0, b.box.mn.z), b.box.mx.z);
    slopeFractions(b, x, z, gx, gz);
}
// Own frame: what of a brush is solid above height y: for a slope, the part of its box where the top is above y, which
// is a box too (the heightfield falls away from the high side along x and z), up to box.mx.y. Empty (mn > mx) when
// nothing is. A box is itself. Collision against a player's box uses this at the box's feet: every point of the
// player's footprint over that part has solid above its feet. A tetra's is a triangle: this is its bounding box (the
// collision functions below cut it to what a box's footprint actually meets).
inline AABB slopeSolidAbove(const Brush& b, float y) {
    if (b.shape == BRUSH_BOX) return b.box;
    if (b.shape == BRUSH_CUT) { AABB c = b.box; c.mn.y = std::max(c.mn.y, y); return cutBounds(b, c); }
    AABB r = b.box;
    const float h = b.box.mx.y - b.box.mn.y;
    float f = h > 1e-6f ? (y - b.box.mn.y) / h : 1.0f;
    if (f >= 1.0f) { r.mn.y = r.mx.y + 1; return r; }   // (empty)
    f = std::max(0.0f, f);
    if (b.shape == BRUSH_TETRA) return slopeTrim(b, f, f);
    const float sx = b.box.mx.x - b.box.mn.x, sz = b.box.mx.z - b.box.mn.z;
    const bool wedge = b.shape == BRUSH_WEDGE;
    const bool hiX = wedge ? b.dir == 0 : (b.dir == 1 || b.dir == 2), loX = wedge ? b.dir == 1 : !(b.dir == 1 || b.dir == 2);
    const bool hiZ = wedge ? b.dir == 2 : (b.dir == 2 || b.dir == 3), loZ = wedge ? b.dir == 3 : !(b.dir == 2 || b.dir == 3);
    if (hiX) r.mn.x = b.box.mn.x + f * sx; else if (loX) r.mx.x = b.box.mx.x - f * sx;
    if (hiZ) r.mn.z = b.box.mn.z + f * sz; else if (loZ) r.mx.z = b.box.mx.z - f * sz;
    return r;
}
// Own frame: the highest the solid reaches over the part of rectangle [x0, x1] x [z0, z1] inside its footprint.
inline float brushTopOver(const Brush& b, float x0, float x1, float z0, float z1) {
    if (b.shape == BRUSH_BOX) return b.box.mx.y;
    if (b.shape == BRUSH_CUT) {
        const AABB r = cutBounds(b, AABB{Vec3(x0, b.box.mn.y, z0), Vec3(x1, b.box.mx.y, z1)});
        return r.mn.y > r.mx.y ? b.box.mn.y : r.mx.y;
    }
    x0 = std::max(x0, b.box.mn.x); x1 = std::min(x1, b.box.mx.x); z0 = std::max(z0, b.box.mn.z); z1 = std::min(z1, b.box.mx.z);
    const bool wedge = b.shape == BRUSH_WEDGE;
    const bool hiX = wedge ? b.dir == 0 : (b.dir == 1 || b.dir == 2), hiZ = wedge ? b.dir == 2 : (b.dir == 2 || b.dir == 3);
    return brushTopAt(b, hiX ? x1 : x0, hiZ ? z1 : z0);
}
// Own frame: the steepness of a slope's surface: its rise over its run (tan of the angle), per plane; the steepest.
inline float slopeSteepness(const Brush& b) {
    if (b.shape == BRUSH_BOX) return 0;
    const Vec3 sz = b.box.mx - b.box.mn;
    if (b.shape == BRUSH_WEDGE) return sz.y / std::max(b.dir < 2 ? sz.x : sz.z, 1e-6f);
    if (b.shape == BRUSH_TETRA) return sz.y * std::sqrt(1.0f / std::max(sz.x * sz.x, 1e-12f) + 1.0f / std::max(sz.z * sz.z, 1e-12f));
    if (b.shape == BRUSH_CUT) {   // (its planes facing up; a plane facing sideways or down is a wall, not a floor)
        Vec3 pn[kMaxCuts]; float pd[kMaxCuts];
        const int np = cutPlanes(b, pn, pd);
        float s = 0;
        for (int k = 0; k < np; k++) if (pn[k].y > 1e-6f) s = std::max(s, std::sqrt(pn[k].x * pn[k].x + pn[k].z * pn[k].z) / pn[k].y);
        return s;
    }
    return sz.y / std::max(std::min(sz.x, sz.z), 1e-6f);
}

// World (any up): what of a brush is solid above height y, as a box. Exact standing on the floor (up 0); otherwise the
// part of its box above y (a little more than the solid: the bots' blockers only need it not to be less).
inline AABB brushSolidAbove(const Brush& b, float y) {
    if (b.shape == BRUSH_BOX || b.up == 0) return slopeSolidAbove(b, y);
    AABB r = b.box;
    r.mn.y = std::max(r.mn.y, y);
    return r;
}
// World: the part of a brush that stops `box` moving along `axis` by `delta` (sign is all that matters), as a box: a
// box itself; a slope, in its own frame, sideways the part solid above the box's base side, toward its base the whole
// box, onto its top the top under the box's footprint (the slope's highest point there: a box's faces are flat).
inline AABB brushObstacle(const Brush& b, const AABB& box, int axis, float delta) {
    if (b.shape == BRUSH_BOX) return b.box;
    if (b.shape == BRUSH_CUT) {   // (exact: what of the solid lies in the box's slab across the move; it's convex)
        AABB c = box;
        c.mn[axis] = b.box.mn[axis]; c.mx[axis] = b.box.mx[axis];
        (void)delta;
        return cutBounds(b, c);
    }
    const SlopeFrame f = slopeFrame(b);
    const Brush l = slopeLocal(b);
    const AABB lb = slopeBoxToLocal(b, f, box);
    AABB r;
    if (l.shape == BRUSH_TETRA) {   // (its solid over the box's footprint: how far the triangle reaches along the other axis)
        float gx, gz;
        slopeFractionsMax(l, lb.mn.x, lb.mx.x, lb.mn.z, lb.mx.z, gx, gz);
        const bool over = lb.mx.x > l.box.mn.x && lb.mn.x < l.box.mx.x && lb.mx.z > l.box.mn.z && lb.mn.z < l.box.mx.z;
        const float hh = l.box.mx.y - l.box.mn.y;
        const float fy = hh > 1e-6f ? std::max(0.0f, (lb.mn.y - l.box.mn.y) / hh) : 1.0f;   // (the box's feet, as a fraction)
        r = l.box;
        // sideways: nothing above its feet anywhere along the other axis's span, empty; otherwise the triangle's reach
        if (axis == f.u) r = fy >= 1.0f || gz <= fy ? AABB{l.box.mx, l.box.mn} : slopeTrim(l, std::max(0.0f, 1.0f + fy - gz), fy);
        else if (axis == f.v) r = fy >= 1.0f || gx <= fy ? AABB{l.box.mx, l.box.mn} : slopeTrim(l, fy, std::max(0.0f, 1.0f + fy - gx));
        else if (!over || gx + gz <= 1.0f) r = AABB{l.box.mx, l.box.mn};   // (not over the triangle: empty)
        else if ((delta > 0) == f.flip) r.mx.y = brushTopOver(l, lb.mn.x, lb.mx.x, lb.mn.z, lb.mx.z);
        return slopeBoxToWorld(b, f, r);
    }
    if (axis != f.h) r = slopeSolidAbove(l, lb.mn.y);
    else if ((delta > 0) != f.flip) r = l.box;   // (toward its base: the flat face)
    else {
        r = l.box;
        if (lb.mx.x <= l.box.mn.x || lb.mn.x >= l.box.mx.x || lb.mx.z <= l.box.mn.z || lb.mn.z >= l.box.mx.z) r.mn.x = r.mx.x + 1;   // (not over it: empty)
        else r.mx.y = brushTopOver(l, lb.mn.x, lb.mx.x, lb.mn.z, lb.mx.z);
    }
    return slopeBoxToWorld(b, f, r);
}
// Does `box` overlap the brush's solid (open sets: touching isn't overlapping)?
inline bool brushOverlaps(const Brush& b, const AABB& box) {
    if (!box.overlaps(b.box)) return false;
    if (b.shape == BRUSH_BOX) return true;
    if (b.shape == BRUSH_CUT) { const AABB r = cutBounds(b, box); return r.mn.x <= r.mx.x; }
    const SlopeFrame f = slopeFrame(b);
    const AABB lb = slopeBoxToLocal(b, f, box);
    const Brush l = slopeLocal(b);
    if (l.shape == BRUSH_TETRA) {   // (the footprint's point nearest the apex: solid above the box's feet there?)
        float gx, gz;
        slopeFractionsMax(l, lb.mn.x, lb.mx.x, lb.mn.z, lb.mx.z, gx, gz);
        return lb.mn.y < l.box.mn.y + (l.box.mx.y - l.box.mn.y) * (gx + gz - 1.0f);
    }
    return lb.overlaps(slopeSolidAbove(l, lb.mn.y));
}
inline bool brushContains(const Brush& b, const Vec3& p) {   // strictly inside
    if (!(p.x > b.box.mn.x && p.x < b.box.mx.x && p.y > b.box.mn.y && p.y < b.box.mx.y && p.z > b.box.mn.z && p.z < b.box.mx.z)) return false;
    if (b.shape == BRUSH_BOX) return true;
    if (b.shape == BRUSH_CUT) {
        Vec3 pn[kMaxCuts]; float pd[kMaxCuts];
        const int np = cutPlanes(b, pn, pd);
        for (int k = 0; k < np; k++) if (dot(pn[k], p) >= pd[k]) return false;
        return true;
    }
    const SlopeFrame f = slopeFrame(b);
    const Vec3 l = slopeToLocal(b, f, p);
    return l.y < brushTopAt(slopeLocal(b), l.x, l.z);
}
// World: a slope's sloped planes (n . p <= d inside; n points out of it), 1 for a wedge or a tetra, 2 for a corner, a
// cut's own (up to kMaxCuts); returns how many. (n and d hold kMaxCuts.)
inline int slopePlanes(const Brush& b, Vec3* n, float* d) {
    if (b.shape == BRUSH_BOX) return 0;
    if (b.shape == BRUSH_CUT) return cutPlanes(b, n, d);
    const SlopeFrame fr = slopeFrame(b);
    const Brush l = slopeLocal(b);
    const Vec3 sz = l.box.mx - l.box.mn;
    const float h = sz.y;
    int k = 0;
    auto plane = [&](int axis, bool hiPositive) {   // top = mn.y + h * g, g = (p[axis] - lo) / s toward +, or (hi - p) / s toward -
        const float s = std::max(axis == 0 ? sz.x : sz.z, 1e-6f);
        Vec3 nn(0, 1, 0);
        // y - mn.y - h * g <= 0
        float dd;
        if (hiPositive) { nn[axis] = -h / s; dd = l.box.mn.y - h * l.box.mn[axis] / s; }
        else { nn[axis] = h / s; dd = l.box.mn.y + h * l.box.mx[axis] / s; }
        const float len = std::sqrt(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z);
        // to the world: n . (M p + c) <= d, M the frame's turn (its own inverse's transpose), c the mirror's offset
        n[k] = slopeDirToWorld(fr, nn * (1.0f / len));
        d[k] = (dd - (fr.flip ? nn.y * (b.box.mn[fr.h] + b.box.mx[fr.h]) : 0.0f)) / len;
        k++;
    };
    if (b.shape == BRUSH_WEDGE) plane(b.dir < 2 ? 0 : 2, b.dir == 0 || b.dir == 2);
    else if (b.shape == BRUSH_TETRA) {   // top = mn.y + h * (gx + gz - 1): one plane across both axes
        const float sx = std::max(sz.x, 1e-6f), sv = std::max(sz.z, 1e-6f);
        const bool px = b.dir == 1 || b.dir == 2, pz = b.dir == 2 || b.dir == 3;
        // gx = px ? (x - mn.x) / sx : (mx.x - x) / sx, the same for z; y - h * gx - h * gz <= mn.y - h
        Vec3 nn(px ? -h / sx : h / sx, 1, pz ? -h / sv : h / sv);
        float dd = l.box.mn.y - h + h * (px ? -l.box.mn.x / sx : l.box.mx.x / sx) + h * (pz ? -l.box.mn.z / sv : l.box.mx.z / sv);
        const float len = std::sqrt(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z);
        n[0] = slopeDirToWorld(fr, nn * (1.0f / len));
        d[0] = (dd - (fr.flip ? nn.y * (b.box.mn[fr.h] + b.box.mx[fr.h]) : 0.0f)) / len;
        k = 1;
    } else { plane(0, b.dir == 1 || b.dir == 2); plane(2, b.dir == 2 || b.dir == 3); }
    return k;
}
// World: the sloped plane at p (a corner's: the one whose height is the lower there, the top being the min of the two).
inline int slopePlaneAt(const Brush& b, const Vec3& p, Vec3& n, float& d) {
    Vec3 pn[kMaxCuts]; float pd[kMaxCuts];
    const int np = slopePlanes(b, pn, pd);
    if (np == 0) return 0;
    int k = 0;
    for (int j = 1; j < np; j++) if (dot(pn[j], p) - pd[j] > dot(pn[k], p) - pd[k]) k = j;
    n = pn[k]; d = pd[k];
    return 1;
}
// World: a slope's faces as convex polygons (corners in order round each, 3 or 4; a cut's, up to 24), each with its
// outward normal: fn(pts, n, normal). Sides that are whole rectangles come out as polygons too; the low edge has none.
// (The light bake builds its own.)
template <class F> void slopePolygons(const Brush& world, F fn) {
    if (world.shape == BRUSH_CUT) { cutFaces(world, world.box, fn); return; }
    const SlopeFrame fr = slopeFrame(world);
    const bool rev = slopeFrameMirrors(fr);
    const Brush br = slopeLocal(world);
    auto out = [&](const Vec3* lp, int np, const Vec3& ln) {
        Vec3 p[4];
        for (int k = 0; k < np; k++) p[k] = slopeToWorld(world, fr, lp[rev ? np - 1 - k : k]);
        fn(p, np, slopeDirToWorld(fr, ln));
    };
    const AABB& a = br.box;
    const float y0 = a.mn.y;
    const Vec3 c[4] = {{a.mn.x, y0, a.mn.z}, {a.mx.x, y0, a.mn.z}, {a.mx.x, y0, a.mx.z}, {a.mn.x, y0, a.mx.z}};
    Vec3 t[4];
    for (int k = 0; k < 4; k++) t[k] = {c[k].x, brushTopAt(br, c[k].x, c[k].z), c[k].z};
    for (int k = 0; k < 4; k++) {   // sides 0..3 face -z, +x, +z, -x
        const int k1 = (k + 1) % 4;
        const bool low0 = t[k].y < y0 + 1e-4f, low1 = t[k1].y < y0 + 1e-4f;
        Vec3 n; n[k % 2 == 0 ? 2 : 0] = (k == 0 || k == 3) ? -1.0f : 1.0f;
        if (low0 && low1) continue;
        if (low0) { const Vec3 p[3] = {c[k], c[k1], t[k1]}; out(p, 3, n); }
        else if (low1) { const Vec3 p[3] = {c[k], c[k1], t[k]}; out(p, 3, n); }
        else { const Vec3 p[4] = {c[k], c[k1], t[k1], t[k]}; out(p, 4, n); }
    }
    if (br.shape == BRUSH_TETRA) {   // its base: the triangle at the apex corner; its top: the one sloped face over it
        const int hi = br.dir, a1 = (hi + 1) % 4, a3 = (hi + 3) % 4;
        { const Vec3 p[3] = {c[hi], c[a3], c[a1]}; out(p, 3, Vec3(0, -1, 0)); }
        Vec3 pn[kMaxCuts]; float pd[kMaxCuts];
        slopePlanes(br, pn, pd);
        const Vec3 p[3] = {t[hi], t[a1], t[a3]};
        out(p, 3, pn[0]);
        return;
    }
    { const Vec3 p[4] = {c[0], c[3], c[2], c[1]}; out(p, 4, Vec3(0, -1, 0)); }
    if (br.shape == BRUSH_WEDGE) {
        Vec3 pn[kMaxCuts]; float pd[kMaxCuts];
        slopePlanes(br, pn, pd);
        out(t, 4, pn[0]);
    } else {
        const int hi = br.dir, lo = (hi + 2) % 4;
        for (int side = 0; side < 2; side++) {
            const Vec3 p[3] = {t[lo], t[side == 0 ? (lo + 1) % 4 : (lo + 3) % 4], t[hi]};
            Vec3 n = normalize(cross(p[1] - p[0], p[2] - p[0]));
            out(p, 3, n.y < 0 ? n * -1.0f : n);
        }
    }
}
// A slope turned or mirrored: `to` (its box already the turned box) gets the dir and up whose solid is `from`'s moved
// by pt (a 90 degree turn or a mirror: any of them maps the 24 dir / up pairs onto each other). Tried on sample points
// that sit on no slope's surface (fractions unlike on every axis, mirrored too); false if none fits.
template <class P> bool slopeTurned(const Brush& from, Brush& to, P pt) {
    static const float fx[3] = {0.11f, 0.34f, 0.58f}, fy[3] = {0.19f, 0.46f, 0.71f}, fz[3] = {0.27f, 0.52f, 0.86f};
    const Vec3 s = from.box.mx - from.box.mn;
    for (int up = 0; up < kSlopeUps; up++)
        for (int dir = 0; dir < 4; dir++) {
            to.up = (uint8_t)up; to.dir = (uint8_t)dir;
            bool same = true;
            for (int i = 0; i < 27 && same; i++) {
                const Vec3 p = from.box.mn + Vec3(s.x * fx[i % 3], s.y * fy[i / 3 % 3], s.z * fz[i / 9]);
                same = brushContains(from, p) == brushContains(to, pt(p));
            }
            if (same) return true;
        }
    return false;
}
// A cut box turned or mirrored: `to` (its box already the turned box) gets the planes whose solid is `from`'s moved by pt
// (any affine map: each plane's three points and a point inside it are moved, and the plane put back through them).
template <class P> void cutTurned(const Brush& from, Brush& to, P pt) {
    Vec3 pn[kMaxCuts]; float pd[kMaxCuts];
    const int np = cutPlanes(from, pn, pd);
    const Vec3 sz = to.box.mx - to.box.mn;
    auto frac = [&](const Vec3& w) { return Vec3((w.x - to.box.mn.x) / std::max(sz.x, 1e-6f), (w.y - to.box.mn.y) / std::max(sz.y, 1e-6f), (w.z - to.box.mn.z) / std::max(sz.z, 1e-6f)); };
    to.ncut = (uint8_t)np;
    for (int k = 0; k < np; k++) {
        const Vec3 o = pn[k] * pd[k];
        const Vec3 t1 = normalize(std::fabs(pn[k].y) < 0.9f ? cross(pn[k], Vec3(0, 1, 0)) : cross(pn[k], Vec3(1, 0, 0))), t2 = cross(pn[k], t1);
        const Vec3 a = frac(pt(o)), b = frac(pt(o + t1)), c = frac(pt(o + t2)), in = frac(pt(o - pn[k]));
        Vec3 n = cross(b - a, c - a);
        float d = dot(n, a);
        if (dot(n, in) > d) { n = n * -1.0f; d = -d; }
        const float s = 1.0f / std::max(std::max(std::fabs(n.x), std::fabs(n.y)), std::max(std::fabs(n.z), 1e-12f));
        to.cut[k][0] = n.x * s; to.cut[k][1] = n.y * s; to.cut[k][2] = n.z * s; to.cut[k][3] = d * s;
    }
}
// Ray vs a brush's solid: entry distance in [0, maxT] or -1; *normal the face it enters (axis faces as rayAABB's axis).
inline float rayBrush(const Vec3& o, const Vec3& dir, const Vec3& invDir, const Brush& b, float maxT, Vec3* normal = nullptr) {
    int axis = -1;
    float t = rayAABB(o, invDir, b.box, maxT, &axis);
    if (t < 0) return -1.0f;
    if (b.shape == BRUSH_BOX) {
        if (normal) { *normal = Vec3(); if (axis >= 0) (*normal)[axis] = dir[axis] > 0 ? -1.0f : 1.0f; }
        return t;
    }
    // the box's slab interval, then cut by the sloped planes
    float tmin = 0, tmax = maxT;
    Vec3 nIn;
    for (int i = 0; i < 3; i++) {
        float t1 = (b.box.mn[i] - o[i]) * invDir[i], t2 = (b.box.mx[i] - o[i]) * invDir[i];
        float s = -1;
        if (t1 > t2) { std::swap(t1, t2); s = 1; }
        if (t1 > tmin) { tmin = t1; nIn = Vec3(); nIn[i] = s; }
        if (t2 < tmax) tmax = t2;
    }
    Vec3 pn[kMaxCuts]; float pd[kMaxCuts];
    const int np = slopePlanes(b, pn, pd);
    for (int k = 0; k < np; k++) {
        const float den = dot(pn[k], dir), num = pd[k] - dot(pn[k], o);
        if (std::fabs(den) < 1e-9f) { if (num < 0) return -1.0f; continue; }
        const float tt = num / den;
        if (den < 0) { if (tt > tmin) { tmin = tt; nIn = pn[k]; } }
        else if (tt < tmax) tmax = tt;
    }
    if (tmin > tmax) return -1.0f;
    if (normal) *normal = nIn;
    return tmin;
}

// Brush materials are 0..kMapMaterials-1 everywhere: a map's boxes, groups and group parts, in files, edits and the editor.
// 12-19 (owner, 2026-10-28): solid like base, each its own look made in the world shader (no textures); ice is slippery
// underfoot, rubber bounces you off it.
// 21 glass (see-through) and 22-35 (owner, 2026-10-28, from the surface ideas): solid, own looks; asphalt grips (faster,
// quicker to brake), mud holds you back.
// 36-38 (owner, 2026-10-29: "separate destructible vs non destructible glass, brick, cobblestone, etc so it doesn't cost
// nearly as much"): breakable brick and cobblestone (thin ones blow open: Map::punchHole) and breakable glass
// (Map::breakGlass). They look like 20, 24 and 21 (lookMaterial), which never break: a plain wall pays for no hole test
// in the shader, no faces kept behind it in the bake, no pane scans per shot, and FIX MAP can merge it and hide behind it.
// 39-138 (owner, 2026-10-29: "yes to everything" on the 100 brick / concrete / tile / stone looks): solid, each its own
// look (Renderer's blockLook): 30 bricks, 20 concretes, 25 tiles, 25 stone walls; plain, never break.
// 139-471 (owner, 2026-10-29: "where's the other several hundred materials"): the picks of the four earlier rounds,
// in their order: 77 of the first 100 (nature, buildings, industrial, sci-fi, decor), 100 nature, 86 industrial,
// 70 wasteland. Solid, plain, each its own look (client/surface_looks.h has every look from 39 on).
// 472-496 (owner, 2026-10-29, the special blocks round: all 25 liquids): not solid, you wade through like water; each
// changes how you move in it or hurts / heals / kills (kLiquids). Looks: surface_looks.h, flowing (uTime).
// 497-536 (owner, 2026-10-29: "add some LARGE patterns 2-4x current size per piece of the surface, especially stone
// bricks, cobbles, wood"): another material's look with its pieces 2-4 times as big (kLarge: the world shader divides
// the face's plane by the scale); solid and plain like the one it scales.
constexpr int kMatLiquids = 472, kLiquidCount = 25;
constexpr int kMatLarge = 497, kLargeCount = 40;
// 537-561 (owner, 2026-10-29, the special blocks round: all 25 semi-transparent): see-through, drawn blended with their
// looks after the world (their look sets alpha). kSemi says what each stops: players, shots (hitscan, projectiles, the
// grapple), sight (bots, who's sent to whom). The glass ones stop players and shots like plain glass; a force field only
// players, a laser barrier only shots, a smoke wall only sight; a water curtain, a hologram, soap film and the scanner
// beam nothing. Not lit by the bake (flat sun and sky) and cast no shadow.
constexpr int kMatSemi = kMatLarge + kLargeCount, kSemiCount = 25;
struct SemiDef { bool players, shots, sight; };
inline constexpr SemiDef kSemi[kSemiCount] = {
    {true, true, false},    // blue tinted glass
    {true, true, false},    // smoked glass (shapes, not detail: bots still see through)
    {true, true, false},    // amber glass
    {true, true, true},     // frosted glass: light through, no view
    {true, true, false},    // reeded glass
    {true, true, false},    // stained glass
    {true, true, false},    // glass block
    {true, true, false},    // scratched plexiglass
    {true, true, false},    // ice wall
    {true, true, false},    // quartz crystal
    {true, true, false},    // green gel
    {true, false, false},   // hex force field: players only
    {false, true, false},   // red laser barrier: shots only
    {false, false, false},  // hologram panel
    {true, false, false},   // chain-link mesh: shots through the holes
    {true, false, false},   // cargo net
    {false, false, false},  // soap film
    {false, false, false},  // water curtain: step through
    {false, false, true},   // smoke wall: sight only
    {true, true, false},    // rain window
    {true, true, false},    // prism glass
    {true, true, false},    // jelly cube
    {true, true, false},    // ghost glass
    {false, false, false},  // scanner beam
    {true, true, false},    // bottle green glass
};
// 562-585 (owner, 2026-10-29, the special blocks round: 24 breakable, all but the clay pot wall): solid until they've
// taken their hp (kBreaks: a shot counts its damage x bullet, a blast its damage at that distance x blast; 0: that
// can't hurt it). Some go on contact instead: BREAK_TOUCH (any touch), BREAK_DASH (dashing into it), BREAK_WEIGHT
// (standing on it for kWeightBreakS). The server decides (EV_DESTRUCT as a pane: the brush is gone; joiners get the
// list); clients throw debris in its colour. Back every round. Not baked: drawn by their looks from a mesh of their own
// (Renderer::uploadBlocks), lit by full sun and sky, rebuilt when one breaks.
constexpr int kMatBreak = kMatSemi + kSemiCount, kBreakCount = 24;
enum : uint8_t { BREAK_HITS = 0, BREAK_TOUCH, BREAK_DASH, BREAK_WEIGHT };
struct BreakDef { float hp, bullet, blast; uint8_t touch; float r, g, b; };
inline constexpr BreakDef kBreaks[kBreakCount] = {
    {80, 1, 3, BREAK_HITS, 0.52f, 0.36f, 0.20f},   // wooden crate
    {40, 1, 2, BREAK_DASH, 0.71f, 0.58f, 0.36f},   // plywood board-up: a dash breaks right through
    {25, 1, 3, BREAK_DASH, 0.79f, 0.79f, 0.78f},   // drywall: blasts tear it
    {400, 0.5, 1.5, BREAK_HITS, 0.51f, 0.52f, 0.51f},   // cinder block wall: takes a lot
    {1, 1, 1, BREAK_TOUCH, 0.79f, 0.76f, 0.72f},   // paper screen: tears from anything
    {60, 1, 2, BREAK_HITS, 0.49f, 0.45f, 0.22f},   // bamboo fence
    {10, 1, 1, BREAK_TOUCH, 0.64f, 0.49f, 0.29f},   // cardboard boxes: crush under anything
    {20, 1, 2, BREAK_DASH, 0.65f, 0.65f, 0.65f},   // picket fence
    {30, 1, 3, BREAK_HITS, 0.65f, 0.51f, 0.31f},   // chipboard: rockets shatter it
    {50, 1, 3, BREAK_HITS, 0.43f, 0.38f, 0.38f},   // rusted sheet: blasts fold it away
    {120, 0, 2.5, BREAK_HITS, 0.62f, 0.62f, 0.60f},   // thin concrete panel: holds up to bullets
    {60, 1, 2, BREAK_HITS, 0.61f, 0.31f, 0.80f},   // crystal cluster
    {200, 1, 2, BREAK_HITS, 0.73f, 0.63f, 0.41f},   // sandstone slab
    {1, 1, 1, BREAK_TOUCH, 0.45f, 0.46f, 0.49f},   // sugar glass: breaks when you touch it
    {100, 0, 3, BREAK_HITS, 0.75f, 0.66f, 0.30f},   // hay bales: blasts only
    {30, 1, 1, BREAK_WEIGHT, 0.37f, 0.34f, 0.31f},   // rotten planks: give way under you
    {120, 1, 2, BREAK_HITS, 0.81f, 0.81f, 0.80f},   // ceramic tile wall
    {12, 1, 2, BREAK_HITS, 0.73f, 0.74f, 0.77f},   // mirror: a couple of shots
    {80, 1, 2, BREAK_HITS, 0.38f, 0.25f, 0.15f},   // barricade
    {200, 0.5, 3, BREAK_HITS, 0.75f, 0.82f, 0.86f},   // frozen waterfall: blasts melt it open
    {20, 1, 2, BREAK_HITS, 0.45f, 0.48f, 0.49f},   // stained glass window
    {300, 0.25, 2, BREAK_HITS, 0.09f, 0.09f, 0.10f},   // tyre stack: shrugs off bullets
    {50, 1, 3, BREAK_HITS, 0.71f, 0.67f, 0.62f},   // lath and plaster
    {10, 1, 2, BREAK_HITS, 0.49f, 0.49f, 0.36f},   // glass bottles
};
constexpr float kWeightBreakS = 0.3f;
// 586-594 (owner, 2026-10-29, the special blocks round: 9 feature blocks). Floors: the healing tile gives back
// kHealTilePerSec of your max health a second while you stand on it, the spike floor takes kSpikePerSec (as acid: pmove
// counts it, the server takes it). Fields you pass through (not solid, drawn see-through): low gravity (x
// kLowGravField), slow-mo (projectiles in it fly at kSlowMoRate), the checkpoint gate (race: one more checkpoint after
// the map's own, in brush order: Map::raceCheckpoints). The gravity flip tile turns gravity upside down above it
// (kGravFlipHeight up from its top): you fall up and stand on the ceiling. The light block glows and lights the room
// round it in the bake (Map::lightBlocks). The shield wall stops shots, rockets and grapples, not players or sight.
// The crumble platform shakes when you stand on it and falls kCrumbleS later, back kCrumbleBackS after that (server).
// The teleporter (owner, 2026-10-29: "a value where if 2 objects have the same value they are linked ... a set face for
// where it places the player/projectiles"): solid; a player or projectile that touches it comes out of the next
// teleporter with the same link (Brush::link, in brush order, round to the first) on that one's exit face
// (Brush::exitFace, kFace*), just outside it, at the same speed, all of it out along that face.
constexpr int kMatFeat = kMatBreak + kBreakCount, kFeatCount = 10;
constexpr int kMatHealTile = kMatFeat, kMatSpikes = kMatFeat + 1, kMatLowGrav = kMatFeat + 2, kMatSlowMo = kMatFeat + 3,
              kMatGravFlip = kMatFeat + 4, kMatGate = kMatFeat + 5, kMatLightBlock = kMatFeat + 6, kMatShield = kMatFeat + 7,
              kMatCrumble = kMatFeat + 8, kMatTeleport = kMatFeat + 9;
enum : uint8_t { kFacePX = 0, kFaceNX, kFacePY, kFaceNY, kFacePZ, kFaceNZ, kFaces };
constexpr float kHealTilePerSec = 0.10f, kSpikePerSec = 0.20f, kLowGravField = 0.5f, kSlowMoRate = 0.5f, kGravFlipHeight = 64.0f;
constexpr float kCrumbleS = 1.0f, kCrumbleBackS = 5.0f;
constexpr int kMapMaterials = kMatFeat + kFeatCount;
constexpr int kMatBlocks = 39;   // the first of the 100
constexpr int kMatNoGrapple = 10;
constexpr int kMatAcid = 11;
constexpr int kMatIce = 12, kMatSand = 13, kMatWood = 14, kMatPlastic = 15, kMatGrass = 16, kMatRubber = 17, kMatSteel = 18, kMatDiamond = 19;
constexpr int kMatBrick = 20;   // classic red brick (owner, 2026-10-28)
constexpr int kMatGlass = 21, kMatAsphalt = 22, kMatMud = 23, kMatCobble = 24, kMatConcrete = 25, kMatTile = 26, kMatMarble = 27,
              kMatCorrugated = 28, kMatHazard = 29, kMatSnow = 30, kMatStone = 31, kMatCrystal = 32, kMatCircuit = 33, kMatRust = 34,
              kMatTerracotta = 35;
constexpr int kMatBrickBreak = 36, kMatGlassBreak = 37, kMatCobbleBreak = 38;
// the look (the world shader's material, the editor's colour) of a material: a breakable one looks like its plain one
inline int lookMaterial(int m) { return m == kMatBrickBreak ? kMatBrick : m == kMatGlassBreak ? kMatGlass : m == kMatCobbleBreak ? kMatCobble : m; }
inline bool glassMaterial(int m) { return m == kMatGlass || m == kMatGlassBreak; }   // see-through, plain or breakable
inline bool semiMaterial(int m) { return m >= kMatSemi && m < kMatSemi + kSemiCount; }   // the see-through looks (kSemi)
inline bool breakMaterial(int m) { return m >= kMatBreak && m < kMatBreak + kBreakCount; }   // the breakable blocks (kBreaks)
inline bool fieldMaterial(int m) { return m == kMatLowGrav || m == kMatSlowMo || m == kMatGate; }   // feature volumes you pass through
// drawn in the see-through pass (their looks have alpha): the see-through looks, the fields and the shield wall
inline bool seeThroughDrawn(int m) { return semiMaterial(m) || fieldMaterial(m) || m == kMatShield; }
// drawn from the blocks' own mesh (they can go in play without a rebake): the breakable blocks and the crumble platform
inline bool blockDrawn(int m) { return breakMaterial(m) || m == kMatCrumble; }
// brick and cobblestone no thicker than this (its thinnest side) can be blown open by a rocket or a sticky (Map::punchHole)
constexpr float kBreakableThick = 0.125f;
inline bool holeMaterial(int m) { return m == kMatBrickBreak || m == kMatCobbleBreak; }
constexpr float kCrumble = 0.8f;   // blown away past this share of its face: the rest of the wall falls too
constexpr int kMaxHoles = 512;     // per map at once (the world shader's table); past it, blasts leave walls as they are
constexpr float kHoleSize = 2.5f;   // a direct hit's hole, metres across (owner, 2026-10-28)
// a hole's cells (u along the wall, v up it; a floor: x and z): the world shader's bricks and stones, rows offset half a cell
inline void holeCell(int m, float& w, float& h) { if (lookMaterial(m) == kMatCobble) { w = 0.16f; h = 0.14f; } else { w = 0.21f; h = 0.075f; } }
inline int holeUAxis(int axis) { return axis == 0 ? 2 : 0; }
inline int holeVAxis(int axis) { return axis == 1 ? 2 : 1; }
// whether a hole of half size r centred (u, v) breaks out cell (row, col): a squarish circle (|x|^4 + |y|^4 < 1) with
// each cell's own nudge, so its edge is broken bricks. The world shader (renderer.cpp) computes the same.
inline bool holeTakes(float u, float v, float r, float cw, float ch, int row, int col) {
    const float x = ((col + 0.5f - 0.5f * (float)(row & 1)) * cw - u) / r, y = ((row + 0.5f) * ch - v) / r;
    const float s = std::sqrt(std::sqrt(x * x * x * x + y * y * y * y));
    uint32_t k = (uint32_t)row * 73856093u ^ (uint32_t)col * 19349663u;
    k ^= k >> 13; k *= 0x5bd1e995u; k ^= k >> 15;
    return s + ((float)(k & 255u) / 255.0f - 0.5f) * 0.3f < 1.0f;
}
inline bool liquidMaterial(int m) { return m >= kMatLiquids && m < kMatLiquids + kLiquidCount; }
// How each liquid moves you while your hull touches it (pmove, every 1 ms step, so prediction matches the server).
// speed / jump: the run and jump; grip: the floor's friction under it (and the push you get from it); gravity: its
// share in the liquid (sink fast > 1, float < 1); drag: per second, all of your velocity (thick ones); current: m/s the
// flow carries you along +x; dmg / heal: share of max health a second (counted like acid, taken by the server);
// kills: like lava; bounce: a fall into it springs you back up.
struct LiquidDef { float speed, jump, grip, gravity, drag, current, dmg, heal; bool kills, bounce; };
inline constexpr LiquidDef kLiquids[kLiquidCount] = {
    {0.90f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false},    // clear lagoon
    {0.70f, 0.8f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, false, false},    // swamp water: slow
    {1.00f, 1.0f, 0.08f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false},   // oil slick: no grip
    {0.35f, 0.4f, 1.0f, 1.0f, 4.0f, 0.0f, 0.0f, 0.0f, false, false},    // tar pit: very slow, a weak jump
    {0.50f, 0.6f, 1.0f, 1.0f, 2.0f, 0.0f, 0.0f, 0.0f, false, false},    // honey: sticky
    {0.80f, 1.0f, 1.0f, 1.6f, 0.0f, 0.0f, 0.0f, 0.0f, false, false},    // mercury: heavy, you sink fast
    {1.00f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, false},     // molten gold: kills
    {1.00f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, false},     // magma river: kills
    {0.85f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.05f, 0.0f, false, false},   // toxic sludge: 5% a second
    {0.90f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false},    // blood pool
    {0.85f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.05f, false, false},   // hot spring: heals 5% a second
    {0.30f, 0.3f, 1.0f, 0.5f, 5.0f, 0.0f, 0.0f, 0.0f, false, false},    // quicksand: you barely move, sink slowly
    {0.70f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false},    // ice slush: cold, slow
    {0.90f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.20f, 0.0f, false, false},   // plasma pool: 20% a second
    {0.85f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false},    // ink (hides you: you're under its surface)
    {0.90f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false},    // glowing tide
    {0.85f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false},    // sewage
    {0.40f, 0.5f, 1.0f, 1.0f, 3.0f, 0.0f, 0.0f, 0.0f, false, false},    // wet cement: very slow
    {0.90f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, true},     // pink slime: bouncy
    {1.30f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false},    // coolant: faster
    {0.90f, 1.0f, 1.0f, 1.0f, 0.0f, 12.0f, 0.0f, 0.0f, false, false},   // white rapids: the current carries you
    {0.85f, 1.0f, 1.0f, 0.6f, 0.0f, 0.0f, 0.0f, 0.0f, false, false},    // deep ocean: floaty
    {1.00f, 1.8f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false},    // fizzing soda: big jumps out
    {1.00f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, false},     // liquid void: kills
    {0.60f, 0.7f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false},    // mud bath: slow
};
struct LargeDef { int base; float scale; };
inline constexpr LargeDef kLarge[kLargeCount] = {
    {14, 2},   // wide planks
    {14, 4},   // huge planks
    {20, 2},   // large brick
    {20, 4},   // huge brick
    {24, 2},   // large cobbles
    {24, 4},   // huge cobbles
    {26, 2},   // large tile
    {26, 4},   // huge tile
    {31, 2},   // large stone
    {31, 4},   // huge stone
    {39, 3},   // large london stock
    {40, 3},   // large red engineering brick
    {51, 3},   // large herringbone brick
    {52, 2},   // large basket-weave brick
    {89, 3},   // large white square tile
    {91, 3},   // large checkerboard tile
    {92, 3},   // large subway tile
    {96, 3},   // large hex tile, terracotta
    {98, 3},   // large herringbone tile
    {105, 2},   // large slate tile
    {106, 2},   // large travertine tile
    {114, 2},   // large limestone ashlar
    {115, 2},   // large sandstone ashlar
    {117, 2},   // large granite blocks
    {120, 2},   // large random ashlar
    {123, 3},   // large dry-stone wall
    {127, 2},   // large castle stone
    {130, 2},   // large polygonal stone
    {131, 3},   // large cobbled setts
    {132, 3},   // large diagonal setts
    {133, 2},   // large flagstone paving
    {135, 3},   // large river stone wall
    {138, 2},   // large mossy stone blocks
    {167, 2},   // large basket-weave parquet
    {177, 2},   // large log cabin
    {199, 2},   // large scaffold boards
    {274, 2},   // large frosted planks
    {454, 2},   // large crate wood
    {128, 2},   // large temple stone
    {395, 2},   // large kerb stones
};
inline bool lavaMaterial(int m) { return m == 7 || (liquidMaterial(m) && kLiquids[m - kMatLiquids].kills); }   // touch it and die
// solid to players, shots and sight (not far scenery, water, lava, trim, acid, the liquids; clip is solid to players only)
inline bool solidMaterial(int m) {   // in the collision grid (a see-through one: when it stops anything)
    if (semiMaterial(m)) { const SemiDef& d = kSemi[m - kMatSemi]; return d.players || d.shots || d.sight; }
    return m < 5 || m == kMatNoGrapple || (m >= kMatIce && m < kMapMaterials && !liquidMaterial(m) && !fieldMaterial(m));
}
// what a brush in the grid stops: players (collision), shots (raycast: hitscan, projectiles, the grapple), sight
inline bool stopsPlayers(int m) { return m != kMatShield && (!semiMaterial(m) || kSemi[m - kMatSemi].players); }
inline bool stopsShots(int m) { return m != 9 && (!semiMaterial(m) || kSemi[m - kMatSemi].shots); }
// Hitscan goes through it at the weapon's wallbang damage: the wallbang material, and any brush 1/8 m thin or less on
// some axis (owner, 2026-10-30: "any 1/8m surface should have the wallbang property": coats, panels, thin trims)
constexpr float kWallbangThin = 0.125f + 1e-3f;
inline bool wallbangBrush(const Brush& b) {
    return b.material == 2 || (stopsShots(b.material) && std::min(std::min(b.box.mx.x - b.box.mn.x, b.box.mx.y - b.box.mn.y), b.box.mx.z - b.box.mn.z) <= kWallbangThin);
}
inline bool stopsSight(int m) { return m != 9 && m != kMatShield && !glassMaterial(m) && (!semiMaterial(m) || kSemi[m - kMatSemi].sight); }
// lit by the bake (the shader skips the lightmap for glow, far, water, lava, acid; clip and glass aren't baked: glass is
// drawn see-through each frame, so a broken pane can go)
inline bool litMaterial(int m) { return m < 4 || m == 8 || m == kMatNoGrapple || (m >= kMatIce && m < kMapMaterials && !glassMaterial(m) && !seeThroughDrawn(m) && !blockDrawn(m)); }
inline bool bakedMaterial(int m) { return m != 9 && !glassMaterial(m) && !seeThroughDrawn(m) && !blockDrawn(m); }   // in the static world mesh (see-through, breakable: meshes of their own)
// Editor clean-ups (owner, 2026-10-29), on the editable brushes (generated ones only hide others). box[i] is brush i's
// box after the fix, del[i] whether it goes. FIX MAP: same-material plain boxes that together make one box become one
// (and one inside another of its material goes), then a brush wholly inside solid opaque boxes goes (glass, water, clip,
// wallbang, trim and thin breakable brick / cobblestone don't hide anything: you see through them, shoot through them
// or blow them open). Glass and breakable brick are never merged (a bigger pane or wall breaks differently).
// FIX GAPS: a gap under 1/8 m between two plain boxes facing each other is closed by stretching the side that's off the
// 1/8 m grid to the one on it (both off: both to the grid line between them, if there is one); on-grid gaps are meant.
struct MapFix { std::vector<AABB> box; std::vector<bool> del; int merged = 0, hidden = 0, gaps = 0; };
struct Map;
MapFix mapFixRedundant(const Map& m);
MapFix mapFixGaps(const Map& m);
inline bool validMapMaterial(int m) { return m >= 0 && m < kMapMaterials; }
// a box line's keyword per material in map files ("box" is base)
inline constexpr const char* kMapMaterialWords[] = {"box", "accent", "wallbang", "metal", "glow", "far", "water", "lava", "trim", "clip", "nograpple", "acid",
                                                    "ice", "sand", "wood", "plastic", "grass", "rubber", "steel", "diamondplate", "brick",
                                                    "glass", "asphalt", "mud", "cobblestone", "concrete", "tile", "marble", "corrugated", "hazard",
                                                    "snow", "stone", "crystal", "circuit", "rust", "terracotta",
                                                    "breakablebrick", "breakableglass", "breakablecobblestone",
                                                    "londonstock",
                                                    "redengineeringbrick",
                                                    "blueengineeringbrick",
                                                    "orangewirecut",
                                                    "clinkerbrick",
                                                    "whitewashedbrick",
                                                    "blackbrick",
                                                    "creambrick",
                                                    "flemishbond",
                                                    "englishbond",
                                                    "stackbond",
                                                    "soldiercourse",
                                                    "herringbonebrick",
                                                    "basketweavebrick",
                                                    "romanbrick",
                                                    "tudorbrick",
                                                    "glazedbrick",
                                                    "whiteglazedbrick",
                                                    "mossybrick",
                                                    "efflorescentbrick",
                                                    "reclaimedbrick",
                                                    "brownrusticbrick",
                                                    "pinkbrick",
                                                    "recessedjoints",
                                                    "flushpointedbrick",
                                                    "burntbrick",
                                                    "polychromebrick",
                                                    "diaperbrick",
                                                    "paintedredbrick",
                                                    "firebrick",
                                                    "freshgreyconcrete",
                                                    "darkconcrete",
                                                    "whiteconcrete",
                                                    "tintedredconcrete",
                                                    "exposedaggregate",
                                                    "broomfinished",
                                                    "stampedstonepattern",
                                                    "concretepavers",
                                                    "gridscoredslab",
                                                    "ribbedconcrete",
                                                    "bushhammered",
                                                    "fibrecementboard",
                                                    "castinplacewalls",
                                                    "weatheredconcrete",
                                                    "breezeblockscreen",
                                                    "splitfaceblock",
                                                    "groundfaceblock",
                                                    "concreterooftiles",
                                                    "lightweightfoamblock",
                                                    "paintedconcretestripes",
                                                    "whitesquaretile",
                                                    "blacksquaretile",
                                                    "checkerboardtile",
                                                    "subwaytile",
                                                    "greensubwaytile",
                                                    "largeformatporcelain",
                                                    "hextilewhite",
                                                    "hextileterracotta",
                                                    "diagonaltile",
                                                    "herringbonetile",
                                                    "fishscaletile",
                                                    "pennyroundtile",
                                                    "bluepooltile",
                                                    "moroccanzellige",
                                                    "encaustictile",
                                                    "quarrytile",
                                                    "slatetile",
                                                    "travertinetile",
                                                    "mosaicspeckle",
                                                    "bathroommint",
                                                    "gymrubbertile",
                                                    "hospitaltile",
                                                    "victorianborder",
                                                    "crackleglazetile",
                                                    "mirrortile",
                                                    "limestoneashlar",
                                                    "sandstoneashlar",
                                                    "redsandstone",
                                                    "graniteblocks",
                                                    "pinkgranite",
                                                    "blackbasaltblocks",
                                                    "randomashlar",
                                                    "rusticrandomashlar",
                                                    "rubblewall",
                                                    "drystonewall",
                                                    "flintandbrick",
                                                    "rusticatedblocks",
                                                    "vermiculatedstone",
                                                    "castlestone",
                                                    "templestone",
                                                    "marbleblocks",
                                                    "polygonalstone",
                                                    "cobbledsetts",
                                                    "diagonalsetts",
                                                    "flagstonepaving",
                                                    "slatecladding",
                                                    "riverstonewall",
                                                    "gabbroblocks",
                                                    "tufablocks",
                                                    "mossystoneblocks",
                                                    "dirt",
                                                    "gravel",
                                                    "graniterock",
                                                    "sandstonestrata",
                                                    "slate",
                                                    "basaltcolumns",
                                                    "mossystone",
                                                    "crackedearth",
                                                    "redclay",
                                                    "obsidian",
                                                    "riverpebbles",
                                                    "treebark",
                                                    "hedge",
                                                    "forestfloor",
                                                    "coral",
                                                    "limestone",
                                                    "saltflat",
                                                    "swampslime",
                                                    "frost",
                                                    "tundra",
                                                    "lichenrock",
                                                    "amber",
                                                    "jade",
                                                    "bone",
                                                    "honeycomb",
                                                    "stucco",
                                                    "stripedwallpaper",
                                                    "painteddrywall",
                                                    "basketweaveparquet",
                                                    "wainscotpanelling",
                                                    "terrazzo",
                                                    "officecarpettiles",
                                                    "linoleum",
                                                    "paintedbrick",
                                                    "cinderblock",
                                                    "roofshingles",
                                                    "standingseamroof",
                                                    "thatch",
                                                    "logcabin",
                                                    "bamboo",
                                                    "tatami",
                                                    "shojiscreen",
                                                    "hexfloortiles",
                                                    "sidewalkslabs",
                                                    "polishedgranite",
                                                    "cork",
                                                    "perforatedmetal",
                                                    "rivetedplates",
                                                    "chippedpaint",
                                                    "galvanizedsteel",
                                                    "copperandpatina",
                                                    "brass",
                                                    "gold",
                                                    "chrome",
                                                    "aluminiumtreadplate",
                                                    "carbonfibre",
                                                    "coinrubberfloor",
                                                    "shippingcontainer",
                                                    "oilstainedconcrete",
                                                    "chainlinkfence",
                                                    "scaffoldboards",
                                                    "neonmaze",
                                                    "forcefield",
                                                    "serverrack",
                                                    "datarain",
                                                    "alienbiomass",
                                                    "starfield",
                                                    "scifipanels",
                                                    "heatvents",
                                                    "pooltablefelt",
                                                    "quiltedleather",
                                                    "sportscourt",
                                                    "astroturf",
                                                    "cardboard",
                                                    "newspaper",
                                                    "camouflage",
                                                    "cooledmagma",
                                                    "shale",
                                                    "quartzite",
                                                    "travertine",
                                                    "pumice",
                                                    "flint",
                                                    "redcanyonrock",
                                                    "chalk",
                                                    "gneiss",
                                                    "malachite",
                                                    "geode",
                                                    "wetcaverock",
                                                    "boulderfield",
                                                    "scree",
                                                    "jasper",
                                                    "micaschist",
                                                    "petrifiedwood",
                                                    "desertvarnish",
                                                    "conglomerate",
                                                    "glacierpolishedgranite",
                                                    "opal",
                                                    "rootyloam",
                                                    "peat",
                                                    "quicksand",
                                                    "riverbankclay",
                                                    "rutteddirtroad",
                                                    "volcanicash",
                                                    "mycelium",
                                                    "ochreearth",
                                                    "reddesertsoil",
                                                    "tilledfield",
                                                    "termitemound",
                                                    "seabirdcliff",
                                                    "frozenmud",
                                                    "autumnleaves",
                                                    "blacksand",
                                                    "clover",
                                                    "flowermeadow",
                                                    "ivyonbrick",
                                                    "mosscarpet",
                                                    "ferns",
                                                    "pinebark",
                                                    "birchbark",
                                                    "palmtrunk",
                                                    "logendgrain",
                                                    "lilypads",
                                                    "reeds",
                                                    "cactus",
                                                    "hay",
                                                    "vinesonstone",
                                                    "mushroomgills",
                                                    "toadstool",
                                                    "kelp",
                                                    "jungleleaves",
                                                    "wheat",
                                                    "pineboughs",
                                                    "glacierice",
                                                    "windpackedsnow",
                                                    "icicles",
                                                    "frostedplanks",
                                                    "rainwetpaving",
                                                    "lakeice",
                                                    "seafoam",
                                                    "waterfall",
                                                    "puddledground",
                                                    "hail",
                                                    "dewygrass",
                                                    "snowflakefrost",
                                                    "tidalmudflats",
                                                    "cloud",
                                                    "icecave",
                                                    "reddunes",
                                                    "desertpavement",
                                                    "sulfurcrust",
                                                    "ropylavarock",
                                                    "scoria",
                                                    "fumarolecrust",
                                                    "rocksalt",
                                                    "badlands",
                                                    "desertcrust",
                                                    "bandediron",
                                                    "tuff",
                                                    "shellbeach",
                                                    "hardpan",
                                                    "emberbed",
                                                    "crossbeddedsandstone",
                                                    "barnacles",
                                                    "musselbed",
                                                    "motherofpearl",
                                                    "seaglass",
                                                    "flowstone",
                                                    "glowwormcave",
                                                    "glowingalgae",
                                                    "quartzcrystals",
                                                    "cobwebs",
                                                    "beetlecarvedwood",
                                                    "snakeskin",
                                                    "fur",
                                                    "turtleshell",
                                                    "feathers",
                                                    "waspnestpaper",
                                                    "bluedsteel",
                                                    "heattintedtitanium",
                                                    "castiron",
                                                    "wroughtiron",
                                                    "bronze",
                                                    "pewter",
                                                    "zinc",
                                                    "anodizedblack",
                                                    "gunmetal",
                                                    "hammeredcopper",
                                                    "engineturnedsteel",
                                                    "millscale",
                                                    "powdercoatgreen",
                                                    "powdercoatyellow",
                                                    "electroplatednickel",
                                                    "tin",
                                                    "leadsheet",
                                                    "damascussteel",
                                                    "quiltedstainless",
                                                    "expandedmetal",
                                                    "boltedaccesspanels",
                                                    "dentedsteel",
                                                    "weldedpatchwork",
                                                    "shiphullplating",
                                                    "boilerplate",
                                                    "lockerdoors",
                                                    "rollershutter",
                                                    "sheetpiling",
                                                    "trapezoidcladding",
                                                    "insulatedpanel",
                                                    "electricalcabinet",
                                                    "steeldoorskin",
                                                    "vaultdoor",
                                                    "hexboltgrid",
                                                    "tankarmour",
                                                    "aircraftskin",
                                                    "steelgridwall",
                                                    "pressedtinceiling",
                                                    "bargrating",
                                                    "galvanizedgrating",
                                                    "fibreglassgrating",
                                                    "epoxyfloor",
                                                    "steelcheckerfloor",
                                                    "interlockingrubbertiles",
                                                    "raisedaccessfloor",
                                                    "loadingdockfloor",
                                                    "polishedconcrete",
                                                    "steeldeckplate",
                                                    "meshcatwalk",
                                                    "industrialtiles",
                                                    "peelinggreenpaint",
                                                    "ruststreaks",
                                                    "heavyscalerust",
                                                    "oxidizedsteel",
                                                    "sootysteel",
                                                    "greasestainedmetal",
                                                    "sandblastedsteel",
                                                    "waterstainedconcrete",
                                                    "spalledconcrete",
                                                    "batteredyellowpaint",
                                                    "graffitibuffedwall",
                                                    "birdspottedroof",
                                                    "saltcrustedsteel",
                                                    "radiationwarning",
                                                    "ventilationduct",
                                                    "radiatorfins",
                                                    "controlpanel",
                                                    "gaugecluster",
                                                    "furnaceglow",
                                                    "moltenslag",
                                                    "warninglights",
                                                    "railwaysleepers",
                                                    "railtrack",
                                                    "manholecovers",
                                                    "stormdraingrate",
                                                    "roadmarkings",
                                                    "zebracrossing",
                                                    "parkingbaylines",
                                                    "tactilepaving",
                                                    "kerbstones",
                                                    "precastconcrete",
                                                    "boardformedconcrete",
                                                    "gabionwall",
                                                    "retainingwallblocks",
                                                    "tunnellining",
                                                    "bridgegirder",
                                                    "rubblepile",
                                                    "crackedslab",
                                                    "weedsthroughconcrete",
                                                    "shatteredtiles",
                                                    "exposedrebar",
                                                    "bulletpockedwall",
                                                    "collapsedbrick",
                                                    "burntoutruinwall",
                                                    "toppledcolumn",
                                                    "ruinedparquet",
                                                    "crumblingplaster",
                                                    "sandbagwall",
                                                    "tanktrapfield",
                                                    "dustylinoleum",
                                                    "charredtimber",
                                                    "scrapheap",
                                                    "palletstack",
                                                    "guttedmachinery",
                                                    "aircraftwreckage",
                                                    "fridgegraveyard",
                                                    "rustedsheetfence",
                                                    "engineblock",
                                                    "chainheap",
                                                    "bentrebarbundle",
                                                    "duststormhaze",
                                                    "saltflatwasteland",
                                                    "bleachedbones",
                                                    "sunbakedhardpan",
                                                    "ripplesand",
                                                    "driedriverbed",
                                                    "sandblastedsign",
                                                    "rustdunes",
                                                    "dustyglass",
                                                    "tumbleweedbrush",
                                                    "driedbloodearth",
                                                    "sandburiedroad",
                                                    "caliche",
                                                    "windscouredrock",
                                                    "ashdunes",
                                                    "radioactivepuddles",
                                                    "mutantgrowth",
                                                    "acidetchedmetal",
                                                    "hazmattarp",
                                                    "glowingfissures",
                                                    "oilywater",
                                                    "sulfurpitcrust",
                                                    "contaminatedsoil",
                                                    "bubblingtar",
                                                    "warningtapemesh",
                                                    "irradiatedglass",
                                                    "biosludgedrain",
                                                    "scrapmetalplating",
                                                    "cratewood",
                                                    "mudandstrawdaub",
                                                    "leantocanvas",
                                                    "firecrackedearth",
                                                    "smoulderingash",
                                                    "blisteredpaint",
                                                    "meltedplastic",
                                                    "rottingwood",
                                                    "mouldbloom",
                                                    "corrodedcopperroof",
                                                    "peelingwallpaper",
                                                    "termiteeatenbeam",
                                                    "charcoalfloor",
                                                    "lichencrustedstone",
                                                    "deadforestfloor",
                                                    "witheredvines",
                                                    "bonedrywood",
                                                    "scorchedtiles",
                                                    "clearlagoon",
                                                    "swampwater",
                                                    "oilslick",
                                                    "tarpit",
                                                    "honey",
                                                    "mercury",
                                                    "moltengold",
                                                    "magmariver",
                                                    "toxicsludge",
                                                    "bloodpool",
                                                    "hotspring",
                                                    "quicksandliquids",
                                                    "iceslush",
                                                    "plasmapool",
                                                    "ink",
                                                    "glowingtide",
                                                    "sewage",
                                                    "wetcement",
                                                    "pinkslime",
                                                    "coolant",
                                                    "whiterapids",
                                                    "deepocean",
                                                    "fizzingsoda",
                                                    "liquidvoid",
                                                    "mudbath",
                                                    "wideplanks",
                                                    "hugeplanks",
                                                    "largebrick",
                                                    "hugebrick",
                                                    "largecobbles",
                                                    "hugecobbles",
                                                    "largetile",
                                                    "hugetile",
                                                    "largestone",
                                                    "hugestone",
                                                    "largelondonstock",
                                                    "largeredengineeringbrick",
                                                    "largeherringbonebrick",
                                                    "largebasketweavebrick",
                                                    "largewhitesquaretile",
                                                    "largecheckerboardtile",
                                                    "largesubwaytile",
                                                    "largehextileterracotta",
                                                    "largeherringbonetile",
                                                    "largeslatetile",
                                                    "largetravertinetile",
                                                    "largelimestoneashlar",
                                                    "largesandstoneashlar",
                                                    "largegraniteblocks",
                                                    "largerandomashlar",
                                                    "largedrystonewall",
                                                    "largecastlestone",
                                                    "largepolygonalstone",
                                                    "largecobbledsetts",
                                                    "largediagonalsetts",
                                                    "largeflagstonepaving",
                                                    "largeriverstonewall",
                                                    "largemossystoneblocks",
                                                    "largebasketweaveparquet",
                                                    "largelogcabin",
                                                    "largescaffoldboards",
                                                    "largefrostedplanks",
                                                    "largecratewood",
                                                    "largetemplestone",
                                                    "largekerbstones",
                                                    "bluetintedglass",
                                                    "smokedglass",
                                                    "amberglass",
                                                    "frostedglass",
                                                    "reededglass",
                                                    "stainedglass",
                                                    "glassblock",
                                                    "scratchedplexiglass",
                                                    "icewall",
                                                    "quartzcrystal",
                                                    "greengel",
                                                    "hexforcefield",
                                                    "redlaserbarrier",
                                                    "hologrampanel",
                                                    "chainlinkmesh",
                                                    "cargonet",
                                                    "soapfilm",
                                                    "watercurtain",
                                                    "smokewall",
                                                    "rainwindow",
                                                    "prismglass",
                                                    "jellycube",
                                                    "ghostglass",
                                                    "scannerbeam",
                                                    "bottlegreenglass",
                                                    "woodencrate",
                                                    "plywoodboardup",
                                                    "drywall",
                                                    "cinderblockwall",
                                                    "paperscreen",
                                                    "bamboofence",
                                                    "cardboardboxes",
                                                    "picketfence",
                                                    "chipboard",
                                                    "rustedsheet",
                                                    "thinconcretepanel",
                                                    "crystalcluster",
                                                    "sandstoneslab",
                                                    "sugarglass",
                                                    "haybales",
                                                    "rottenplanks",
                                                    "ceramictilewall",
                                                    "mirror",
                                                    "barricade",
                                                    "frozenwaterfall",
                                                    "stainedglasswindow",
                                                    "tyrestack",
                                                    "lathandplaster",
                                                    "glassbottles",
                                                    "healingtile",
                                                    "spikefloor",
                                                    "lowgravityfield",
                                                    "slowmofield",
                                                    "gravityflip",
                                                    "checkpointgate",
                                                    "lightblock",
                                                    "shieldwall",
                                                    "crumbleplatform",
                                                    "teleporter"};
static_assert(sizeof kMapMaterialWords / sizeof *kMapMaterialWords == kMapMaterials, "a file keyword per material");
inline int materialKeyword(const std::string& w) {
    for (int m = 0; m < kMapMaterials; m++) if (w == kMapMaterialWords[m]) return m;
    return -1;
}
inline int nextMapMaterial(int m) { return (m + 1) % kMapMaterials; }   // the editor's M key: base .. lava, trim, clip, nograpple, acid, ice .. diamond plate, base

// What any map may hold, however it arrives (file, download, demo, edits): Map::validate checks all of it, after
// groups and ladders are expanded, before the collision grid is built. Shipped maps use a small fraction of this.
constexpr float kMapExtent = 4096.0f;        // every coordinate, |x| <= this
constexpr size_t kMaxMapBrushes = 60000;     // generated ones included, on every path (file, download, edit)
static_assert(kMaxMapBrushes <= 0xFFFF, "the collision grid's cells index brushes with 16 bits (Map::cellBrushes)");
constexpr size_t kMaxMapGridRefs = 16u << 20; // brush references in the collision grid (32 MB; one box over the whole range is 4.2M)
constexpr size_t kMaxMapSpawns = 256, kMaxMapLights = 512, kMaxMapLadders = 256, kMaxMapGroups = 256, kMaxGroupParts = 1024;
constexpr size_t kMaxMapStairs = 512;
constexpr size_t kMaxMapSites = 16, kMaxMapCheckpoints = 128, kMaxMapPath = 512, kMaxMapPoints = 16, kMaxMapCartChecks = 64;
constexpr float kMaxObjectHeight = 64.0f;    // ladders and groups (a ladder's rungs are made every 0.4 m)
constexpr float kMinLightRadius = 0.5f, kMaxLightRadius = 100.0f, kMaxLightIntensity = 10.0f;   // intensity > 0 (0 lights nothing)
inline bool validMapLight(float radius, float intensity) {
    return radius >= kMinLightRadius && radius <= kMaxLightRadius && intensity > 0 && intensity <= kMaxLightIntensity;   // (NaN fails)
}

// A map's title, from a file or an edit (one rule for both): the spaces around it dropped, then at most
// kMaxMapTitle printable ASCII characters and no '#' (the comment mark). false: not a valid title.
constexpr size_t kMaxMapTitle = 40;
bool cleanMapTitle(std::string& title);
// A map's identity (REVIEW MAP-13): the sha256 of its text (CR dropped, so a CRLF copy is the same map), or in an
// edit session, chained over the edit ops from there (mapEditId), so nobody re-serializes the map to name it. Downloads,
// the downloaded-map cache, light caches, the bots' nav graph and the ranked allowlist all go by it.
struct MapId {
    uint8_t b[32] = {};
    bool operator==(const MapId& o) const { return memcmp(b, o.b, sizeof b) == 0; }
    bool operator!=(const MapId& o) const { return !(*this == o); }
    std::string hex() const;                   // all 64 hex digits (file names, the ranked allowlist)
    static constexpr size_t kShortBytes = 16;  // an edit event names the map it leaves by its first 16 bytes
    std::string shortHex() const;              // those, 32 hex digits
    bool startsWith(const std::string& shortHex) const { return shortHex.size() == 2 * kShortBytes && this->shortHex() == shortHex; }
};
MapId mapContentId(const std::string& text);                      // sha256 of the text without its CRs
MapId mapEditId(const MapId& before, const std::string& op);     // the id after applying op to map `before`

struct SpawnPoint {
    Vec3 pos;
    float yaw = 0;
    int team = SPAWN_ANY;
};

// The .qmap format (REVIEW MAP-10/11, JUDGMENT C7). "version N" heads a file (none: version 1, which kept stairs and
// the setting in comments; those are still read). A keyword this build doesn't know is skipped with a warning (in
// Map::warnings), so an older build still loads a newer map; a known keyword with a bad value is an error.
// 2: "version", "stairs", "setting", "handmade"; a second hill or an unknown spawn team is an error.
constexpr int kMapFormatVersion = 11;   // 3: the nograpple material; 4: the acid material; 5: wedges and corners; 6: brick, cobblestone and glass never break (breakable ones are their own materials); 7: materials 139-471; 8: the liquids 472-496 and the large patterns 497-536; 9: the see-through looks 537-561; 10: the breakable blocks 562-585 and the feature blocks 586-595 (a teleporter's "tp <link> <face>"); 11: mode spawn sets ("spawn koth-a ...")

struct Map {
    std::string name;
    std::string title;
    int formatVersion = kMapFormatVersion;   // the file's "version" (1 when it has none)
    std::vector<std::string> warnings;        // what the parse skipped (unknown keywords, modes, ladders): logged
    // "handmade": made or changed by a person (the editor writes it on every save), not by tools/make_maps.py.
    // quark_tests runs every map check on a hand-made map and reports what it misses, but doesn't fail on it (the
    // owner's rule, docs/JUDGMENT_CALLS.md). humanMade(): that, or a file the generator didn't write (no header of its).
    bool handmade = false;
    bool humanMade() const;
    std::string setting;       // "setting <words>": the place the map is (a city block, a space station...)
    // "stairs x0 z0 x1 z1 base top x|z rising solid": a staircase the generator built from boxes (steps from base to top
    // over the x0..z1 footprint, rising toward +/- axis; solid: blocks down to the ground, else slabs). Only recorded:
    // nothing is made from it (quark_tests walks every one and checks it cuts no path).
    struct Stairs { float x0 = 0, z0 = 0, x1 = 0, z1 = 0, base = 0, top = 0, rising = 1; char axis = 'x'; bool solid = true; };
    std::vector<Stairs> stairs;
    int preferredMode = -1;    // "for <mode>": the mode this map was designed for
    uint16_t rankedLadders = 0; // "ranked vanilla vampire instagib": the ranked ladders (1v1 rulesets) this map is built for
    uint32_t allowedModes = 0; // "modes ffa duel ...": only these (bit per mode); 0 = every mode the map has the pieces for
    std::vector<Brush> brushes;
    std::vector<SpawnPoint> spawns;
    std::vector<AABB> bombSites;
    bool hasHill = false;
    AABB hill;
    std::vector<AABB> ladders;        // "ladder x0 y0 z0 x1 y1 z1 [auto]": climbable volumes (stand in it and move)
    std::vector<uint8_t> ladderAuto;  // per ladder: draws its own rails and rungs (one editor object; else separate boxes do)
    void genLadderBrushes();          // (re)makes the generated brushes (auto ladders' rails / rungs, groups' parts), at the end of brushes
    // "group custom|room|stairs <material> <dir> x0 y0 z0 x1 y1 z1" (+ "part <material> fx0 fy0 fz0 fx1 fy1 fz1" lines for custom,
    // a wedge / corner part followed by "wedge|corner <dir + 4 * up>"):
    // one editor object whose boxes are made from its box, so moving / stretching it moves / stretches them all.
    // room: floor, walls with a player-sized doorway each, ceiling. stairs: steps rising toward dir (0 +x, 1 -x, 2 +z,
    // 3 -z), each no taller than pmove's step-up. custom: parts kept as 0..1 fractions of the box.
    enum { GROUP_CUSTOM, GROUP_ROOM, GROUP_STAIRS };
    struct Group { int type = GROUP_CUSTOM, material = 0, dir = 0; AABB box; std::vector<Brush> parts; };
    std::vector<Group> groups;
    static void groupBoxes(const Group& g, std::vector<Brush>& out); // its parts in world space
    size_t editableBrushes() const { // (the generated brushes are always the last ones)
        size_t n = brushes.size();
        while (n && brushes[n - 1].generated) n--;
        return n;
    }
    std::vector<AABB> checkpoints;    // race; the last one is the finish line
    // what race plays: the checkpoints, then the checkpoint gate brushes in brush order (the last one is the finish),
    // remade with the grid (buildRace)
    std::vector<AABB> raceCheckpoints;
    void buildRace();
    std::vector<Vec3> payloadPath;    // ordered waypoints
    std::vector<float> cartChecks;    // payload checkpoints, as fractions of the track ("cartcheck 0.33"), ascending
    std::vector<AABB> points;         // multi point: the capture zones, in the order they're attacked ("point x0 y0 z0 x1 y1 z1")
    Vec3 sunDir = normalize(Vec3(0.4f, 1.0f, 0.25f)); // points toward the sun
    // "palette base r g b accent r g b metal r g b glow r g b sky r g b" (linear 0..1): per-map colours
    // "void <y>": falling below this height kills (pits, water, the drop off a bridge); default 20 m below the map
    bool hasVoid = false;
    float holdFire = 0;        // "holdfire <s>": nobody can fire for this long after a round goes live (the dojo's bow)
    bool indoor = false;   // "indoor <y>": a roof over everything at y (cameras that orbit the map stay below it)
    float ceilingY = 0;
    float voidY = 0;
    float killY() const { return hasVoid ? voidY : bounds.mn.y - 20.0f; }
    bool hasPalette = false;
    Vec3 palBase, palAccent, palMetal, palGlow, palSky;
    struct Light { Vec3 pos; float radius = 10, intensity = 1; };
    std::vector<Light> lights;        // baked point lights ("light x y z radius [intensity]"): lit rooms under roofs
    AABB bounds;
    // The map's identity (MapId): its content's, until an edit session chains it (setEditId). Hashed the first time
    // it's asked for (never on the parse alone: most parses never need it).
    const MapId& id() const { return editChained_ ? editId_ : contentId(); }
    const MapId& contentId() const;          // mapContentId(sourceText()): what a download checks, and ranked
    void setEditId(const MapId& v) { editId_ = v; editChained_ = true; }
    // The .qmap text (sent to clients that lack the map, saved, hashed for ranked): the parsed text, or after edits
    // serialize()d the first time it's asked for. Read it with sourceText().
    mutable std::string source;
    mutable bool sourceStale = false;
    mutable bool contentIdKnown_ = false;    // contentId_ is mapContentId(source)
    mutable MapId contentId_;
    MapId editId_;
    bool editChained_ = false;
    const std::string& sourceText() const { if (sourceStale) { source = serialize(); sourceStale = false; contentIdKnown_ = false; } return source; }

    bool load(const std::string& name, std::string* err = nullptr);
    bool onLadder(const AABB& player) const { for (auto& l : ladders) if (player.overlaps(l)) return true; return false; }
    // Never throws: a bad map is an error. Every field is reset first (only name is kept), so nothing of an earlier
    // map or of a failed parse is left behind.
    bool parse(const std::string& text, std::string* err = nullptr);
    bool parseText(const std::string& text, std::string* err);          // (parse's body: upgrades an older format, then parseCurrent)
    bool parseCurrent(const std::string& text, std::string* err);       // (the current format)
    bool validate(std::string* err = nullptr) const;   // the limits above (counts, finite, in range)
    // Back to .qmap text (what the editor saves). Every number is written with as many digits as it needs to parse
    // back to the same float (spawn yaws to the same radians), so a saved map is exactly the map its edits made.
    std::string serialize() const;
    bool rebuild();                   // after editing brushes in place: bounds, wallbang flag, grid (false: over the limits)
    // One brush replaced, inserted or removed, with bounds, wallbang flag, lava and acid lists and grid left exactly as rebuild()
    // would leave them, but updated in place (the edit path: Midtown's full rebuild is ~0.3 ms, this a few us). The
    // generated brushes aren't remade (the caller keeps them right). false: over the grid's limit, nothing changed.
    bool setBrush(size_t i, const Brush& b) { return changeBrush(0, i, b); }
    bool insertBrush(size_t i, const Brush& b) { return changeBrush(1, i, b); }
    bool eraseBrush(size_t i) { return changeBrush(2, i, Brush()); }
    bool changeBrush(int op, size_t i, const Brush& b);

    // Movement collision: returns how far along `axis` the box may move (clamped delta).
    // *hit (when given): the brush that stopped it, nullptr for none or a moving solid (pmove: a steep slope is a wall).
    // *glass (not null): glass doesn't stop the move; the index of the first pane it would have is put there (else -1)
    float sweepAxis(const AABB& box, int axis, float delta, const Brush** hit = nullptr, int* glass = nullptr) const;
    bool boxSolid(const AABB& box) const;
    bool inLava(const AABB& box) const { for (auto& l : lava) if (box.mn.x < l.mx.x && box.mx.x > l.mn.x && box.mn.y < l.mx.y && box.mx.y > l.mn.y && box.mn.z < l.mx.z && box.mx.z > l.mn.z) return true; return false; }
    std::vector<AABB> lava;   // lava brushes (touch one and you die), from rebuild()
    bool inAcid(const AABB& box) const { for (auto& l : acid) if (box.mn.x < l.mx.x && box.mx.x > l.mn.x && box.mn.y < l.mx.y && box.mx.y > l.mn.y && box.mn.z < l.mx.z && box.mx.z > l.mn.z) return true; return false; }
    std::vector<AABB> acid;   // acid brushes (pmove counts the damage while you touch one), from rebuild()
    // the feature fields (FIELD_*): low gravity and slow-mo boxes, the gravity flip tiles' columns (kGravFlipHeight up
    // from the top), from rebuild(); pmove and the projectiles ask them only when there are any
    enum : uint8_t { FIELD_LOWGRAV = 1, FIELD_SLOW = 2, FIELD_FLIP = 4 };
    struct Field { AABB box; uint8_t kind; };
    std::vector<Field> fields;
    bool hasSlow = false;
    uint8_t fieldsAt(const AABB& box) const {   // the kinds the box touches
        uint8_t k = 0;
        for (auto& f : fields)
            if (box.mn.x < f.box.mx.x && box.mx.x > f.box.mn.x && box.mn.y < f.box.mx.y && box.mx.y > f.box.mn.y && box.mn.z < f.box.mx.z && box.mx.z > f.box.mn.z) k |= f.kind;
        return k;
    }
    float projectileRate(const Vec3& p) const {   // kSlowMoRate inside a slow-mo field, else 1
        if (!hasSlow) return 1.0f;
        for (auto& f : fields)
            if ((f.kind & FIELD_SLOW) && p.x >= f.box.mn.x && p.x <= f.box.mx.x && p.y >= f.box.mn.y && p.y <= f.box.mx.y && p.z >= f.box.mn.z && p.z <= f.box.mx.z) return kSlowMoRate;
        return 1.0f;
    }
    // projectileExtrapolate through the slow-mo fields (in 2 ms steps there; the closed form when there are none)
    void flyProjectile(Vec3& pos, Vec3& vel, float gravity, float t) const;
    struct Liquid { AABB box; uint16_t kind; };   // kind: kLiquids' index
    std::vector<Liquid> liquids;   // the liquid brushes (kMatLiquids..: pmove moves you differently in them), from rebuild()
    const LiquidDef* liquidAt(const AABB& box) const {   // the first one the box touches (none: null)
        for (auto& l : liquids)
            if (box.mn.x < l.box.mx.x && box.mx.x > l.box.mn.x && box.mn.y < l.box.mx.y && box.mx.y > l.box.mn.y && box.mn.z < l.box.mx.z && box.mx.z > l.box.mn.z)
                return &kLiquids[l.kind];
        return nullptr;
    }
    void rebuildFluids();   // lava, acid and liquids from the brushes
    // Returns hit distance or -1. normal receives the face normal on hit.
    float raycast(const Vec3& origin, const Vec3& dir, float maxDist, Vec3* normal = nullptr, int* material = nullptr, bool hitClip = false) const;   // *material: the hit brush's (-1: a moving solid); hitClip: clip (9) stops it too (the grapple)
    // Shadow-ray style early out: light, sight and line of fire, so clip (material 9) doesn't block it unless
    // hitClip (movement probes: clip blocks players).
    bool raycastAny(const Vec3& origin, const Vec3& dir, float maxDist, bool hitClip = false) const;
    // What blocked the last ray that used this hint: a grid slot (cell, and the slot in its brush list) or a moving solid.
    struct RayHint { uint32_t cell = UINT32_MAX, ref = 0; int32_t dyn = -1; };
    // raycastAny's answer, exactly, for every ray (PERF: lines of sight, where most rays are blocked, and by the same
    // brush as the ray before). It first tries what the hint names: a moving solid the ray hits (raycastAny tests them
    // all), or the brush in a grid slot that is still in that cell's list, isn't skipped (clip), is hit by the same
    // rayAABB test, and whose cell the same cell walk reaches (a brush can be hit in a cell the walk never visits: the
    // walk stops at the grid's solid Y slab, and at a corner it steps to one of the two neighbours only, while the box
    // test alone counts a graze). raycastAny reaching that cell tests that slot unless it hit something first, so it
    // answers blocked as well. Otherwise the full cast, learning its blocker into the hint. Nothing to invalidate: an
    // edited brush, a rebuilt grid or a moved cart is only ever checked as it is now.
    bool raycastAnyHinted(const Vec3& origin, const Vec3& dir, float maxDist, RayHint& hint, bool hitClip = false) const;
    // Hitscan bullets: like raycast, but passes through wallbang brushes; *through is set if it went
    // through at least one before the returned hit (or maxDist); *bangDist = distance to the first one.
    float raycastBullet(const Vec3& origin, const Vec3& dir, float maxDist, float* bangDist) const;
    bool hasWallbang = false;
    bool hasIce = false, hasRubber = false;   // (pmove asks what's underfoot / hit only on maps with them)
    bool hasGripMats = false;                 // ice, rubber, asphalt or mud: pmove asks what's underfoot

    // ---- Destruction in play (owner, 2026-10-28): glass breaks, thin brick and cobblestone get holes blown in them.
    // The server and every client make the same calls in the same order (EV_DESTRUCT), so all of them collide alike;
    // resetDestruction undoes it all (each round, a map restart, an edit). Never saved: a map's text stays the map's.
    // A hole: its centre (u, v) on the wall and half size r, the wall's thin axis and its extent along it, and the cells
    // (bricks, stones) it breaks out (holeTakes; the world shader draws the same).
    // r < 0: the whole wall crumbled (kCrumble): a rectangle, half sizes cw x ch round (u, v).
    struct Hole { float u, v, r, cw, ch; int axis; float lo, hi; };
    std::vector<Hole> holes;
    std::vector<uint32_t> blocks;         // the breakable blocks (kBreaks: breakGlass takes them too)
    std::vector<uint32_t> crumbles;       // the crumble platforms (breakGlass and restoreBrush take them)
    std::vector<uint32_t> teleporters;    // the teleporters, in brush order
    int teleporterAt(const AABB& box) const;   // the first one the box touches (-1: none)
    int teleportTarget(int from) const;        // the next one with from's link after it (round to the first; -1: none)
    // where something leaving teleporter `to` goes: the middle of its exit face, out by `out` (*n: the face's normal)
    Vec3 teleportExit(int to, float out, Vec3* n) const;
    bool restoreBrush(size_t i);          // a gone one back (a crumble platform after kCrumbleBackS; false: it wasn't gone)
    int crumbleAt(const Vec3& p, float eps = 0.01f) const;
    std::vector<uint32_t> glass;          // the breakable glass brushes (drawn see-through each frame; a gone one isn't)
    std::vector<uint32_t> windows;        // the plain glass brushes (never break: drawn see-through each frame, nothing else)
    std::vector<Brush> pristineBrushes;   // the brushes as they were before the first break (empty: none yet)
    uint32_t destructSerial = 0;          // counts every change (the renderer sends the holes again)
    bool breakGlass(size_t i);            // false: not a standing pane
    int glassAt(const Vec3& p, float eps = 0.03f) const;   // a standing pane whose box (grown by eps) holds p, -1 none
    int blockAt(const Vec3& p, float eps = 0.03f) const;   // the same for a standing breakable block
    // Blows a hole in every thin (kBreakableThick) brick / cobblestone box near `at`: kHoleSize across where it meets the
    // wall, less the distance from it to the wall's plane (owner: "if a sticky blows up 1m away ... a 1.5m hole").
    // Returns how many boxes it cut. A wall with less than 1 - kCrumble of it left comes down whole (owner, 2026-10-29:
    // "when 80% of a destructable wall gets broken the whole thing should disapear"); *crumbled gets those walls' boxes.
    int punchHole(const Vec3& at, std::vector<AABB>* crumbled = nullptr);
    void resetDestruction();

    bool supportsMode(int mode) const;  // allowed and has what the mode needs
    bool canSupportMode(int mode) const; // has what the mode needs (ignores allowedModes)

    // Uniform XZ grid over brushes: queries only touch cells they pass through.
    static constexpr float kCell = 4.0f;
    int gridW = 0, gridH = 0;
    float gridX0 = 0, gridZ0 = 0;
    // Cell c's brushes (indices, ascending) are cellBrushes[start, end); the slots up to the next cell's start are spare
    // room, so an edit can add a brush to a cell without rebuilding the grid.
    struct GridCell { uint32_t start = 0, end = 0; };
    std::vector<GridCell> cells;       // gridW*gridH
    std::vector<uint16_t> cellBrushes;
    size_t gridRefs = 0;               // brush references in use (spare room not counted)
    // Lowest bottom / highest top of the brushes in the grid (empty: lo > hi). Rays stop walking cells once they have
    // left this slab (walkCells): a sun or sky ray otherwise walks every column to the grid's edge. Edits only widen it.
    float gridYLo = 1e30f, gridYHi = -1e30f;
    bool buildGrid();                  // false: more grid references than kMaxMapGridRefs
    static bool gridSolid(const Brush& b) { return !b.gone && (solidMaterial(b.material) || b.material == 9); } // in the grid: far scenery, water, lava aren't
    template <class F> void forBox(const AABB& b, F f) const; // calls f(brush) for brushes in overlapped cells (may repeat)

    // Moving solids that aren't part of the static geometry (the payload cart). Collision and
    // raycasts include them; kept tiny so checking them all is free.
    std::vector<AABB> dynSolids;
    AABB payloadCartBox(float dist) const;
    float payloadLength() const;
    Vec3 payloadPoint(float dist) const;
    // Payload: where on the track the cart is after `progress` metres pushed. With unstaged spawns each team spawns
    // at its own end, so in the rounds team B attacks the cart starts at B's end and runs the track the other way.
    float cartTrack(int mode, int attackers, float progress) const;
    Vec3 cartPoint(int mode, int attackers, float progress) const { return payloadPoint(cartTrack(mode, attackers, progress)); }
};

// Map edit ops (the editor's undo steps and what multiplayer editing sends through the server), one line each:
//   <kind> ins|set|del <index> [fields]   kind: b brush (material + box), s spawn (team x y z yawDeg), l light (x y z radius
//                                         intensity), z bomb site (box), c checkpoint (box), p path point (x y z), h hill (has + box),
//                                         o multi point capture zone (box), d ladder (auto + box),
//                                         g group (type material dir box; ins makes it empty, set keeps its parts)
//   g part <index> <material> <6 fractions of the box> [w|c <dir + 4 * up>]: adds a part (a box, wedge or corner) to custom group <index>
//   (materials: 0..kMapMaterials-1 for boxes, groups and parts alike, trim 8, clip 9, nograpple 10 and acid 11 included)
//   t <title>    f <preferred mode, -1 any>    m <allowed modes bitmask, 0 = all>
// applyMapEdit validates (indices, sizes, finite numbers, at least one box and spawn) and updates the grid (in place
// for brushes and custom group parts). It doesn't serialize: m.source goes stale until m.sourceText() is asked for.
bool applyMapEdit(Map& m, const std::string& op);
// The op that sets (or inserts) object `idx` of `kind` to its current state in m: used to build edits and their undo.
std::string describeMapObject(const Map& m, char kind, const char* verb, int idx);

std::string findDataPath(const std::string& rel); // resolves shipped, read-only data (maps/..., mods/...) in the install
// Per-user data (settings, ranked key, demos, your maps, downloaded maps, light caches, HUD/controls profiles, custom
// modes, logs): never inside the install, so updates and uninstalls can't touch it.
//   Windows %APPDATA%\quark   macOS ~/Library/Application Support/quark   Linux $XDG_DATA_HOME/quark (~/.local/share/quark)
// --data-dir / QUARK_DATA_DIR override it. The first use of the default folder copies files left in the old
// install-folder locations over (originals are kept; a .migrated marker stops it running again).
void setUserDataDir(const std::string& dir, bool migrate = false); // call before the first userDataPath()
std::string userDataPath(const std::string& rel = ""); // <root>/<rel>; the root is created on first use
std::string userMapsDir();                              // your own / edited maps (searched before the shipped ones)
int migrateUserData(const std::string& oldRoot, const std::string& newRoot); // files copied (exposed for tests)
void setExecutableDir(const char* argv0);
const std::string& executableDir();       // empty if unknown
std::vector<std::string> listMaps();
// The file Map::load reads for a map name (old names mapped to the new ones): your maps folder first, then the
// shipped one. Empty for a name that isn't valid.
std::string mapFilePath(const std::string& name);
// A map name is a name, never a path: 1-40 letters, digits, - and _ (the editor's rule), and not a Windows device
// name (con, nul, com1, ...: opening "maps/con.qmap" opens the device). Names from servers and demos become file
// names (map, download, light cache, demo), so every one is checked with this.
bool validMapName(const std::string& name);
// A plain file name inside one folder (a HUD or controls profile): printable, no folder separators or drive, doesn't
// start with '.', at most 128 characters.
bool plainFileName(const std::string& name);
// Writes every shipped map back to its original text (and drops their light caches and your edited copies) and removes
// retired ones; returns how many. shippedDir: the install's maps folder (findDataPath("maps")) unless given (tests).
int resetOfficialMaps(const std::string& shippedDir = std::string());
// Every map embedded in the game (tools/embed_maps.py): name and original text, by name (quark --bake-maps).
std::vector<std::pair<std::string, std::string>> officialMapTexts();
std::string mapsFolder(); // absolute path of your maps folder (userMapsDir)
std::string typedMapName(const std::string& typed); // what the editor's name field keeps of what you type: validMapName's characters (space -> _), at most 40
std::string safeMapName(const std::string& name); // for building file names: anything else becomes '_', capped
// Maps fetched from servers live apart from your own, keyed by content: <user>/maps/downloads/<sha256 hex>.qmap
std::string downloadedMapPath(const MapId& id);
bool saveMapFile(const std::string& path, const std::string& text);
// Writes a file whole or not at all: a temp file next to it, then a rename over it, so a crash or a full disk
// leaves the old file (settings, profiles, the ranked key, custom modes, maps). privateFile: only this user may
// read it (POSIX 0600: the ranked key).
bool writeFileAtomic(const std::string& path, const std::string& data, bool privateFile = false);

// Map format converters (owner rule: every change to the map format ships a converter step, in every future version
// too). kMapUpgrades[k - 1] turns a version k file into version k + 1; upgradeMapText runs every step from the file's
// version up to kMapFormatVersion and then writes the "version" line. Map::parse runs it first, so the parser itself only
// ever reads the current format (and a newer file is parsed as it is, skipping what this build doesn't know).
int mapTextVersion(const std::string& text);                     // the file's "version" (1 when it has none, 0 a bad one)
bool upgradeMapText(const std::string& in, std::string* out);    // false: nothing to do (current or newer); out untouched
struct MapUpgradeReport { int converted = 0, failed = 0; std::vector<std::string> log; };
// Every .qmap directly in `dir` written in an older format (a player's own maps): converted in place, the original
// kept as dir/old/v<N>/<name> and a line in dir/old/convert.log. One that doesn't convert (or doesn't parse after) is
// left as it was, logged, and not tried again. Cheap once done: a current file is told by its first or last 4 KB.
MapUpgradeReport upgradeMapDir(const std::string& dir);

