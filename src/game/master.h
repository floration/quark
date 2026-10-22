// Internet server list ("master server") on a Cloudflare Worker (tools/master-worker.js).
// Public servers announce ip:port every minute; the Join screen fetches the list and then asks each
// server directly over UDP (so only reachable servers show up, with real ping and player counts).
// Everything here blocks on HTTP and runs on background threads.
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The list's (and ranked's) base URL: QUARK_MASTER env var if set (tests, local copies), else the address built into
// the game, unless the website publishes a master.txt signed with the release key (master.txt.sig): then that one.
// Empty = no internet list.
std::string masterUrl();
// The URL in a master.txt body if sigHex is a valid release-key (pubHex) signature of exactly that body.
bool signedMasterUrl(const std::string& body, const std::string& sigHex, const std::string& pubHex, std::string* url);
// ip:port of every public server running this version.
bool fetchServerList(std::vector<std::string>& out, std::string* err = nullptr);
// This machine's public IPv4 (empty if unknown).
std::string publicIpv4();

class MasterAnnouncer {
public:
    ~MasterAnnouncer() { stop(); }
    // listed = false: still announces (that's how the server gets the token that proves it owns ip:port, needed for
    // ranked tickets and race runs) but the list never shows it.
    void start(uint16_t port, bool listed = true);
    void stop();
    std::string status();
    // Requests to the list made as this server (ranked match tickets and results, race runs): queued, sent in
    // order on the announcer thread once it holds a token (port, token and ip are appended to the form). The
    // replies (or "!" + the error) come back through takeReplies(), with the key given here. Never blocks.
    void post(const std::string& key, const std::string& path, const std::string& form);
    bool hasReplies() const { return hasReplies_.load(std::memory_order_relaxed); }
    std::vector<std::pair<std::string, std::string>> takeReplies();
private:
    void run();
    struct Post { std::string key, path, form; };
    std::deque<Post> posts_;
    std::vector<std::pair<std::string, std::string>> replies_;
    std::atomic<bool> hasReplies_{false};
    bool haveToken_ = false;
    bool listed_ = true;
    uint16_t port_ = 0;
    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool quit_ = false;
    std::string status_;
};
