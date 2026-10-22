#include "master.h"
#include <cstdlib>
#include "../common/http.h"

namespace {
const char* kMasterUrlFile = "https://floration.github.io/quark/master.txt";
const char* kIpv4Echo = "https://api.ipify.org"; // IPv4-only: hosts on IPv6 still announce their v4 address

std::string trim(std::string s) {
    while (!s.empty() && (unsigned char)s.back() <= ' ') s.pop_back();
    size_t i = 0;
    while (i < s.size() && (unsigned char)s[i] <= ' ') i++;
    return s.substr(i);
}
bool isIpv4(const std::string& s) {
    int dots = 0, digits = 0;
    for (char c : s) {
        if (c == '.') { if (!digits) return false; dots++; digits = 0; }
        else if (c >= '0' && c <= '9') { if (++digits > 3) return false; }
        else return false;
    }
    return dots == 3 && digits > 0;
}
}

std::string masterUrl() {
    static std::mutex mu;
    static std::string cached;
    static bool fetched = false;
    std::lock_guard<std::mutex> l(mu);
    if (const char* e = getenv("QUARK_MASTER")) { std::string u = trim(e); return u.rfind("http", 0) == 0 ? u : ""; } // ("off": no list)
    if (!fetched) {
        std::string body;
        if (httpGetText(kMasterUrlFile, body)) {
            body = trim(body);
            if (body.rfind("https://", 0) == 0 && body.find_first_of(" '\"\\\n") == std::string::npos) cached = body;
            while (!cached.empty() && cached.back() == '/') cached.pop_back();
            fetched = true;
        }
    }
    return cached;
}

bool fetchServerList(std::vector<std::string>& out, std::string* err) {
    out.clear();
    std::string base = masterUrl();
    if (base.empty()) { if (err) *err = "no internet server list configured"; return false; }
    std::string body;
    if (!httpGetText(base + "/list?v=" QUARK_VERSION, body, 5000, err)) return false;
    size_t i = 0;
    while (i < body.size() && out.size() < 256) {
        size_t j = body.find('\n', i);
        if (j == std::string::npos) j = body.size();
        std::string line = trim(body.substr(i, j - i));
        if (!line.empty() && line.size() < 32) out.push_back(line);
        i = j + 1;
    }
    return true;
}

std::string publicIpv4() {
    std::string b;
    if (httpGetText(kIpv4Echo, b, 4000) && isIpv4(trim(b))) return trim(b);
    return "";
}

void MasterAnnouncer::start(uint16_t port, bool listed) {
    stop();
    port_ = port;
    listed_ = listed;
    quit_ = false;
    haveToken_ = false;
    thread_ = std::thread([this] { run(); });
}

void MasterAnnouncer::stop() {
    if (!thread_.joinable()) return;
    { std::lock_guard<std::mutex> l(mu_); quit_ = true; }
    cv_.notify_all();
    thread_.join();
}

std::string MasterAnnouncer::status() { std::lock_guard<std::mutex> l(mu_); return status_; }

void MasterAnnouncer::post(const std::string& key, const std::string& path, const std::string& form) {
    { std::lock_guard<std::mutex> l(mu_); if (posts_.size() < 64) posts_.push_back({key, path, form}); }
    cv_.notify_all();
}

std::vector<std::pair<std::string, std::string>> MasterAnnouncer::takeReplies() {
    std::lock_guard<std::mutex> l(mu_);
    std::vector<std::pair<std::string, std::string>> r;
    r.swap(replies_);
    hasReplies_ = false;
    return r;
}

void MasterAnnouncer::run() {
    std::string ip, base, token; // token: issued by the list on our first announce, proves we own ip:port
    auto nextAnnounce = std::chrono::steady_clock::now();
    for (;;) {
        if (std::chrono::steady_clock::now() >= nextAnnounce) {
            nextAnnounce = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            if (base.empty()) base = masterUrl();
            if (ip.empty() && !base.empty()) ip = publicIpv4();
            std::string st;
            if (base.empty()) st = "internet list: not configured";
            else {
                std::string body, err;
                std::string url = base + "/announce?port=" + std::to_string(port_) + "&v=" QUARK_VERSION + (ip.empty() ? "" : "&ip=" + ip) +
                                  (token.empty() ? "" : "&token=" + token) + (listed_ ? "" : "&hidden=1");
                bool ok = httpGetText(url, body, 5000, &err);
                st = !ok ? "internet list: " + err : listed_ ? "listed on the internet server list" : "registered with the ranking server (not listed)";
                if (ok && body.rfind("ok ", 0) == 0) {
                    std::string t = trim(body.substr(3));
                    if (t.size() == 32 && t.find_first_not_of("0123456789abcdef") == std::string::npos) token = t;
                }
            }
            std::lock_guard<std::mutex> l(mu_);
            status_ = st;
            haveToken_ = !token.empty() || base.empty(); // (no list at all: the queued posts fail right away)
        }
        for (;;) { // queued server requests, in order
            Post p;
            {
                std::lock_guard<std::mutex> l(mu_);
                if (quit_ || posts_.empty()) break;
                if (!base.empty() && token.empty()) break; // wait for a token (the next announce)
                p = posts_.front();
                posts_.pop_front();
            }
            std::string out, err;
            bool ok = !base.empty() && httpPostForm(base + p.path, p.form + "&port=" + std::to_string(port_) + "&token=" + token + (ip.empty() ? "" : "&ip=" + ip), out, 8000, &err);
            std::lock_guard<std::mutex> l(mu_);
            replies_.push_back({p.key, ok ? trim(out) : "!" + (base.empty() ? std::string("no ranking server configured") : err)});
            hasReplies_ = true;
        }
        std::unique_lock<std::mutex> l(mu_);
        if (cv_.wait_until(l, nextAnnounce, [this] { return quit_ || (!posts_.empty() && haveToken_); }) && quit_) break;
    }
    if (!base.empty() && listed_) { // leave the list right away instead of timing out
        std::string body;
        httpGetText(base + "/remove?port=" + std::to_string(port_) + (ip.empty() ? "" : "&ip=" + ip) +
                    (token.empty() ? "" : "&token=" + token), body, 2000);
    }
}
