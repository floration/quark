// libFuzzer harness: what a game server decodes from the network and from its players (Server::handlePacket, the
// string commands: chat, votes, names...). One server runs for the whole fuzzing session (FFA on Bastion, no
// internet: QUARK_MASTER / QUARK_PUBLIC_IP off, user data in a temp folder) with one player that connected the
// real way (challenge cookie, then accept). Run it from the build folder (it loads maps/bastion.qmap).
// Input: records of [u8 kind][u8 len][len bytes], each one of
//   kind 0: a packet from the player: header + type (first byte) + the player's nonce + the rest (PKT_INPUT, ...)
//   kind 1: raw bytes from the player's address (headers and nonces as the fuzzer likes)
//   kind 2: a string command from the player (chat, vote, name, ...), as if it came in a PKT_INPUT
//   kind 3: raw bytes from another address (first 2 bytes pick it): connects, info / probe requests, spoofs
// then one server frame. The player reconnects whenever it's gone (kicked, timed out, disconnected).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>
#include "common/net.h"
#include "game/map.h"
#include "game/protocol.h"
#include "game/server.h"

namespace {
Server* g_srv = nullptr;
UdpSocket g_sock;
NetAddr g_me, g_server;
uint32_t g_nonce = 0x5157u;
uint32_t g_token[2] = {0, 0};   // the reconnect token from the last accept: a reconnect takes back the held slot
int g_id = -1;

// Waits (running the server) for a packet of `type` to our socket; its body after the header in `out`.
bool await(uint8_t type, std::vector<uint8_t>& out) {
    double until = timeNow() + 0.5;
    uint8_t buf[2048];
    while (timeNow() < until) {
        g_srv->frame();
        NetAddr from;
        int n;
        while ((n = g_sock.recv(from, buf, sizeof buf)) > 0) {
            Reader r(buf, n);
            if (r.u32() != kProtocolMagic) continue;
            uint8_t t = r.u8();
            if (t == PKT_REJECT && getenv("QUARK_FUZZ_DEBUG")) fprintf(stderr, "rejected: %s\n", r.str().c_str());
            if (t != type) continue;
            out.assign(buf + 5, buf + n);
            return true;
        }
        sleepMs(1);
    }
    return false;
}

void sendConnect(uint32_t cookie) {
    uint8_t out[128];
    Writer w(out, sizeof out);
    writeHeader(w, PKT_CONNECT);
    w.u16(kProtocolVersion); w.u32(g_nonce); w.u32(cookie); w.str("fuzzer");
    w.u32(g_token[0]); w.u32(g_token[1]); w.str(""); w.str(""); w.u32(g_server.ip); w.u16(g_server.port);   // (as the game)
    g_srv->handlePacket(g_me, out, w.size());
}

bool connectPlayer() {
    static bool debug = getenv("QUARK_FUZZ_DEBUG") != nullptr;
    double t0 = timeNow();
    struct Log { bool on; double t0; ~Log() { if (on) fprintf(stderr, "reconnect %.0f ms\n", (timeNow() - t0) * 1000); } } log{debug, t0};
    g_nonce++;
    std::vector<uint8_t> body;
    sendConnect(0);
    if (!await(PKT_CHALLENGE, body) || body.size() < 8) return false;   // (a reject: printed with QUARK_FUZZ_DEBUG)
    Reader r(body.data(), (int)body.size());
    r.u32();
    sendConnect(r.u32());
    if (!await(PKT_ACCEPT, body) || body.size() < 5) return false;
    Reader a(body.data(), (int)body.size());
    if (a.u32() != g_nonce) return false;
    g_id = a.u8();
    a.str(); for (int k = 0; k < 32; k++) a.u8(); a.u8();   // map, map id (sha256), mode
    g_token[0] = a.u32(); g_token[1] = a.u32();
    return !a.bad();
}
} // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    setenv("QUARK_MASTER", "off", 1);
    setenv("QUARK_PUBLIC_IP", "off", 1);
    std::string dir = (std::filesystem::temp_directory_path() / ("quark_fuzz_server_" + std::to_string(getpid()))).string();
    setUserDataDir(dir, false);
    if (!g_sock.open(0)) { fprintf(stderr, "no UDP socket\n"); exit(1); }
    g_srv = new Server;
    g_srv->setLog([](const std::string&) {});
    g_srv->voteCooldown_ = 0;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.maxPlayers = 8;
    std::string err;
    for (int tries = 0; tries < 20; tries++) {
        cfg.port = (uint16_t)(30000 + (getpid() * 7 + tries * 131) % 20000);
        if (g_srv->start(cfg, &err)) break;
    }
    if (!g_srv->port()) { fprintf(stderr, "server start: %s (run from the build folder: it needs maps/)\n", err.c_str()); exit(1); }
    g_me = NetAddr{0x7F000001u, g_sock.localPort()};
    g_server = NetAddr{0x7F000001u, g_srv->port()};
    if (!connectPlayer()) { fprintf(stderr, "the fuzzer's player couldn't connect\n"); exit(1); }
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if ((g_srv->playerCount() == 0 || g_id < 0 || g_srv->away(g_id)) && !connectPlayer()) return 0;
    size_t i = 0;
    std::vector<uint8_t> pkt;
    while (i + 2 <= size) {
        uint8_t kind = data[i] & 3, len = data[i + 1];
        i += 2;
        if (len > size - i) len = (uint8_t)(size - i);
        const uint8_t* p = data + i;
        i += len;
        switch (kind) {
            case 0: {
                if (len < 1) break;
                pkt.assign(5 + 4, 0);
                Writer w(pkt.data(), (int)pkt.size());
                writeHeader(w, (PacketType)p[0]);
                w.u32(g_nonce);
                pkt.insert(pkt.end(), p + 1, p + len);
                g_srv->handlePacket(g_me, pkt.data(), (int)pkt.size());
                break;
            }
            case 1: g_srv->handlePacket(g_me, p, len); break;
            case 2:
                g_srv->handleStringCmdForTest(g_id, std::string((const char*)p, len));
                break;
            case 3: {
                if (len < 2) break;
                NetAddr other{0x0A000000u | p[0], (uint16_t)(1024 + p[1])};
                g_srv->handlePacket(other, p + 2, len - 2);
                break;
            }
        }
        if (g_srv->playerCount() == 0 || g_srv->away(g_id)) break;   // (kicked or gone: the next input reconnects)
    }
    g_srv->frame();
    NetAddr from;
    uint8_t buf[2048];
    while (g_sock.recv(from, buf, sizeof buf) > 0) {}   // (drain what the server sent us)
    return 0;
}
