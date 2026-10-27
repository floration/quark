// physexpr: a physics set's movement equations (src/game/moveexpr.h) as the game builds them, with each dynamic one
// sampled over the range of every input it reads (the editor's curve plot). INTERFACE.md, "v2".
#include <cmath>
#include "maptool.h"
#include "physics.h"

namespace phys {
namespace {

// One input's sample points: the request's inputs["p.name"] ([lo, hi], {min, max, n} or {values: [...]}), else its
// default range; flags (0 / 1) are 2 points, the charges whole numbers.
bool samplePoints(const Json& req, int input, int defaultN, std::vector<double>& xs, std::string& err) {
    const mexpr::InputInfo& q = mexpr::inputInfo(input);
    const std::string name = std::string("p.") + q.name;
    const Json& j = req["inputs"][name];
    double lo = q.lo, hi = q.hi;
    int n = defaultN;
    const bool flag = q.lo == 0 && q.hi == 1 && input != mexpr::P_AIR && input != mexpr::P_GROUND;
    if (flag) n = 2;
    if (input == mexpr::P_CHARGES) n = (int)(hi - lo) + 1;
    xs.clear();
    if (j.isArr()) {
        if (j.size() != 2 || !j[0].isNum() || !j[1].isNum()) { err = "inputs." + name + " must be [lo, hi], {min, max, n} or {values: [...]}"; return false; }
        lo = j[0].num(); hi = j[1].num();
        if (!flag && input != mexpr::P_CHARGES) n = defaultN;
    } else if (j.isObj()) {
        if (j["values"].isArr()) {
            for (size_t k = 0; k < j["values"].size() && k < 10000; k++) {
                if (!j["values"][k].isNum()) { err = "inputs." + name + ".values must be numbers"; return false; }
                xs.push_back(j["values"][k].num());
            }
            return true;
        }
        lo = j["min"].num(lo); hi = j["max"].num(hi);
        if (j["n"].isNum()) n = j["n"].i();
    } else if (!j.isNull()) { err = "inputs." + name + " must be [lo, hi], {min, max, n} or {values: [...]}"; return false; }
    if (!std::isfinite(lo) || !std::isfinite(hi)) { err = "inputs." + name + ": not a number"; return false; }
    n = std::max(2, std::min(n, 10000));
    for (int k = 0; k < n; k++) xs.push_back(lo + (hi - lo) * k / (n - 1));
    return true;
}

int runExpr(const Json& req, Json& out, CmdCtx&) {
    const double t0 = maptoolNow();
    // the set: "physics" / "qmode" as everywhere, with "expr" (an object {key: text}, or a string with "key") over it
    Json phys = req["physics"].isNull() ? (req["qmode"].isStr() ? Json(req["qmode"].str()) : Json::object()) : req["physics"];
    if (phys.isStr()) { Json o = Json::object(); o.set("qmode", phys.str() == "standard" ? std::string() : phys.str()); phys = std::move(o); }
    if (!phys.isObj()) return cmdError(out, "physics must be an object, 'standard' or .qmode text");
    const Json& ex = req["expr"];
    if (!ex.isNull()) {
        Json merged = phys["expr"].isObj() ? phys["expr"] : Json::object();
        if (ex.isStr()) {
            if (!req["key"].isStr()) return cmdError(out, "expr as a string needs \"key\": the setting it is for");
            merged.set(req["key"].str(), ex.str());
        } else if (ex.isObj()) {
            for (auto& kv : ex.members()) merged.set(kv.first, kv.second);
        } else return cmdError(out, "expr must be an object {key: \"expression\"} or a string with \"key\"");
        phys.set("expr", std::move(merged));
    }
    CustomRules r;
    std::vector<std::string> clamped;
    std::string err;
    if (!physicsFromJson(phys, r, clamped, err)) return cmdError(out, err);
    MoveTuning t;
    mexpr::Program prog;
    mexpr::Report rep;
    buildExprs(r, t, prog, rep);
    const int n = std::max(2, std::min(req["samples"].i(101), 10000));
    // the other inputs while one is swept: at rest, or the request's "at": {"p.air": 0.3}
    double at[mexpr::P_COUNT];
    mexpr::restInputs(at);
    for (auto& kv : req["at"].members()) {
        bool known = false;
        for (int i = 0; i < mexpr::P_COUNT; i++)
            if (kv.first == std::string("p.") + mexpr::inputInfo(i).name) { known = true; at[i] = kv.second.num(at[i]); }
        if (!known) return cmdError(out, "at." + kv.first + " isn't an input (physparams expr.inputs lists them)");
    }
    out = Json::object();
    Json items = Json::array();
    for (auto& it : rep.items) {
        Json o = exprItemJson(it);
        const int s = mexpr::settingIndex(it.key);
        if (it.ok && it.dynamic && s >= 0) {
            Json samples = Json::array();
            for (int i = 0; i < mexpr::P_COUNT; i++) {
                if (!(it.allInputs & 1u << i)) continue;
                std::vector<double> xs;
                if (!samplePoints(req, i, n, xs, err)) return cmdError(out, err);
                Json xa = Json::array(), ya = Json::array(), aa = Json::array();
                for (double x : xs) {
                    double in[mexpr::P_COUNT];
                    for (int q = 0; q < mexpr::P_COUNT; q++) in[q] = at[q];
                    in[i] = x;
                    if (i == mexpr::P_AIR && x > 0) in[mexpr::P_ONGROUND] = 0;      // (in the air: not on the floor)
                    if (i == mexpr::P_HEIGHT && x > 0) in[mexpr::P_ONGROUND] = 0;
                    if (i == mexpr::P_GROUND) in[mexpr::P_ONGROUND] = 1;
                    MoveTuning u = t;
                    prog.run(in, u);
                    const float v = mexpr::settingGet(u, s);
                    xa.push(x); ya.push(v); aa.push(v * mexpr::settingBase(s));
                }
                Json sm = Json::object();
                sm.set("input", std::string("p.") + mexpr::inputInfo(i).name);
                sm.set("unit", mexpr::inputInfo(i).unit);
                sm.set("x", std::move(xa)); sm.set("y", std::move(ya)); sm.set("abs", std::move(aa));
                samples.push(std::move(sm));
            }
            o.set("samples", std::move(samples));
        }
        items.push(std::move(o));
    }
    out.set("items", std::move(items));
    Json order = Json::array();
    for (int u : rep.order) order.push(rep.items[u].key);
    out.set("order", std::move(order));
    out.set("dynamic", (int)prog.items.size());
    out.set("ops", (int)prog.ops.size());
    out.set("physics", physicsToJson(r));
    if (!clamped.empty()) { Json c = Json::array(); for (auto& k : clamped) c.push(k); out.set("clamped", std::move(c)); }
    out.set("ms", (maptoolNow() - t0) * 1000);
    return 0;
}

} // namespace

MAPTOOL_COMMAND("physexpr", runExpr, "physics editor: {expr:{key:text}|text+key, physics?, inputs?, at?, samples?} -> {items:[{ok, error, dynamic, deps, inputs, value, samples}]}");

} // namespace phys
