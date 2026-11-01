// Movement equations (src/game/moveexpr.h, owner 2026-10-24): the parser and its errors, static resolution order,
// cycles, clamps, the .qmode round trip, the deterministic math (golden bits: the Windows CI run checks MinGW gives the
// same), the dynamic path in pmove (a friction curve vs stopping distance; an always-1 equation bit-identical to the
// plain rules), p.air / p.ground on the wire, and a server + client predicting the same equations over the network.
// Included by tests.cpp (it shares CHECK, pump, TP). `quark_tests exprbench` measures the cost per step.
#include "game/moveexpr.h"
#include "game/protocol.h"

// golden bits of the deterministic math and of a compiled program (computed on Linux x86-64 / glibc; the Windows CI
// run (MinGW) must match: a mismatch means the server and its clients could disagree)
static constexpr uint64_t kMoveExprMathGolden = 0xfa20c966af511a09ull, kMoveExprProgramGolden = 0x62746c4413756e1cull;

static mexpr::Report exprReport(const std::vector<std::pair<std::string, std::string>>& ex, MoveTuning* out = nullptr, mexpr::Program* prog = nullptr) {
    MoveTuning t;
    mexpr::Program p;
    mexpr::Report r;
    mexpr::build(ex, t, p, &r);
    if (out) *out = t;
    if (prog) *prog = p;
    return r;
}
static const mexpr::ItemReport* exprItem(const mexpr::Report& r, const std::string& key) {
    for (auto& i : r.items) if (i.key == key) return &i;
    return nullptr;
}
static float exprHspeed(const Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }
static uint64_t fnvDoubles(const std::vector<double>& v) {
    uint64_t h = 1469598103934665603ull;
    for (double d : v) { unsigned char b[8]; memcpy(b, &d, 8); for (unsigned char c : b) { h ^= c; h *= 1099511628211ull; } }
    return h;
}

static void testMoveExpr() {
    printf("movement equations: parse, resolve, cycles, round trip, deterministic math, pmove\n");
    { // parse errors: each names where and why, and leaves the setting at its fixed value
        struct Bad { const char* text; const char* says; } bad[] = {
            {"1 +", "unexpected end"}, {"spede * 2", "unknown name 'spede'"}, {"p.sped", "unknown input 'p.sped'"},
            {"sqrt(1, 2)", "sqrt() takes 1 arguments"}, {"curve(p.speed, 0, 1, 0, 1, \"wobble\")", "unknown ease mode \"wobble\""},
            {"(1 + 2", "expected ')'"}, {"1 ? 2", "expected ':'"}, {"\"in\"", "only an ease mode"}, {"ease(p.air, 2)", "mode must be a quoted name"},
            {"base.nothing", "unknown setting 'base.nothing'"}, {"frobnicate(1)", "unknown function 'frobnicate'"}, {"2 $ 3", "unexpected '$'"},
            {"1e999", "out of range"}, {".", "expected a number"},
        };
        for (auto& b : bad) {
            MoveTuning t;
            mexpr::Report r = exprReport({{"friction", b.text}}, &t);
            const mexpr::ItemReport* it = exprItem(r, "friction");
            const bool ok = it && !it->ok && it->error.find(b.says) != std::string::npos && it->error.rfind("col ", 0) == 0 && t.m[MV_FRICTION] == 1;
            if (!ok) printf("  '%s': %s\n", b.text, it ? it->error.c_str() : "(no item)");
            CHECK(ok);
        }
        std::string deep(200, '(');   // nested too deep, never a stack overflow
        CHECK(!exprReport({{"friction", deep + "1" + std::string(200, ')')}}).items[0].ok);
        std::string longSum = "1";
        for (int i = 0; i < 200; i++) longSum += "+1";
        CHECK(exprReport({{"friction", longSum}}).items[0].error.find("too long") != std::string::npos);
        CHECK(!exprReport({{"nothing", "1"}}).items[0].ok);   // not a setting
        // the 32-value stack: a right-leaning chain needs one slot per level
        std::string chain;
        for (int i = 0; i < 40; i++) chain += "p.speed + (";
        chain += "1" + std::string(40, ')');
        mexpr::Report deepR = exprReport({{"friction", chain}});
        CHECK(!deepR.items[0].ok && deepR.items[0].error.find("too complex") != std::string::npos);
    }
    { // precedence, functions, constants
        auto eval = [](const std::string& e) {   // a static expression's value (within friction's 0.1..5)
            MoveTuning t;
            mexpr::Report r = exprReport({{"friction", e}}, &t);
            if (!r.items[0].ok) printf("  '%s': %s\n", e.c_str(), r.items[0].error.c_str());
            return (double)t.m[MV_FRICTION];
        };
        auto near = [](double a, double b) { return std::fabs(a - b) < 1e-6; };
        CHECK(near(eval("1 + 2 * 3 / 4"), 2.5) && near(eval("-2^2 + 5"), 1) && near(eval("2^-1"), 0.5) && near(eval("2^3^0.5 - 1"), std::pow(2, std::sqrt(3.0)) - 1));
        CHECK(near(eval("(1 < 2) + (2 <= 2) + (3 > 4) + (1 == 1) + (1 != 1)"), 3) && near(eval("1 && 0 || 1 ? 2 : 3"), 2) && near(eval("!0 + !5"), 1));
        CHECK(near(eval("sqrt(4) + abs(-1) - sign(-3)"), 4) && near(eval("min(3, 1, 2) + max(0.5, 0.25)"), 1.5) && near(eval("floor(1.7) + ceil(0.2) + round(0.5)"), 3));
        CHECK(near(eval("clamp(9, 1, 2)"), 2) && near(eval("lerp(1, 3, 0.25)"), 1.5) && near(eval("step(1, 0.5) + step(1, 1)"), 1));
        CHECK(near(eval("exp(log(3))"), 3) && near(eval("pow(2, 0.5)^2"), 2) && near(eval("pi / 3.14159265358979"), 1) && near(eval("base.friction"), 5) && near(eval("base.speed / 9.5"), 1));
        CHECK(near(eval("curve(5, 0, 10, 1, 3)"), 2) && near(eval("curve(-5, 0, 10, 1, 3)"), 1) && near(eval("curve(50, 0, 10, 1, 3)"), 3));
        CHECK(near(eval("curve(2.5, 0, 10, 1, 3, \"in\")"), 1 + 2 * 0.0625) && near(eval("curve(2.5, 0, 10, 1, 3, 'out')"), 1 + 2 * (1 - 0.75 * 0.75)));
        CHECK(near(eval("ease(0.5, \"smooth\") * 2"), 1) && near(eval("ease(0.25, \"inout\") * 8"), 1) && near(eval("ease(2, \"expo\") + ease(-1, \"back\") + 1"), 2));
        CHECK(near(eval("curve(10, 10, 10, 1, 2)"), 2) && near(eval("curve(9, 10, 10, 1, 2)"), 1));   // (x0 == x1: a step)
        CHECK(near(eval("1.5e0 + .5 + 0.05e1 - 0.000"), 2.5));
        CHECK(eval("100") == 5.0f && eval("-3") == 0.1f && eval("0 / 0") == 1.0f);   // clamped; NaN keeps the fixed value
        for (int s = 0; s < mexpr::kSettings; s++) {   // the ranges match CustomRules::fromText's clamps
            CustomRules lo, hi;
            const std::string line = s < 3 ? std::string(mexpr::settingKey(s)) : std::string("move_") + mexpr::settingKey(s);
            lo.fromText("custommode=x\n" + line + "=-1e30\n");
            hi.fromText("custommode=x\n" + line + "=1e30\n");
            auto get = [&](const CustomRules& r) { return s == 0 ? r.gravity : s == 1 ? r.moveSpeed : s == 2 ? r.jump : r.move[s - 3]; };
            CHECK(get(lo) == mexpr::settingMin(s) && get(hi) == mexpr::settingMax(s));
            CHECK(mexpr::settingIndex(mexpr::settingKey(s)) == s);
        }
    }
    { // static resolution: in dependency order whatever the file order; base.<key> is the standard absolute value
        MoveTuning t;
        mexpr::Report r = exprReport({{"friction", "ground_accel * 2"}, {"ground_accel", "speed + 0.5"}, {"speed", "1.5"},
                                      {"stop_speed", "base.friction / 7 * jump"}}, &t);
        for (auto& i : r.items) CHECK(i.ok && !i.dynamic);
        CHECK(t.speed == 1.5f && t.m[MV_GROUND_ACCEL] == 2.0f && t.m[MV_FRICTION] == 4.0f && t.m[MV_STOP_SPEED] == 1.0f && t.expr == nullptr);
        CHECK(r.order.size() == 4 && r.items[r.order[0]].key == "speed");
        CHECK(exprItem(r, "friction")->deps == std::vector<std::string>({"ground_accel"}));
        // a static value is the clamped one for everything that reads it
        exprReport({{"speed", "10"}, {"jump", "speed / 2"}}, &t);
        CHECK(t.speed == 3.0f && t.jump == 1.5f);
    }
    { // cycles: an error on each setting in the cycle (they keep their fixed values); one reading a cycle reads that
        MoveTuning t;
        mexpr::Report r = exprReport({{"friction", "stop_speed + 1"}, {"stop_speed", "air_accel"}, {"air_accel", "friction"}, {"air_cap", "friction * 2"},
                                      {"step", "step * 2"}}, &t);
        for (const char* k : {"friction", "stop_speed", "air_accel", "step"}) CHECK(!exprItem(r, k)->ok && exprItem(r, k)->error.find("cycle") != std::string::npos);
        CHECK(exprItem(r, "air_cap")->ok && t.m[MV_AIR_CAP] == 2.0f && t.m[MV_FRICTION] == 1.0f && t.m[MV_STEP] == 1.0f);
        printf("  %s\n", exprItem(r, "friction")->error.c_str());
    }
    { // dynamic: classified through the settings an expression reads; a dynamic one's value at rest is in the tuning
        MoveTuning t;
        mexpr::Program prog;
        mexpr::Report r = exprReport({{"friction", "curve(p.hspeed, 0, 10, 2, 1)"}, {"stop_speed", "friction * 2"}, {"air_cap", "base.air_cap / 2.3 + 0"},
                                      {"gravity", "p.air > 0.2 ? 1.5 : 1"}}, &t, &prog);
        CHECK(exprItem(r, "friction")->dynamic && exprItem(r, "stop_speed")->dynamic && !exprItem(r, "air_cap")->dynamic && exprItem(r, "gravity")->dynamic);
        CHECK(exprItem(r, "stop_speed")->allInputs == (1u << mexpr::P_HSPEED) && exprItem(r, "stop_speed")->inputs == 0);
        CHECK(t.m[MV_FRICTION] == 2.0f && t.m[MV_STOP_SPEED] == 4.0f && t.m[MV_AIR_CAP] == 1.0f && t.gravity == 1.0f);   // (at rest)
        CHECK(prog.items.size() == 3 && prog.inputs == (1u << mexpr::P_HSPEED | 1u << mexpr::P_AIR));
        double in[mexpr::P_COUNT];
        mexpr::restInputs(in);
        in[mexpr::P_HSPEED] = 5; in[mexpr::P_AIR] = 0.3; in[mexpr::P_ONGROUND] = 0;
        prog.run(in, t);
        CHECK(t.m[MV_FRICTION] == 1.5f && t.m[MV_STOP_SPEED] == 3.0f && t.gravity == 1.5f);
        in[mexpr::P_HSPEED] = NAN;   // a NaN result: the fixed value, never a NaN in the movement
        prog.run(in, t);
        CHECK(t.m[MV_FRICTION] == 1.0f);
    }
    { // the .qmode: expr.<key>= lines round-trip as written; unknown keys and control characters are dropped
        CustomRules a;
        CHECK(a.fromText("custommode=eq\nmove_friction=1.5\nexpr.friction=  curve(p.hspeed, 0, 20, 2, 0.5, \"inout\")  \nexpr.gravity=p.air > 0.3 ? 1.4 : 1\n"
                         "expr.nothing=1\nexpr.jump=\nexpr.speed=bad(\nexpr.gravity=p.air > 0.2 ? 1.5 : 1\nexpr.step=1\x01\n"));
        CHECK(a.exprs.size() == 3 && a.exprs[0].first == "friction" && a.exprs[0].second == "curve(p.hspeed, 0, 20, 2, 0.5, \"inout\")");
        CHECK(a.exprs[1].first == "gravity" && a.exprs[1].second == "p.air > 0.2 ? 1.5 : 1" && a.exprs[2].first == "speed");   // (the later line wins, in place)
        CHECK(a.move[MV_FRICTION] == 1.5f);   // (the plain value is kept: the fallback, and what older versions play)
        CustomRules b;
        CHECK(b.fromText(a.toText()) && b.toText() == a.toText() && b.exprs == a.exprs);
        CHECK(a.toText().find("\nexpr.friction=curve(p.hspeed, 0, 20, 2, 0.5, \"inout\")\n") != std::string::npos);
        CHECK(a.fromText("custommode=x\nexpr.friction=" + std::string(mexpr::kMaxText + 1, '1') + "\n") && a.exprs.empty());
        // set as the active rules: static ones resolved, dynamic ones compiled; standard / no-equation rules: no program
        setActiveRules(b);
        CHECK(moveTuning().expr != nullptr && moveTuning().m[MV_FRICTION] == 2.0f);   // (at rest: hspeed 0 -> 2)
        CustomRules st;
        st.fromText("custommode=static\nexpr.friction=2\n");
        setActiveRules(st);
        CHECK(moveTuning().expr == nullptr && moveTuning().m[MV_FRICTION] == 2.0f);
        setActiveRules(CustomRules());
        CHECK(moveTuning().expr == nullptr && moveTuning().m[MV_FRICTION] == 1.0f);
    }
    { // deterministic math: golden bits over a fixed grid (the CI's Windows run must get the same; std:: libm may not)
        std::vector<double> v;
        for (int i = -400; i <= 400; i++) {
            const double x = i * 0.037 + 0.001;
            v.push_back(mexpr::detExp(x));
            v.push_back(mexpr::detExp2(x));
            if (x > 0) v.push_back(mexpr::detLog(x));
            v.push_back(mexpr::detPow(std::fabs(x) + 0.5, x * 0.25));
            v.push_back(mexpr::detPow(x, 3));
            for (int m = 0; m < (int)mexpr::easeModes().size(); m++) v.push_back(mexpr::ease(m, (i + 400) / 800.0));
        }
        double worst = 0;   // and close to the C library's
        for (int i = -400; i <= 400; i++) {
            const double x = i * 0.037 + 0.001;
            worst = std::max(worst, std::fabs(mexpr::detExp(x) / std::exp(x) - 1));
            if (x > 0) worst = std::max(worst, std::fabs(mexpr::detLog(x) - std::log(x)) / std::max(1.0, std::fabs(std::log(x))));
            worst = std::max(worst, std::fabs(mexpr::detPow(std::fabs(x) + 0.5, x * 0.25) / std::pow(std::fabs(x) + 0.5, x * 0.25) - 1));
        }
        const uint64_t h = fnvDoubles(v);
        printf("  deterministic math: %zu values, hash %016llx, worst relative error vs libm %.2g\n", v.size(), (unsigned long long)h, worst);
        CHECK(worst < 1e-14);
        CHECK(h == kMoveExprMathGolden);
        CHECK(mexpr::detPow(-2, 3) == -8 && mexpr::detPow(0, -1) == HUGE_VAL && std::isnan(mexpr::detPow(-2, 0.5)) && mexpr::detPow(5, 0) == 1);
        CHECK(std::isnan(mexpr::detLog(-1)) && mexpr::detLog(0) == -HUGE_VAL && mexpr::detExp(1000) == HUGE_VAL && mexpr::detExp(-1000) == 0);
        CHECK(mexpr::detExp(0) == 1 && mexpr::detLog(1) == 0 && mexpr::detExp2(10) == 1024);
        // a compiled program on fixed inputs: golden bits too (the server and every client compute these)
        mexpr::Program prog;
        MoveTuning t;
        exprReport({{"friction", "curve(p.hspeed, 2, 20, 3, 0.5, \"outexpo\")"}, {"gravity", "1 + 0.5 * ease(p.air / 0.6, \"back\") + sqrt(p.height) / 20"},
                    {"air_accel", "pow(1 + p.speed / 30, 1.7) * (p.dashing ? 0.5 : 1)"}, {"ground_accel", "exp(-p.ground * 3) + log(1 + p.charges)"}}, &t, &prog);
        CHECK(prog.items.size() == 4);
        std::vector<double> out;
        for (int i = 0; i < 2000; i++) {
            double in[mexpr::P_COUNT];
            mexpr::restInputs(in);
            in[mexpr::P_SPEED] = i * 0.025; in[mexpr::P_HSPEED] = i * 0.02; in[mexpr::P_AIR] = (i % 700) * 0.001; in[mexpr::P_GROUND] = (i % 300) * 0.002;
            in[mexpr::P_HEIGHT] = (i % 51); in[mexpr::P_DASHING] = i % 3 == 0; in[mexpr::P_CHARGES] = i % 4;
            prog.run(in, t);
            out.push_back(t.m[MV_FRICTION]); out.push_back(t.gravity); out.push_back(t.m[MV_AIR_ACCEL]); out.push_back(t.m[MV_GROUND_ACCEL]);
        }
        const uint64_t hp = fnvDoubles(out);
        printf("  compiled program on fixed inputs: hash %016llx\n", (unsigned long long)hp);
        CHECK(hp == kMoveExprProgramGolden);
    }
    { // pmove: a friction curve (more friction slow, less fast) between the standard stop and a fixed x2's
        Map open;
        std::string err;
        CHECK(open.parse("box -500 -1 -500 500 0 500\nspawn any 0 0 0\n", &err));
        auto stop = [&](const char* rules) {
            CustomRules r;
            r.fromText(std::string("custommode=stop\n") + rules);
            setActiveRules(r);
            MoveState q; q.flags = MF_ONGROUND;
            UserCmd run; run.forward = 127; run.msec = 1;
            for (int i = 0; i < 1000; i++) pmove(q, run, open);
            Vec3 p0 = q.pos;
            UserCmd none; none.msec = 1;
            int ms = 0;
            while (ms < 5000 && (q.vel.x != 0 || q.vel.z != 0)) { pmove(q, none, open); ms++; }
            setActiveRules(CustomRules());
            return std::make_pair(length(q.pos - p0), ms);
        };
        auto st = stop(""), x2 = stop("move_friction=2\n"), curve = stop("expr.friction=curve(p.hspeed, 2, 9.5, 2, 1)\n");
        auto same = stop("expr.friction=1 + 0 * p.hspeed\n");   // dynamic, always 1: must be the standard run exactly
        printf("  stop from 9.5 m/s: standard %.4f m in %d ms, friction x2 %.4f m in %d ms, curve 2 (slow) -> 1 (9.5 m/s) %.4f m in %d ms\n",
               st.first, st.second, x2.first, x2.second, curve.first, curve.second);
        CHECK(st.first > 1.15f && st.first < 1.3f && x2.first < st.first * 0.6f);
        CHECK(curve.first < st.first && curve.first > x2.first && curve.second < st.second && curve.second > x2.second);
        CHECK(same == st);
        // the same with a dynamic equation that is always 1, over a whole run of everything (bit for bit, every field)
        Map m;
        CHECK(m.load("bastion"));
        auto trace = [&](const char* rules) {
            CustomRules r;
            r.fromText(std::string("custommode=trace\n") + rules);
            setActiveRules(r);
            MoveState q; q.pos = m.spawns[0].pos;
            uint32_t rng = 7;
            std::vector<double> out;
            UserCmd c;
            for (int k = 0; k < 30000; k++) {
                rng = rng * 1664525u + 1013904223u;
                if (k % 211 == 0) { c.forward = (int8_t)((int)(rng >> 24) - 128); c.side = (int8_t)((rng >> 8) % 3 * 127 - 127); c.buttons = (uint16_t)((rng >> 12) & (BTN_JUMP | BTN_DASH | BTN_GRAPPLE)); }
                c.yaw += ((int)((rng >> 4) % 2001) - 1000) * 1e-5f; c.pitch = ((int)((rng >> 16) % 301) - 150) * 0.005f; c.msec = (uint8_t)(1 + (rng >> 20) % 8);
                pmove(q, c, m);
                for (float f : {q.pos.x, q.pos.y, q.pos.z, q.vel.x, q.vel.y, q.vel.z, q.dashTimer, q.dashExitSpeed, q.hookCooldown}) out.push_back(f);
                out.push_back(q.flags); out.push_back(q.dashCharges); out.push_back(q.dashCooldownMs);
            }
            setActiveRules(CustomRules());
            return out;
        };
        std::vector<double> plain = trace("move_air_accel=1.2\n"), dyn = trace("move_air_accel=1.2\nexpr.air_accel=1.2 + 0 * p.speed\nexpr.gravity=p.height * 0 + p.air * 0 + 1\n");
        CHECK(plain == dyn);
        // p.air / p.ground: counted (gravity x2 after 0.15 s in the air: a lower jump), and only then
        auto apex = [&](const char* rules, uint16_t* phase) {
            CustomRules r;
            r.fromText(std::string("custommode=jump\n") + rules);
            setActiveRules(r);
            MoveState q; q.flags = MF_ONGROUND;
            UserCmd none; none.msec = 1;
            for (int i = 0; i < 50; i++) pmove(q, none, open);
            *phase = q.phaseMs;
            UserCmd j = none; j.buttons = BTN_JUMP;
            pmove(q, j, open);
            float top = 0;
            for (int i = 0; i < 2000 && !(q.flags & MF_ONGROUND); i++) { pmove(q, none, open); top = std::max(top, q.pos.y); }
            setActiveRules(CustomRules());
            return top;
        };
        uint16_t ph0 = 0, ph1 = 0;
        float a0 = apex("", &ph0), a1 = apex("expr.gravity=p.air > 0.15 ? 2 : 1\n", &ph1);
        printf("  jump apex: standard %.4f m, gravity x2 after 0.15 s in the air %.4f m (ground timer %u ms after 50 ms standing)\n", a0, a1, ph1);
        CHECK(ph0 == 0 && ph1 == 50 && a1 < a0 - 0.1f && a1 > 0.8f);
    }
    { // the wire: phaseMs rides on mods bit 128 (protocol 44); zero sends nothing extra; protocol 43 reads as before
        auto roundTrip = [](const MoveState& in, uint16_t proto, MoveState& out, int& size) {
            Snapshot s; s.tickRate = 1000; s.self = in;
            uint8_t buf[512];
            Writer w(buf, sizeof buf);
            writeSnapshotClient(w, s);
            size = w.size();
            Reader r(buf, w.size());
            Snapshot back;
            bool ok = readSnapshotClient(r, back, proto);
            out = back.self;
            return ok;
        };
        MoveState m; m.mods = MOVE_QUICK_DASH | MOVE_TINY | MOVE_LOW_GRAVITY;
        MoveState got; int n0 = 0, n1 = 0;
        CHECK(roundTrip(m, kProtocolVersion, got, n0) && got.mods == m.mods && got.phaseMs == 0);
        CHECK(roundTrip(m, 43, got, n0) && got.mods == m.mods);   // (no phase: the bytes are protocol 43's)
        m.phaseMs = 1234;
        CHECK(roundTrip(m, kProtocolVersion, got, n1) && got.mods == m.mods && got.phaseMs == 1234 && n1 == n0 + 2);
        CHECK(demo::convertible(43) && kSnapPhaseSince <= kProtocolVersion);
    }
}

// Everything outside pmove that reads a movement value for a player (stepTuning / moveValue: the HUD's dash pips and
// hook bar, the server's grapple on players, the syringe refill, the hyper sound, the bots, the helper's strafe bot)
// gets the value pmove's next step plays by, never the value at rest; and spent charges when the max falls mid-air.
static void testMoveExprCallers() {
    printf("movement equations outside pmove: the per-step value everywhere, charges when the max falls\n");
    Map open;
    std::string err;
    CHECK(open.parse("box -500 -1 -500 500 0 500\nspawn any 0 0 0\n", &err));
    auto rules = [](const char* text) { CustomRules r; r.fromText(std::string("custommode=callers\n") + text); setActiveRules(r); };
    { // no dynamic equation: moveTuning() itself (no copy)
        setActiveRules(CustomRules());
        MoveState q;
        MoveTuning scratch;
        CHECK(&stepTuning(q, &open, scratch) == &moveTuning() && moveValue(q, MV_HOOK_RANGE) == 1.0f && dashMaxCharges(q) == 1);
        rules("move_hook_range=1.5\nexpr.friction=2\n");
        CHECK(&stepTuning(q, &open, scratch) == &moveTuning() && moveValue(q, MV_HOOK_RANGE) == 1.5f && moveValue(q, MV_FRICTION) == 2.0f);
    }
    { // dynamic: for that state
        rules("expr.hook_range=curve(p.hspeed, 0, 20, 1, 2)\nexpr.dash_charges=p.onground ? 3 : 1\n");
        MoveState slow, fast;
        fast.vel = {10, 0, 0};
        slow.flags = fast.flags = MF_ONGROUND;
        CHECK(moveValue(slow, MV_HOOK_RANGE) == 1.0f && moveValue(fast, MV_HOOK_RANGE) == 1.5f);
        CHECK(dashMaxCharges(slow) == 3 && dashMaxCharges(MoveState()) == 1 && dashMaxCharges(uint8_t(0)) == 3);   // (the mods one: at rest)
        MoveState air;   // (the syringe: every charge back, for that state's max)
        air.dashCharges = 0;
        dashRefillAll(air, &open);
        CHECK(air.dashCharges == 1);
        // the max falls mid-air (3 on the floor, 1 in the air): the charges held above it go, the recharging ones
        // keep their timers; back on the floor once they're done, a refill gives all 3
        MoveState q; q.pos = {0, 30, 0};
        q.dashCharges = 1; q.dashRecharging = 2; q.dashRechargeMs[0] = 300; q.dashRechargeMs[1] = 500;
        UserCmd none; none.msec = 1;
        pmove(q, none, open);
        CHECK(q.dashCharges == 1 && q.dashRecharging == 2 && q.dashRechargeMs[0] == 299 && q.dashRechargeMs[1] == 499);
        MoveState h = q; h.dashCharges = 3;
        pmove(h, none, open);
        CHECK(h.dashCharges == 1 && h.dashRecharging == 2);
        for (int i = 0; i < 3000 && !(q.flags & MF_ONGROUND); i++) pmove(q, none, open);
        for (int i = 0; i < 600; i++) pmove(q, none, open);
        printf("  max 3 on the floor, 1 in the air: after the fall %d charges, %d recharging\n", q.dashCharges, q.dashRecharging);
        CHECK((q.flags & MF_ONGROUND) && q.dashCharges == 3 && q.dashRecharging == 0);
    }
    { // dashAimedDown agrees with pmove's own super dash test when dash_vertical changes during the dash: aimed 10 deg
      // down is a dash jump normally (sin 10 = 0.17 < 0.25), a super dash when the vertical halves while dashing
        auto tech = [&](const char* text, bool& aimed) {
            rules(text);
            MoveState q; q.flags = MF_ONGROUND;
            UserCmd c; c.msec = 1; c.forward = 127; c.pitch = -10 * PI / 180;
            for (int i = 0; i < 30; i++) pmove(q, UserCmd(), open);
            c.buttons = BTN_DASH;
            pmove(q, c, open);
            aimed = dashAimedDown(q, &open);   // (the state the next step starts from, as the game's sound reads it)
            c.buttons = BTN_DASH | BTN_JUMP;
            pmove(q, c, open);
            setActiveRules(CustomRules());
            return q;
        };
        bool a0 = true, a1 = false;
        MoveState plain = tech("", a0), eq = tech("expr.dash_vertical=p.dashing ? 0.5 : 1\n", a1);
        printf("  10 deg down: standard vy %.2f (%d charges, aimed down %d), dash_vertical x0.5 while dashing vy %.2f (%d charges, aimed down %d)\n",
               plain.vel.y, plain.dashCharges, (int)a0, eq.vel.y, eq.dashCharges, (int)a1);
        CHECK(!a0 && plain.vel.y > 7.0f && plain.dashCharges == 1);   // a dash jump: a full jump, every charge back (one by default)
        CHECK(a1 && eq.vel.y < plain.vel.y - 0.1f && eq.dashCharges == 0);   // a super dash: the hop (0.87 of a jump this shallow); its own charge still recharging (one by default: none left)
    }
    setActiveRules(CustomRules());
}

// A host whose custom mode has dynamic equations, and a client predicting by the rules it received: the server's state
// matches the prediction (no replays once running); a client predicting by the standard rules instead mispredicts.
static void testMoveExprNetwork(uint16_t port) {
    printf("movement equations over the network (server and client prediction agree)\n");
    setActiveRules(CustomRules());
    Server s;
    ServerConfig cfg;
    cfg.autoStart = true; cfg.map = "bastion"; cfg.port = port;
    CHECK(cfg.custom.fromText("custommode=equations\nmode=ffa\nexpr.friction=curve(p.hspeed, 2, 12, 2, 0.6, \"inout\")\n"
                              "expr.gravity=p.air > 0.25 ? 1.6 : 0.8\nexpr.air_accel=1 + p.speed / 20\nexpr.jump=1 + 0.2 * step(1, p.ground)\n"
                              "expr.air_cap=curve(p.height, 0, 6, 1, 1.5, \"smooth\")\n"));
    s.setLog([](const std::string&) {});
    { std::string e_; if (!s.start(cfg, &e_)) { printf("  FAIL %s:%d: server start: %s\n", __FILE__, __LINE__, e_.c_str()); g_fail++; return; } }
    setActiveRules(CustomRules());
    std::atomic<bool> quit{false};
    std::thread host([&] { s.run(quit); });
    std::atomic<int> phased{0};   // server states that carried the p.air / p.ground timer
    // each command's predicted state, kept here, against the server's for it once a snapshot acknowledges it
    auto play = [&](bool useRules, const char* name) {
        NetClient a;
        a.connect("127.0.0.1:" + std::to_string(port), name);
        std::vector<std::string> parts;
        int wrong = 0, compared = 0;
        bool rulesSet = false;
        std::vector<MoveState> mine(NetClient::kCmdRing);
        std::vector<uint32_t> mineSeq(NetClient::kCmdRing, 0);
        uint32_t lastAck = 0;
        const double t0 = timeNow();
        for (int i = 0; timeNow() - t0 < 16.0 && compared < 800; i++) {   // (the round's freeze first: counted once moving)
            a.poll();
            for (auto& e : a.takeEvents()) if (e.type == EV_RULES && e.b > 0 && e.a < e.b) { parts.resize(e.b); parts[e.a] = e.text; }
            if (!rulesSet && !parts.empty() && std::none_of(parts.begin(), parts.end(), [](auto& p) { return p.empty(); })) {
                std::string all; for (auto& p : parts) all += p;
                CustomRules got;
                CHECK(got.fromText(all) && got.exprs == cfg.custom.exprs);
                if (useRules) setActiveRules(got);   // (what Game does on EV_RULES)
                rulesSet = true;
            }
            if (a.hasSnapshot() && a.latest().ackCmdSeq != lastAck) {
                lastAck = a.latest().ackCmdSeq;
                const size_t k = lastAck % NetClient::kCmdRing;
                if (mineSeq[k] == lastAck) {
                    const MoveState& m = mine[k], &srv = a.latest().self;
                    compared++;
                    if (srv.phaseMs > 0) phased++;
                    const float dp = length(m.pos - srv.pos);
                    if (dp > 1.0f) compared--;   // (a respawn, not a prediction)
                    else if (dp > 1e-4f || length(m.vel - srv.vel) > 1e-3f || m.flags != srv.flags) wrong++;
                }
            }
            UserCmd c{};
            c.forward = 127; c.buttons = (i / 300) % 2 ? BTN_JUMP : 0; c.yaw = i * 0.002f;
            a.tick(c);
            const MoveState& pm = a.predicted();
            if (rulesSet && a.selfAlive() && !(pm.flags & MF_FROZEN) && exprHspeed(pm.vel) > 1.0f) {
                mine[(a.nextSeq() - 1) % NetClient::kCmdRing] = pm;
                mineSeq[(a.nextSeq() - 1) % NetClient::kCmdRing] = a.nextSeq() - 1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        setActiveRules(CustomRules());
        return std::make_pair(wrong, compared);
    };
    std::pair<int, int> good, bad;   // (both at once, each on its own thread: the rules a thread predicts by are its own)
    std::thread other([&] { bad = play(false, "nopred"); });
    good = play(true, "pred");
    other.join();
    quit = true; host.join(); s.stop();
    printf("  predictions the server disagreed with: %d of %d with the rules, %d of %d predicting by the standard rules (%d with the air / ground timer)\n", good.first, good.second, bad.first, bad.second, phased.load());
    CHECK(good.second > 200 && good.first == 0 && bad.second > 200 && bad.first > bad.second / 10 && phased > 100);
    setActiveRules(CustomRules());
}

// quark_tests exprbench: pmove's cost per 1 ms step with 0, 1 and 5 dynamic equations (running and bunny-hopping across
// an open floor; the median of 5 runs of 2 million steps each, steady_clock: the tests' timeNow() runs at the test time
// scale). The "neutral" equations are curves whose ends are both 1, so the movement is exactly the standard one and only
// their cost shows; the "live" ones change the movement (so the path, and the collision work, differ too).
static int moveExprBench() {
    Map open;
    std::string err;
    if (!open.parse("box -1000 -1 -1000 1000 0 1000\nspawn any 0 0 0\n", &err)) { printf("bench map: %s\n", err.c_str()); return 1; }
    auto now = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    const char* neutral1 = "expr.friction=curve(p.hspeed, 2, 12, 1, 1, \"inout\")\n";
    const char* neutral5 = "expr.friction=curve(p.hspeed, 2, 12, 1, 1, \"inout\")\nexpr.gravity=p.air > 0.25 ? 1 : 1\n"
                           "expr.air_accel=1 + p.speed * 0\nexpr.jump=1 + 0 * step(1, p.ground)\nexpr.air_cap=pow(1 + p.hspeed / 40, 1.5) * 0 + (p.dashing ? 1 : 1)\n";
    std::string neutral5h = std::string(neutral5) + "expr.ground_accel=curve(p.height, 0, 6, 1, 1, \"smooth\")\n";
    struct Case { const char* name; std::string rules; } cases[] = {
        {"standard rules (no custom mode)", ""},
        {"custom mode, no equations", "move_friction=1\n"},
        {"custom mode, static equations only", "expr.friction=sqrt(speed)\nexpr.air_accel=base.air_accel / 2.2\n"},
        {"1 dynamic equation (neutral)", neutral1},
        {"5 dynamic equations (neutral)", neutral5},
        {"6 dynamic, one reads p.height (neutral)", neutral5h},
        {"5 dynamic equations (live)", "expr.friction=curve(p.hspeed, 2, 12, 2, 0.6, \"inout\")\nexpr.gravity=p.air > 0.25 ? 1.6 : 0.8\n"
                                       "expr.air_accel=1 + p.speed / 20\nexpr.jump=1 + 0.2 * step(1, p.ground)\nexpr.air_cap=pow(1 + p.hspeed / 40, 1.5) * (p.dashing ? 0.5 : 1)\n"},
    };
    double baseNs = 0;
    for (auto& c : cases) {
        CustomRules r;
        if (!c.rules.empty()) r.fromText("custommode=bench\n" + c.rules);
        setActiveRules(r);
        std::vector<double> runs;
        for (int k = 0; k < 5; k++) {
            MoveState q; q.flags = MF_ONGROUND;
            UserCmd u; u.forward = 127; u.side = 0; u.msec = 1;
            const int N = 2000000;
            double t0 = now();
            for (int i = 0; i < N; i++) {
                u.buttons = (i / 700) % 3 ? BTN_JUMP : 0;
                u.yaw = (i % 20000) * 0.0003f;
                pmove(q, u, open);
                if (std::fabs(q.pos.x) > 900 || std::fabs(q.pos.z) > 900) q.pos = Vec3();
            }
            runs.push_back((now() - t0) * 1e9 / N);
        }
        std::sort(runs.begin(), runs.end());
        const double ns = runs[2];
        if (c.rules.empty()) baseNs = ns;
        printf("  %-42s %6.1f ns/step  (%+.1f ns vs standard)\n", c.name, ns, ns - baseNs);
    }
    setActiveRules(CustomRules());
    { // the interpreter alone: one item each, 20 million runs
        for (const char* e : {"curve(p.hspeed, 2, 12, 2, 0.6, \"inout\")", "p.air > 0.25 ? 1.6 : 0.8", "1 + p.speed / 20",
                              "pow(1 + p.hspeed / 40, 1.5) * (p.dashing ? 0.5 : 1)", "curve(p.speed, 0, 30, 1, 2, \"outexpo\")"}) {
            MoveTuning t;
            mexpr::Program prog;
            mexpr::build({{"friction", e}}, t, prog);
            double in[mexpr::P_COUNT];
            mexpr::restInputs(in);
            const int N = 20000000;
            double t0 = now(), sum = 0;
            for (int i = 0; i < N; i++) { in[mexpr::P_HSPEED] = in[mexpr::P_SPEED] = (i & 1023) * 0.03; in[mexpr::P_AIR] = (i & 511) * 0.001; prog.run(in, t); sum += t.m[MV_FRICTION]; }
            printf("  interpreter: %-52s %5.1f ns (%zu ops)  [%g]\n", e, (now() - t0) * 1e9 / N, prog.ops.size(), sum > 0 ? 1.0 : 0.0);
        }
    }
    return 0;
}
