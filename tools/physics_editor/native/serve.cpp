// physserve --stream: the physics editor's helper kept running. serve.py starts one and sends it every physsim /
// physexpr / physexport request as a line, instead of starting quark_maptool per request: a run after an edit then
// costs the run itself, not a process start (about 10 ms on Linux, more on Windows) plus the map parse (the maps sent
// as text are kept: setMapCache). Each request runs the very command the one-shot form runs.
//
//   first line  {}                                        (the --stream request: options, none yet)
//   then        {"id": 7, "cmd": "physsim", "request": {...}}   one per line
//   answers     {"type": "ready"} once, then {"id": 7, "code": 0, "answer": {...}} per request, in order
//   stop        the process ends
#include <string>
#include "maptool.h"
#include "physics.h"

namespace phys {
namespace {

bool served(const std::string& c) {
    for (const char* n : {"physsim", "physexpr", "physexport", "physparams", "physpresets", "physroom"}) if (c == n) return true;
    return false;
}

int runServe(const Json&, Json& out, CmdCtx& ctx) {
    if (!ctx.control) return cmdError(out, "physserve needs --stream (a request per line on stdin)");
    setMapCache(true);
    { Json r = Json::object(); r.set("type", "ready"); ctx.emit(r); }
    std::string line, err;
    while (std::getline(*ctx.control, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        if (line == "stop") break;
        Json req, ans = Json::object(), o = Json::object();
        int rc = 1;
        if (!Json::parse(line, req, &err) || !req.isObj()) cmdError(ans, "not a request line: " + err);
        else {
            o.set("id", req["id"]);
            const std::string name = req["cmd"].str();
            const Command* cmd = nullptr;
            if (served(name)) for (auto& c : maptoolCommands()) if (name == c.name) cmd = &c;
            if (!cmd) cmdError(ans, "physserve runs physsim, physexpr, physexport, physparams, physpresets, physroom; not '" + name + "'");
            else {
                CmdCtx one;   // (the one-shot form: no --stream, no control lines)
                const Json& r = req["request"];
                try { rc = cmd->run(r.isObj() ? r : Json::object(), ans, one); }
                catch (const std::exception& e) { ans = Json::object(); rc = cmdError(ans, std::string("internal error: ") + e.what()); }
                if (rc && !ans["error"].isStr()) cmdError(ans, "failed");
            }
        }
        o.set("code", rc);
        o.set("answer", std::move(ans));
        ctx.emit(o);
    }
    setMapCache(false);
    return 0;
}

} // namespace

MAPTOOL_COMMAND("physserve", runServe, "physics editor --stream: {} then {id, cmd: physsim|physexpr|physexport|..., request} lines -> {id, code, answer} lines");

} // namespace phys
