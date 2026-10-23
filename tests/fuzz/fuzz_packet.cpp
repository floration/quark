// libFuzzer harness: what a client decodes from the network (src/game/protocol.cpp). Build with -DQUARK_FUZZ=ON
// (clang; how to run them: the QUARK_FUZZ comment in CMakeLists.txt).
// Input: byte 0 picks the decoder, the rest is the packet body after the header and nonce:
//   0  a full snapshot (client header, world part with no base, events), as netclient.cpp PKT_SNAPSHOT
//   1  u16 n, then a full snapshot of n bytes, then a delta snapshot decoded against it
//   2  user commands (the server's PKT_INPUT reader), until the input runs out
//   3  EV_BRACKET text (parseBracket)
// QUARK_FUZZ_SEEDS=<dir>: writes a seed corpus there at start (real encoded snapshots and commands) and exits.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include "game/protocol.h"

namespace {
void decodeSnapshot(Reader& r, Snapshot& s, const Snapshot* base) {
    if (!readSnapshotClient(r, s)) return;
    if (s.baseTick && !base) return;   // (the client drops a delta it has no base for)
    if (!readSnapshotWorld(r, s, s.baseTick ? base : nullptr)) return;
    readSnapshotEvents(r, s);
}

Snapshot sample(uint32_t tick) {
    Snapshot s;
    s.tick = tick; s.timeMs = tick * 2; s.ackCmdSeq = tick - 3; s.clientId = 1;
    s.self.pos = {1, 2, 3}; s.self.vel = {4, 0, -2}; s.selfHealth = 90; s.selfWeapon = 2;
    s.ms.mode = MODE_BOMB; s.ms.phase = 1; s.ms.timeLeft = 61.5f; s.ms.scoreA = 2; s.ms.bombState = BOMB_CARRIED;
    for (int i = 0; i < 4; i++) {
        PlayerSnap p;
        p.id = (uint8_t)i; p.flags = PS_ALIVE | PS_CONNECTED | (i & 1 ? PS_TEAM_B : 0);
        p.pos = {(float)i * 3 + tick % 7, 0, -(float)i}; p.yaw = 0.5f * i; p.health = (uint8_t)(100 - i * 10);
        p.kills = (int16_t)i; p.ping = (uint16_t)(20 + i);
        s.players.push_back(p);
    }
    ProjectileSnap pr;
    pr.id = 7; pr.type = 1; pr.owner = 2; pr.pos = {0, 1, 0}; pr.vel = {30, 0, 0};
    s.projectiles.push_back(pr);
    GameEvent e;
    e.id = tick; e.type = EV_KILL; e.a = 1; e.b = 2; e.c = 3;
    s.events.push_back(e);
    e.id = tick + 1; e.type = EV_BRACKET; e.text = "0|1|2|3\n-2|-2\n@0,1";
    s.events.push_back(e);
    return s;
}

std::string encode(Snapshot& s, const Snapshot* base) {
    uint8_t buf[kMaxPacket * 4];
    Writer w(buf, sizeof buf);
    if (base) s.baseTick = base->tick;
    writeSnapshotClient(w, s);
    writeSnapshotWorld(w, s, base);
    writeSnapshotEvents(w, s.events);
    return std::string((const char*)buf, w.size());
}

void writeSeeds(const std::string& dir) {
    auto put = [&](const std::string& name, const std::string& bytes) { std::ofstream(dir + "/" + name, std::ios::binary) << bytes; };
    Snapshot a = sample(100), b = sample(101);
    std::string full = encode(a, nullptr), delta = encode(b, &a);
    put("full", std::string(1, '\0') + full);
    std::string pair(1, '\1');
    pair += (char)(full.size() & 255);
    pair += (char)(full.size() >> 8);
    put("delta", pair + full + delta);
    uint8_t buf[256];
    Writer w(buf, sizeof buf);
    for (uint32_t i = 0; i < 4; i++) {
        UserCmd c;
        c.seq = 50 + i; c.viewTick = 99; c.forward = 127; c.side = -127; c.buttons = 5; c.msec = 2; c.weapon = 1; c.yaw = 1.5f; c.pitch = -0.2f;
        writeCmd(w, c);
    }
    put("cmds", std::string(1, '\2') + std::string((const char*)buf, w.size()));
    put("bracket", std::string(1, '\3') + "0|1|2|3|-1|5|6|7\n-2|-2|-2|-2\n-2|-2\n-2\n@1,2");
}
} // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    if (const char* d = getenv("QUARK_FUZZ_SEEDS")) { writeSeeds(d); exit(0); }
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 1 || size > 1 << 16) return 0;
    const uint8_t* p = data + 1;
    int n = (int)size - 1;
    switch (data[0] & 3) {
        case 0: {
            Reader r(p, n);
            Snapshot s;
            decodeSnapshot(r, s, nullptr);
            break;
        }
        case 1: {
            if (n < 2) break;
            int first = p[0] | p[1] << 8;
            if (first > n - 2) break;
            Reader ra(p + 2, first);
            Snapshot a;
            decodeSnapshot(ra, a, nullptr);
            Reader rb(p + 2 + first, n - 2 - first);
            Snapshot b;
            decodeSnapshot(rb, b, &a);
            break;
        }
        case 2: {
            Reader r(p, n);
            while (r.remaining() > 0 && !r.bad()) {
                UserCmd c = readCmd(r);
                if (!(c.pitch >= -1.55f && c.pitch <= 1.55f)) abort();   // readCmd clamps the pitch, NaN included
            }
            break;
        }
        case 3: {
            std::vector<std::vector<int>> rounds;
            int round = 0, match = 0;
            if (parseBracket(std::string((const char*)p, n), rounds, round, match) && !rounds.empty()) {
                // the promise in protocol.h: round and match are clamped (round == size: the bracket is over)
                if (round < 0 || round > (int)rounds.size() || match < 0 || match > 2 * kMaxPlayers) abort();
            }
            break;
        }
    }
    return 0;
}
