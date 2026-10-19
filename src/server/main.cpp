// quark_server: standalone dedicated server.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <deque>
#include <sstream>
#include "../game/server.h"
#include "../game/gamemodes.h"

static void usage() {
    printf(
        "quark_server [options]\n"
        "  --port N         UDP port (default %d)\n"
        "  --map NAME       map from maps/ (default arena)\n"
        "  --mode MODE      vs | bomb | race | koth | payload | npayload | ffa | duel | 1v1 | tourney\n"
        "  --name TEXT      server name shown in the browser\n"
        "  --max N          max players (default 8, up to 32; over 16 starts at a lower tick rate)\n"
        "  --rounds N       rounds to win (vs/bomb/race)\n"
        "  --timelimit S    ffa time limit in seconds\n"
        "  --scorelimit N   koth points / ffa kills to win\n"
        "  --upnp           ask the router to forward the port automatically\n"
        "  --ranked         rated matches (OpenSkill): every player's game reports the result; no bots\n"
        "  --tickrate N     highest tick rate to try: 1000 (default), 500, 250 or 125.\n"
        "                   During warmup it steps down automatically if the host can't keep up.\n"
        "  --bots N         play-vs-bots server: N bots (team modes: on team B; humans join team A)\n"
        "  --allies N       team modes with --bots: N bots on the humans' team\n"
        "  --botskill N     0 beginner, 1 intermediate (default), 2 advanced, 3 expert\n"
        "  --log LEVEL      game log to logs/: simple (deaths, map changes, chat, wins),\n"
        "                   verbose (+ 125 Hz gameplay), extreme (every tick, everything)\n"
        "  --logfile PATH   write the game log here instead\n"
        "  --teamsize N     duel: 1 (1v1, default) or 2 (2v2)\n"
        "  --public         list this server on the internet server list (players see it under Join)\n"
        "  --variant NAME   1v1 / tourney ruleset: vampire, classic, quirky, hitscan, instagib, boomers, blades, moon\n"
        "  --tourney-maps a,b      maps tourney matches rotate through\n"
        "  --tourney-modes a,b     1v1 rulesets tourney matches rotate through\n"
        "  --nobalance      don't add bots to even out uneven teams (players can also vote it off)\n"
        "console commands: map <name> [mode], mode <name>, forcestart, restart, kick <id>, say <text>, status,\n"
        "                  addbot [a|b] [0-3], kickbots, quit\n",
        kDefaultPort);
}

int main(int argc, char** argv) {
    setExecutableDir(argv[0]);
    ServerConfig cfg;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--port") cfg.port = (uint16_t)atoi(next().c_str());
        else if (a == "--map") cfg.map = next();
        else if (a == "--mode") {
            std::string m = next();
            cfg.mode = modeFromName(m.c_str());
            if (cfg.mode < 0) { printf("unknown mode %s\n", m.c_str()); return 1; }
        }
        else if (a == "--name") cfg.name = next();
        else if (a == "--max") cfg.maxPlayers = atoi(next().c_str());
        else if (a == "--rounds") cfg.roundsToWin = atoi(next().c_str());
        else if (a == "--timelimit") cfg.timeLimit = atoi(next().c_str());
        else if (a == "--scorelimit") cfg.scoreLimit = atoi(next().c_str());
        else if (a == "--upnp") cfg.upnp = true;
        else if (a == "--tickrate") cfg.maxTickRate = atoi(next().c_str());
        else if (a == "--bots") { cfg.vsBots = true; cfg.botsEnemy = atoi(next().c_str()); }
        else if (a == "--allies") cfg.botsAlly = atoi(next().c_str());
        else if (a == "--botskill") cfg.botSkill = cfg.balanceBotSkill = atoi(next().c_str());
        else if (a == "--log") {
            std::string l = next();
            cfg.logLevel = logLevelFromName(l);
            if (cfg.logLevel < 0) { printf("unknown log level %s (simple | verbose | extreme)\n", l.c_str()); return 1; }
        }
        else if (a == "--logfile") { cfg.logFile = next(); if (!cfg.logLevel) cfg.logLevel = LOG_SIMPLE; }
        else if (a == "--teamsize") cfg.duelTeamSize = atoi(next().c_str());
        else if (a == "--public") cfg.publicList = true;
        else if (a == "--variant") {
            std::string v = next();
            cfg.arenaVariant = arenaVariantFromName(v.c_str());
            if (cfg.arenaVariant < 0) { printf("unknown 1v1 ruleset %s\n", v.c_str()); return 1; }
        }
        else if (a == "--tourney-maps" || a == "--tourney-modes") {
            std::stringstream ls(next());
            std::string it;
            while (std::getline(ls, it, ','))
                if (a == "--tourney-maps") cfg.tourneyMaps.push_back(it);
                else cfg.tourneyVariants.push_back(std::max(0, arenaVariantFromName(it.c_str())));
        }
        else if (a == "--nobalance") cfg.balanceBots = false;
        else if (a == "--ranked") cfg.ranked = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { printf("unknown option %s\n", a.c_str()); usage(); return 1; }
    }
    netInit();
    Server server;
    std::string err;
    if (!server.start(cfg, &err)) {
        printf("failed to start: %s\n", err.c_str());
        return 1;
    }
    printf("quark server '%s' running. Players connect to <your public IP>:%u\n", cfg.name.c_str(), server.port());
    if (!cfg.upnp) printf("tip: forward UDP port %u on your router, or run with --upnp\n", server.port());

    std::atomic<bool> quit{false};
    std::mutex mu;
    std::deque<std::string> lines;
    std::thread input([&] {
        std::string line;
        while (std::getline(std::cin, line)) {
            std::lock_guard<std::mutex> l(mu);
            lines.push_back(line);
            if (line == "quit" || line == "exit") break;
        }
    });
    input.detach();

    while (!quit) {
        double wait = server.frame();
        {
            std::lock_guard<std::mutex> l(mu);
            while (!lines.empty()) {
                std::string line = lines.front();
                lines.pop_front();
                if (line == "quit" || line == "exit") quit = true;
                else if (!line.empty()) server.command(line);
            }
        }
        server.idle(wait);
    }
    server.stop();
    netShutdown();
    return 0;
}
