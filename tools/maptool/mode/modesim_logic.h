// The mode logic language (src/game/modelogic.h) for modesim_cmd.cpp, behind plain types.
//
// Why a separate file: src/game/modelogic.h and src/game/gamemodes.h (since G1's rules refactor) both declare
// `enum ModeSlot` / MSLOT_*, so no translation unit can include both, and modesim_cmd.cpp needs the server
// (server.h, gamemodes.h). modesim_logic.cpp includes modelogic.h only. Once src/game has one ModeSlot, this can fold
// back into modesim_cmd.cpp.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ModeCompile {
    bool ok = false;             // compiled, and no extra error was added
    std::string name, hash, label;   // label: name#hash8 (GRAMMAR.md 9)
    int base = -1;               // MODE_* it plays as
    std::string diagJson;        // quark_tests checkmode's JSON (modeDiagJson), with the extra errors
    std::vector<std::string> needs;   // map features the logic reads: hill, sites, cart, points, checkpoints
};
// Compile a .qmode (or a bare logic text) as the game does. base: MODE_* or -1 (the text's mode= / base line).
// extraErrors (line 0) are added to the errors, and make the answer not ok (checkmode --map's missing features).
ModeCompile compileModeText(const std::string& text, int base, const std::vector<std::string>& extraErrors = {});
int modeBaseFromName(const std::string& name);   // the logic's mode names ("arena" = 1v1, "1v1" too); -1
const char* modeSlotNameOf(int slot);            // modelogic.h's modeSlotName (ModeSlot as an int: "tick", "kill", ...)
