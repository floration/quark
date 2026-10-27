#include "filewatch.h"
#include "../common/u8file.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>

namespace {
// The file's write time and size, as one key (-2: no file). Through std::filesystem on the UTF-8 path: the narrow
// stat() read it in the ANSI code page, so under a user folder with an accent in its name it always failed and edits
// never reloaded.
long long fileKey(const std::string& path) {
    std::error_code ec;
    const auto p = utf8Path(path);
    const auto size = std::filesystem::file_size(p, ec);
    if (ec) return -2;
    const auto t = std::filesystem::last_write_time(p, ec);
    if (ec) return -2;
    return (long long)((unsigned long long)t.time_since_epoch().count() * 1000003ULL + (unsigned long long)size);   // (time + size: edits within a second still count)
}
// The watched files (hud.cfg or the mode's profile, theme.cfg), looked at once a second on a thread of their own: the
// look (12-17 us, more with an antivirus) used to be on the frame, and that frame was in the 0.1% lows every second.
struct Watch {
    static constexpr int kSlots = 4;
    std::mutex mu;
    std::string path[kSlots];
    std::atomic<long long> key[kSlots];
    int next = 0;
    std::atomic<bool> stop{false};
    std::thread t;
    Watch() { for (auto& k : key) k = -3; }
    ~Watch() { stop = true; if (t.joinable()) t.join(); }
};
Watch& watch() { static Watch w; return w; }
void watchLoop() {
    Watch& w = watch();
    while (!w.stop) {
        for (int i = 0; i < Watch::kSlots; i++) {
            std::string p;
            { std::lock_guard<std::mutex> l(w.mu); p = w.path[i]; }
            if (!p.empty()) w.key[i] = fileKey(p);
        }
        for (int i = 0; i < 10 && !w.stop; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
}

long long watchedFileKey(const std::string& path) {
    Watch& w = watch();
    std::lock_guard<std::mutex> l(w.mu);
    if (!w.t.joinable()) w.t = std::thread(watchLoop);
    for (int i = 0; i < Watch::kSlots; i++) if (w.path[i] == path) return w.key[i];
    const int i = w.next;   // (a file not watched yet: looked at now, once; it takes the oldest slot)
    w.next = (w.next + 1) % Watch::kSlots;
    w.path[i] = path;
    return w.key[i] = fileKey(path);
}

long long fileWritten(const std::string& path) {
    const long long k = fileKey(path);
    Watch& w = watch();
    std::lock_guard<std::mutex> l(w.mu);
    for (int i = 0; i < Watch::kSlots; i++) if (w.path[i] == path) w.key[i] = k;
    return k;
}

