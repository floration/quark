// parse / serialize: the .qmap text <-> the JSON form, through the game's own Map::parse and Map::serialize.
#include "maptool.h"

static int runParse(const Json& req, Json& out, CmdCtx&) {
    double t0 = maptoolNow();
    Map m;
    std::string err;
    if (!loadRequestMap(req, m, err)) return cmdError(out, err);
    out = mapToJson(m);
    if (req["withText"].boolean()) out.set("serialized", m.serialize());   // what Map::serialize writes for it (the editor's save)
    out.set("ms", (maptoolNow() - t0) * 1000);
    return 0;
}
MAPTOOL_COMMAND("parse", runParse, "{text|file|name|map, withText?} -> the map's JSON form (+ player hull, modes it supports)");

static int runSerialize(const Json& req, Json& out, CmdCtx&) {
    const Json& mj = req["map"].isObj() ? req["map"] : req;
    Map m;
    std::string err;
    if (!mapFromJson(mj, m, err)) return cmdError(out, err);
    out = Json::object();
    std::string text = m.serialize();
    // the text must load as the game loads it (limits, ranges, at least one box and spawn): say so if it doesn't
    Map check;
    check.name = m.name.empty() ? "untitled" : m.name;
    std::string perr;
    bool ok = check.parse(text, &perr);
    out.set("text", text);
    out.set("valid", ok);
    if (!ok) out.set("parseError", perr);
    else {
        Json w = Json::array();
        for (auto& s : check.warnings) w.push(s);
        out.set("warnings", std::move(w));
    }
    return 0;
}
MAPTOOL_COMMAND("serialize", runSerialize, "the map's JSON form (or {map}) -> {text, valid, parseError?} via Map::serialize");
