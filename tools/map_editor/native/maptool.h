// quark_maptool's shared pieces: the command registry, the request / answer helpers and the map <-> JSON form.
// Commands in other folders (tools/bot_editor/native/*.cpp, globbed into the same executable by CMake) include this
// header and register themselves with one line:
//     static int runSim(const Json& req, Json& out, CmdCtx& ctx) { ... return 0; }
//     MAPTOOL_COMMAND("sim", runSim, "one line of help");
#pragma once
#include <cstdio>
#include <istream>
#include <string>
#include <vector>
#include "json.h"
#include "common/qmath.h"
#include "game/map.h"

// ---- commands ------------------------------------------------------------------------------------------------
struct CmdCtx {
    std::vector<std::string> flags;   // "--stream" and the like, as given on the command line
    bool stream = false;              // --stream: the command writes NDJSON lines itself (emit), `out` is ignored
    std::istream* control = nullptr;  // --stream: the rest of stdin, one control line each (nullptr: none)
    bool hasFlag(const std::string& f) const { for (auto& x : flags) if (x == f) return true; return false; }
    void emit(const Json& line) const;   // one NDJSON line on stdout, flushed
};
// Fills `out` (the answer object) and returns the exit code (0 ok; 1 with out = {"error": ...}).
using CmdFn = int (*)(const Json& req, Json& out, CmdCtx& ctx);
struct Command { const char* name; CmdFn run; const char* help; };
const std::vector<Command>& maptoolCommands();
struct CommandRegistrar { CommandRegistrar(const char* name, CmdFn fn, const char* help); };
#define MAPTOOL_CAT2(a, b) a##b
#define MAPTOOL_CAT(a, b) MAPTOOL_CAT2(a, b)
#define MAPTOOL_COMMAND(name, fn, help) static CommandRegistrar MAPTOOL_CAT(maptoolReg_, __LINE__)(name, fn, help)

int cmdError(Json& out, const std::string& msg);   // out = {"error": msg}, returns 1
double maptoolNow();                               // seconds (steady clock), for the "ms" timings answers carry

// ---- JSON <-> game values ------------------------------------------------------------------------------------
Json jvec(const Vec3& v);
Json jbox(const AABB& b);                  // {"min":[..],"max":[..]}
Vec3 toVec(const Json& j, Vec3 def = Vec3());
AABB toBox(const Json& j);                 // {"min","max"} (corners in any order)
const char* spawnTeamName(int team, char* buf);   // buf: 16 chars. a, b, any, race, att<N>, def<N>
int spawnTeamFromName(const std::string& s);       // -1: none
int modeFromJson(const Json& j, int def);          // "vs" / "ffa" / ... or a number; def when missing / unknown

// ---- maps ------------------------------------------------------------------------------------------------------
// A request's map, from (first found) "text" (.qmap text), "file" (a path), "name" (a map the game finds by name:
// your maps, then the shipped ones) or "map" (the JSON form: serialized, then parsed). Parsed with the game's own
// Map::parse, so every grid, ladder and group is built as the game builds it. "mapName" names a text / JSON map.
bool loadRequestMap(const Json& req, Map& m, std::string& err);
Json mapToJson(const Map& m);                       // the JSON form (INTERFACE.md)
bool mapFromJson(const Json& j, Map& m, std::string& err);   // the fields only (no grid): serialize() it, then parse
Json playerJson();                                  // the hull numbers (gamedefs.h / pmove)
Json customKindsJson();                             // custom.cpp: the custom test kinds and their fields (help)
