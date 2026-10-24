#include "botbrain.h"
#include "gamedefs.h"

float evalBrainCurve(const BrainCurve& c, float x) {
    if (c.kind == BrainCurve::STEPS) {
        float v = c.base;
        for (int k = 0; k < c.n; k++) if (x > c.x[k]) v = c.y[k];
        return v;
    }
    if (c.n == 0) return 0.0f;
    if (!(x > c.x[0])) return c.y[0];
    for (int k = 0; k + 1 < c.n; k++)
        if (x <= c.x[k + 1]) return c.y[k] + (c.y[k + 1] - c.y[k]) * ((x - c.x[k]) / (c.x[k + 1] - c.x[k]));
    return c.y[c.n - 1];
}

namespace {

BrainList makeList(std::initializer_list<int> ws) {
    BrainList l;
    for (int w : ws) { l.w[l.n++] = (uint8_t)w; l.mask |= (uint16_t)(1u << w); }
    return l;
}

BrainParams makeDefault(int level) {
    BrainParams p{};
#define QB_SET(name, type, unit, lo, hi, doc, a, b, c, d) { const QB_CT_##type v[4] = {a, b, c, d}; p.name = v[level]; }
    QUARK_BRAIN_PARAMS(QB_SET)
#undef QB_SET
    p.l_close       = makeList({WPN_SHOTGUN, WPN_KNIFE, WPN_LASER, WPN_BUBBLE, WPN_ROCKET, WPN_RIFLE, WPN_STICKY, WPN_CROSSBOW, WPN_SNIPER, WPN_SYRINGE});
    p.l_mid         = makeList({WPN_ROCKET, WPN_LASER, WPN_RIFLE, WPN_STICKY, WPN_BUBBLE, WPN_CROSSBOW, WPN_SHOTGUN, WPN_SNIPER, WPN_KNIFE, WPN_SYRINGE});
    p.l_midNoLead   = makeList({WPN_RIFLE, WPN_LASER, WPN_SHOTGUN, WPN_ROCKET, WPN_STICKY, WPN_CROSSBOW, WPN_BUBBLE, WPN_SNIPER, WPN_KNIFE, WPN_SYRINGE});
    p.l_far         = makeList({WPN_CROSSBOW, WPN_RIFLE, WPN_SNIPER, WPN_LASER, WPN_ROCKET, WPN_STICKY, WPN_SHOTGUN, WPN_BUBBLE, WPN_KNIFE, WPN_SYRINGE});
    p.l_farNoLead   = makeList({WPN_RIFLE, WPN_SNIPER, WPN_LASER, WPN_CROSSBOW, WPN_ROCKET, WPN_STICKY, WPN_SHOTGUN, WPN_BUBBLE, WPN_KNIFE, WPN_SYRINGE});
    p.l_vfar        = makeList({WPN_SNIPER, WPN_CROSSBOW, WPN_RIFLE, WPN_ROCKET, WPN_STICKY, WPN_LASER, WPN_SHOTGUN, WPN_BUBBLE, WPN_KNIFE, WPN_SYRINGE});
    p.l_hookWeapons = makeList({WPN_SHOTGUN, WPN_KNIFE, WPN_LASER, WPN_BUBBLE});
    for (float& v : p.t_idealRange) v = 14.0f;
    p.t_idealRange[WPN_SHOTGUN] = 4.0f; p.t_idealRange[WPN_SNIPER] = 40.0f; p.t_idealRange[WPN_KNIFE] = 1.5f;
    p.t_idealRange[WPN_LASER] = 12.0f; p.t_idealRange[WPN_STICKY] = 10.0f; p.t_idealRange[WPN_BUBBLE] = 7.0f;
    p.t_idealRange[WPN_CROSSBOW] = 22.0f; p.t_idealRange[WPN_RIFLE] = 18.0f;
    p.c_reactDist.kind = BrainCurve::STEPS; p.c_reactDist.base = 1.0f;
    p.c_reactDist.n = 1; p.c_reactDist.x[0] = 40.0f; p.c_reactDist.y[0] = 1.25f;
    return p;
}

const BrainParams kDefaultParams[4] = {makeDefault(0), makeDefault(1), makeDefault(2), makeDefault(3)};

} // namespace

const BrainParams& defaultBrainParams(int level) { return kDefaultParams[level < 0 || level > 3 ? 1 : level]; }
