// quark_maptool: the map editor's native helper. `quark_maptool <command> [--flags] [request.json]`; the request
// (JSON) from the file or stdin, the answer (JSON) on stdout. Commands: native/README.md.
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include "maptool.h"
#include "common/net.h"
#include "common/version.h"

static int runHelp(const Json&, Json& out, CmdCtx&) {
    out = Json::object();
    out.set("tool", "quark_maptool");
    out.set("version", kQuarkVersion);
    Json cmds = Json::array();
    for (auto& c : maptoolCommands()) { Json o = Json::object(); o.set("name", c.name); o.set("help", c.help); cmds.push(std::move(o)); }
    out.set("commands", std::move(cmds));
    out.set("customKinds", customKindsJson());
    out.set("player", playerJson());
    return 0;
}
MAPTOOL_COMMAND("help", runHelp, "this list: commands, custom test kinds, the player hull");

extern std::string g_maptoolExe;   // measure.cpp (the movement envelope's cache key)

int main(int argc, char** argv) {
    netInit();
    {
        std::error_code ec;
        auto p = std::filesystem::canonical(argv[0], ec);
#ifndef _WIN32
        if (ec) p = std::filesystem::canonical("/proc/self/exe", ec);
#else
        if (ec) p = std::filesystem::canonical(std::string(argv[0]) + ".exe", ec);
#endif
        if (!ec) g_maptoolExe = p.string();
        setExecutableDir(argv[0]);   // (the game's own lookups: the maps folder next to it)
    }
    std::ios::sync_with_stdio(false);
    if (argc < 2 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) {
        fprintf(stderr, "usage: quark_maptool <command> [--stream] [request.json]   (request on stdin otherwise)\ncommands:");
        for (auto& c : maptoolCommands()) fprintf(stderr, " %s", c.name);
        fprintf(stderr, "\n");
        return argc < 2 ? 2 : 0;
    }
    CmdCtx ctx;
    std::string file;
    for (int i = 2; i < argc; i++) {
        if (!strncmp(argv[i], "--", 2)) ctx.flags.push_back(argv[i]);
        else file = argv[i];
    }
    ctx.stream = ctx.hasFlag("--stream");
    const Command* cmd = nullptr;
    for (auto& c : maptoolCommands()) if (!strcmp(c.name, argv[1])) cmd = &c;
    Json out;
    auto finish = [&](int rc) {
        if (ctx.stream) { if (rc) { Json e = out; e.set("type", "error"); ctx.emit(e); } return rc; }
        std::string s = out.dump();
        s += '\n';
        fwrite(s.data(), 1, s.size(), stdout);
        fflush(stdout);
        return rc;
    };
    if (!cmd) return finish(cmdError(out, std::string("unknown command '") + argv[1] + "' (quark_maptool help lists them)"));
    std::string text;
    if (!file.empty()) {
        std::ifstream f(file, std::ios::binary);
        if (!f) return finish(cmdError(out, "can't read " + file));
        std::ostringstream ss; ss << f.rdbuf(); text = ss.str();
        if (ctx.stream) ctx.control = &std::cin;
    } else if (ctx.stream) {   // the request is stdin's first line; the lines after it are controls
        std::getline(std::cin, text);
        ctx.control = &std::cin;
    } else text.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    Json req = Json::object();
    bool blank = text.find_first_not_of(" \t\r\n") == std::string::npos;
    std::string err;
    if (!blank && !Json::parse(text, req, &err)) return finish(cmdError(out, "request isn't JSON: " + err));
    if (!req.isObj()) return finish(cmdError(out, "request must be a JSON object"));
    int rc = 1;
    try { rc = cmd->run(req, out, ctx); }
    catch (const std::exception& e) { rc = cmdError(out, std::string("internal error: ") + e.what()); }
    if (rc && !out["error"].isStr()) cmdError(out, "failed");
    return finish(rc);
}
