// Internet server list ("master server") on a Cloudflare Worker (tools/master-worker.js).
// Public servers announce ip:port every minute; the Join screen fetches the list and then asks each
// server directly over UDP (so only reachable servers show up, with real ping and player counts).
// Everything here blocks on HTTP and runs on background threads.
#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The list's base URL: QUARK_MASTER env var if set, else the one published on the website
// (site/master.txt), so it can change without a new release. Empty = no internet list.
std::string masterUrl();
// ip:port of every public server running this version.
bool fetchServerList(std::vector<std::string>& out, std::string* err = nullptr);
// This machine's public IPv4 (empty if unknown).
std::string publicIpv4();

class MasterAnnouncer {
public:
    ~MasterAnnouncer() { stop(); }
    void start(uint16_t port);
    void stop();
    std::string status();
private:
    void run();
    uint16_t port_ = 0;
    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool quit_ = false;
    std::string status_;
};
