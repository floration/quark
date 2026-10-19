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
    if (const char* e = getenv("QUARK_MASTER")) return trim(e);
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

void MasterAnnouncer::start(uint16_t port) {
    stop();
    port_ = port;
    quit_ = false;
    thread_ = std::thread([this] { run(); });
}

void MasterAnnouncer::stop() {
    if (!thread_.joinable()) return;
    { std::lock_guard<std::mutex> l(mu_); quit_ = true; }
    cv_.notify_all();
    thread_.join();
}

std::string MasterAnnouncer::status() { std::lock_guard<std::mutex> l(mu_); return status_; }

void MasterAnnouncer::run() {
    std::string ip, base;
    for (;;) {
        if (base.empty()) base = masterUrl();
        if (ip.empty()) { std::string b; if (httpGetText(kIpv4Echo, b) && isIpv4(trim(b))) ip = trim(b); }
        std::string st;
        if (base.empty()) st = "internet list: not configured";
        else {
            std::string body, err;
            std::string url = base + "/announce?port=" + std::to_string(port_) + "&v=" QUARK_VERSION + (ip.empty() ? "" : "&ip=" + ip);
            st = httpGetText(url, body, 5000, &err) ? "listed on the internet server list" : "internet list: " + err;
        }
        std::unique_lock<std::mutex> l(mu_);
        status_ = st;
        if (cv_.wait_for(l, std::chrono::seconds(60), [this] { return quit_; })) break;
    }
    if (!base.empty()) { // leave the list right away instead of timing out
        std::string body;
        httpGetText(base + "/remove?port=" + std::to_string(port_) + (ip.empty() ? "" : "&ip=" + ip), body, 2000);
    }
}
