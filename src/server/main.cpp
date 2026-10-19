// quark_server: standalone dedicated server.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <deque>
#include "../game/server.h"
#include "../game/gamemodes.h"

static void usage() {
    printf(
        "quark_server [options]\n"
        "  --port N         UDP port (default %d)\n"
        "  --map NAME       map from maps/ (default arena)\n"
        "  --mode MODE      vs | bomb | race | koth | payload | ffa\n"
        "  --name TEXT      server name shown in the browser\n"
        "  --max N          max players (default 8, the maximum)\n"
        "  --rounds N       rounds to win (vs/bomb/race)\n"
        "  --timelimit S    ffa time limit in seconds\n"
        "  --scorelimit N   koth points / ffa kills to win\n"
        "  --upnp           ask the router to forward the port automatically\n"
        "  --tickrate N     highest tick rate to try: 1000 (default), 500, 250 or 125.\n"
        "                   During warmup it steps down automatically if the host can't keep up.\n"
        "console commands: map <name> [mode], mode <name>, forcestart, restart, kick <id>, say <text>, status, quit\n",
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
