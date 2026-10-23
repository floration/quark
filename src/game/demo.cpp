#include "demo.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include "gamedefs.h"
#include "map.h"
#include "protocol.h"
#include "../common/version.h"

namespace fs = std::filesystem;

namespace demo {

namespace {
std::mutex g_verMu;
std::map<uint16_t, std::vector<std::string>> g_seen; // protocol -> versions that ran with it (versions.txt)

std::string slurp(const fs::path& p, uint64_t maxBytes) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return "";
    std::streamoff n = f.tellg();
    if (n < 0 || (uint64_t)n > maxBytes) return "";
    f.seekg(0);
    std::string s((size_t)n, '\0');
    if (n && !f.read(&s[0], n)) return "";
    return s;
}
int verCmp(const std::string& a, const std::string& b) { // numeric x.y.z (a suffix after the numbers compares as text)
    unsigned x[3] = {}, y[3] = {};
    sscanf(a.c_str(), "%u.%u.%u", &x[0], &x[1], &x[2]);
    sscanf(b.c_str(), "%u.%u.%u", &y[0], &y[1], &y[2]);
    for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return a < b ? -1 : a > b ? 1 : 0;
}
std::string folderSafe(std::string s) {
    for (char& c : s) if (!(isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_')) c = '_';
    return s.empty() ? "unknown" : s;
}
void appendLog(const fs::path& dir, const std::string& line) {
    std::error_code ec;
    fs::create_directories(dir / "old", ec);
    char stamp[32] = "";
    std::time_t t = std::time(nullptr);
    if (const std::tm* tm = std::localtime(&t)) std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", tm);
    std::ofstream(dir / "old" / "convert.log", std::ios::app) << stamp << " " << line << "\n";
}
} // namespace

void loadVersions(const std::string& file) {
    std::ifstream f(fs::u8path(file));
    std::map<uint16_t, std::vector<std::string>> seen;
    std::string line;
    while (std::getline(f, line)) {
        unsigned p = 0;
        char v[64] = "";
        if (sscanf(line.c_str(), "%u %63s", &p, v) != 2 || p > 65535) continue;
        auto& list = seen[(uint16_t)p];
        if (std::find(list.begin(), list.end(), v) == list.end()) list.push_back(v);
    }
    std::lock_guard<std::mutex> l(g_verMu);
    g_seen = std::move(seen);
}

void noteVersion(const std::string& file, const std::string& version, uint16_t protocol) {
    loadVersions(file);
    {
        std::lock_guard<std::mutex> l(g_verMu);
        auto& list = g_seen[protocol];
        if (std::find(list.begin(), list.end(), version) != list.end()) return;
        list.push_back(version);
    }
    std::ofstream(fs::u8path(file), std::ios::app) << protocol << " " << version << "\n";
}

std::string versionForProtocol(uint16_t protocol) {
    switch (protocol) { // the versions before this table was kept by the game itself (git history)
        case 24: return "1.11.0";
        case 26: return "1.12";
        case 27: return "1.13-1.16";
        case 28: case 29: case 30: case 31: case 32: return "1.17.0-test";
        default: break;
    }
    {
        std::lock_guard<std::mutex> l(g_verMu);
        auto it = g_seen.find(protocol);
        if (it != g_seen.end() && !it->second.empty()) {
            std::vector<std::string> v = it->second;
            std::sort(v.begin(), v.end(), [](const std::string& a, const std::string& b) { return verCmp(a, b) < 0; });
            return v.size() == 1 ? v[0] : v.front() + "-" + v.back();
        }
    }
    if (protocol == kProtocolVersion) return QUARK_VERSION;
    return "protocol " + std::to_string(protocol);
}

std::string recordedWith(uint16_t protocol) {
    std::string v = versionForProtocol(protocol);
    return v.rfind("protocol ", 0) == 0 ? "an unknown version (" + v + ")" : "version " + v;
}

bool readProtocol(const std::string& path, uint16_t* protocol) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    unsigned char h[6];
    if (!f.read((char*)h, 6)) return false;
    uint32_t magic = (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
    if (magic != kMagic) return false;
    *protocol = (uint16_t)(h[4] | h[5] << 8);
    return true;
}

bool convertible(uint16_t protocol) { return protocol >= kRecordFormatSince && protocol < kProtocolVersion; }

bool convert(const std::string& in, std::string* out, std::string* err) {
    try {
        if (in.size() < 6) { *err = "not a demo"; return false; }
        uint32_t magic;
        uint16_t ver;
        memcpy(&magic, in.data(), 4);
        memcpy(&ver, in.data() + 4, 2);
        if (magic != kMagic) { *err = "not a demo"; return false; }
        if (!convertible(ver)) { *err = "no converter from protocol " + std::to_string(ver); return false; }
        // (protocols kRecordFormatSince..now share the record format: each record is read the way playback reads it)
        bool haveMap = false, havePrev = false;
        int snaps = 0;
        Snapshot prev;
        size_t pos = 6;
        while (pos + 13 <= in.size()) {
            uint8_t type = (uint8_t)in[pos];
            uint32_t n;
            memcpy(&n, in.data() + pos + 9, 4);
            if (n > in.size() - pos - 13) break; // (truncated: playback stops there too)
            Reader r((const uint8_t*)in.data() + pos + 13, (int)n);
            pos += 13 + (size_t)n;
            if (type == 1) {
                std::string name = r.str();
                uint32_t len = r.u32();
                std::string src;
                src.reserve(std::min<size_t>(len, n));
                for (uint32_t i = 0; i < len && !r.bad(); i++) src += (char)r.u8();
                int id = r.u8();
                Map m;
                std::string perr;
                bool ok = !r.bad() && id < kMaxPlayers && validMapName(name) && m.parse(src, &perr);
                if (!ok && !haveMap) { *err = "its map doesn't load in this version" + (perr.empty() ? std::string() : " (" + perr + ")"); return false; }
                haveMap = haveMap || ok;
                havePrev = false;
            } else if (type == 2) {
                Snapshot s;
                if (!readSnapshotClient(r, s)) continue;
                if (!readSnapshotWorld(r, s, s.baseTick && havePrev ? &prev : nullptr)) continue;
                if (!readSnapshotEvents(r, s)) continue;
                prev = s;
                havePrev = true;
                snaps++;
            }
        }
        if (!haveMap) { *err = "it has no map"; return false; }
        if (!snaps) { *err = "none of its snapshots read in this version"; return false; }
        *out = in;
        uint16_t now = kProtocolVersion;
        memcpy(&(*out)[4], &now, 2);
        return true;
    } catch (const std::exception& e) { // (a hostile or broken file is an error, never a crash)
        *err = std::string("unreadable (") + e.what() + ")";
        return false;
    }
}

UpgradeReport upgradeDir(const std::string& dirIn, const std::atomic<bool>* stop) {
    UpgradeReport rep;
    fs::path dir = fs::u8path(dirIn);
    std::error_code ec;
    // leftovers of a conversion cut off (the game killed mid-write): the original is still in place
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == ".converting") fs::remove(e.path(), ec);
    ec.clear();
    std::set<std::string> failedBefore; // "name size" of demos that didn't convert: not read again every start
    {
        std::ifstream f(dir / "old" / "failed.txt");
        std::string line;
        while (std::getline(f, line)) failedBefore.insert(line);
    }
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".qdemo") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const fs::path& p : files) {
        if (stop && stop->load()) break;
        uint16_t proto;
        if (!readProtocol(p.u8string(), &proto) || proto == kProtocolVersion) continue;
        std::string name = p.filename().u8string(), label = versionForProtocol(proto);
        uint64_t size = fs::file_size(p, ec);
        std::string key = name + " " + std::to_string(size);
        if (failedBefore.count(key)) continue;
        auto fail = [&](const std::string& why) {
            std::string line = name + ": recorded with " + recordedWith(proto) + ", kept as it is: " + why;
            rep.log.push_back(line);
            appendLog(dir, line);
            std::ofstream(dir / "old" / "failed.txt", std::ios::app) << key << "\n";
            failedBefore.insert(key);
        };
        if (proto > kProtocolVersion) { rep.newer++; fail("recorded with a newer version"); continue; }
        if (!convertible(proto)) { rep.failed++; fail("no converter for that version"); continue; }
        std::string data = slurp(p, kMaxBytes), out, why;
        if (data.size() != size) { rep.failed++; fail("can't read it"); continue; }
        if (!convert(data, &out, &why)) { rep.failed++; fail(why); continue; }
        data.clear();
        // the converted copy first (next to it), then the original into old/<version>/, then the copy in its place:
        // whenever this stops, the demo is there (original or converted) and the original is never lost
        fs::path tmp = p;
        tmp += ".converting";
        {
            std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
            o.write(out.data(), (std::streamsize)out.size());
            o.close();
            if (!o) { fs::remove(tmp, ec); rep.failed++; fail("can't write the converted copy (disk full?)"); continue; }
        }
        fs::path backDir = dir / "old" / folderSafe(label);
        fs::create_directories(backDir, ec);
        fs::path back = backDir / p.filename();
        for (int k = 1; fs::exists(back, ec); k++) back = backDir / (p.stem().u8string() + "-" + std::to_string(k) + ".qdemo");
        ec.clear();
        fs::create_hard_link(p, back, ec);
        if (ec) { ec.clear(); fs::copy_file(p, back, ec); }
        if (ec || fs::file_size(back, ec) != size) {
            fs::remove(tmp, ec);
            rep.failed++;
            fail("can't keep a copy of the original in " + backDir.u8string());
            continue;
        }
        fs::rename(tmp, p, ec);
        if (ec) { // (a rename that won't replace a file: the original is safe in old/ by now)
            ec.clear();
            fs::remove(p, ec);
            if (!ec) fs::rename(tmp, p, ec);
        }
        if (ec) {
            std::error_code e2;
            if (!fs::exists(p, e2)) fs::copy_file(back, p, e2); // put the original back where it was
            fs::remove(tmp, e2);
            rep.failed++;
            fail("can't replace it with the converted copy");
            continue;
        }
        rep.converted++;
        std::string line = name + ": converted from " + recordedWith(proto) + " (protocol " + std::to_string(proto) + ") to " + QUARK_VERSION +
                           " (protocol " + std::to_string(kProtocolVersion) + "), original in old/" + folderSafe(label) + "/";
        rep.log.push_back(line);
        appendLog(dir, line);
    }
    return rep;
}

namespace {
struct Run {
    std::thread th;
    std::atomic<bool> stop{false};
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
};
Run* g_run = nullptr;
std::atomic<bool> g_running{false};
} // namespace

void startUpgrade(const std::string& demoDir, const std::string& versionsFile) {
    if (g_run) return;
    noteVersion(versionsFile, QUARK_VERSION, kProtocolVersion);
    std::error_code ec;
    if (!fs::is_directory(fs::u8path(demoDir), ec)) return; // no demos yet
    Run* run = new Run;
    g_running = true;
    run->th = std::thread([run, demoDir] {
        UpgradeReport r = upgradeDir(demoDir, &run->stop);
        for (auto& l : r.log) fprintf(stderr, "demos: %s\n", l.c_str());
        g_running = false;
        std::lock_guard<std::mutex> l(run->mu);
        run->done = true;
        run->cv.notify_all();
    });
    g_run = run;
}

bool upgradeRunning() { return g_running; }

bool finishUpgrade(double maxWaitSeconds) {
    Run* run = g_run;
    if (!run) return true;
    g_run = nullptr;
    run->stop = true; // the file being converted finishes; no other one starts
    bool ended;
    {
        std::unique_lock<std::mutex> l(run->mu);
        ended = run->cv.wait_for(l, std::chrono::duration<double>(maxWaitSeconds), [run] { return run->done; });
    }
    if (!ended) { run->th.detach(); return false; } // (left to the process exit; `run` stays allocated for it)
    run->th.join();
    delete run;
    return true;
}

} // namespace demo
