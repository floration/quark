// Cross-platform UDP sockets (Winsock on Windows, BSD sockets elsewhere).
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

struct NetAddr {
    uint32_t ip = 0;   // host byte order
    uint16_t port = 0; // host byte order
    bool operator==(const NetAddr& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const NetAddr& o) const { return !(*this == o); }
    std::string toString() const;
    // Accepts "host", "host:port", or dotted IP. Resolves DNS names.
    static bool parse(const std::string& s, uint16_t defaultPort, NetAddr& out);
    static NetAddr broadcast(uint16_t port) { return {0xFFFFFFFFu, port}; }
};

// This machine's IPv4 addresses (host byte order): every interface's, as the OS lists them.
std::vector<uint32_t> localIpv4Addresses();

bool netInit();
void netShutdown();

// One datagram for UdpSocket::recvBatch / sendBatch (the caller owns the bytes).
struct InPacket { uint8_t* data = nullptr; size_t cap = 0; int len = 0; NetAddr from; };
struct OutPacket { const uint8_t* data = nullptr; size_t len = 0; NetAddr to; };

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket() { close(); }
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // port 0 binds to an ephemeral port.
    bool open(uint16_t port, bool allowBroadcast = false);
    void close();
    bool isOpen() const { return fd_ != kInvalid; }
    bool send(const NetAddr& to, const void* data, size_t len);
    // Non-blocking. Returns bytes received, 0 if nothing pending.
    int recv(NetAddr& from, void* buf, size_t cap);
#if defined(__linux__)
    static constexpr int kBatch = 16;   // recvmmsg / sendmmsg: one system call for many datagrams
#else
    static constexpr int kBatch = 1;    // (no batched calls here: recvBatch / sendBatch go one datagram at a time)
#endif
    // Non-blocking: up to n datagrams, each into its data (up to cap; len = bytes, as recv). Returns how many (0: none).
    int recvBatch(InPacket* pk, int n);
    // Sends each; returns how many went out whole (send() == true for each, one failure doesn't stop the rest).
    int sendBatch(const OutPacket* pk, int n);
    // Block up to timeoutMs waiting for data (used by the dedicated server to sleep).
    bool wait(int timeoutMs);
    bool waitUs(int timeoutUs);
    uint16_t localPort() const;

private:
#ifdef _WIN32
    using Handle = uintptr_t;
    static constexpr Handle kInvalid = ~(uintptr_t)0;
#else
    using Handle = int;
    static constexpr Handle kInvalid = -1;
#endif
    Handle fd_ = kInvalid;
#ifdef _WIN32
    // waitUs on Windows: a high-resolution waitable timer plus the socket's FD_READ event (select only wakes on the
    // 1 ms system timer tick, so a 0.6 ms wait could take up to ~1.7 ms and start 1000 Hz ticks late)
    void* readEvent_ = nullptr;
    void* timer_ = nullptr;
    bool waitSetup_ = false;
#endif
};

// Monotonic time in seconds since first call.
double timeNow();
// Tests only: run the clock faster than real time (simulation is unchanged, just compressed).
void setTimeScale(double scale);
double timeScale();   // (1 outside tests: real seconds = timeNow() seconds / timeScale())
void sleepMs(int ms);
// Returns at timeNow() >= t, on time: sleeps (a high-resolution timer on Windows) until just before, then spins.
void waitUntil(double t);
// Sleeps about `seconds` (real time) on a timer alone, no socket: a high-resolution waitable timer on Windows (it
// still fires on the clock interrupt: up to a timer period, ~1 ms, late), nanosleep elsewhere (late by the thread's
// timer slack: raiseThreadPriority sets it to 1 ns on Linux).
void sleepPrecise(double seconds);
// Gives the calling thread above-normal scheduling priority (server thread inside the client); on Linux, a 1 ns timer
// slack instead (the default 50 us is added to every sleep, select and poll timeout of the thread).
void raiseThreadPriority();

// Short durations in 64 fixed bins (the last one: everything above), read as a high percentile.
class TailHist {
public:
    explicit TailHist(double bin) : bin_(bin) {}
    void add(double s) { int b = s <= 0 ? 0 : (int)(s / bin_); n_[b < kBins ? b : kBins - 1]++; count_++; }
    int count() const { return count_; }
    // The upper edge of the bin the `perMille` percentile falls in (*past: it's the last bin, past the range).
    double upper(int perMille, bool* past = nullptr) const;
    void clear() { for (auto& c : n_) c = 0; count_ = 0; }
    static constexpr int kBins = 64;
private:
    double bin_;
    uint32_t n_[kBins] = {};
    int count_ = 0;
};

// How long before a deadline to stop sleeping and spin. Adaptive, from how late the OS wakes this thread (observe() after
// each timer sleep: 5 us bins, windows of 1000, 1 s at 1000 Hz): the 99th percentile + 20 us when that is within 300 us
// (Linux with a 1 ns slack on bare metal: ~30 us); else the host is noisy (a capped VM, Windows' timer grid) and spinning
// the cap would mostly burn CPU (on a capped VM, make ticks later), so the 90th percentile + 20 us, at most 60 us. It
// rises at once and falls a quarter of the way per window; it starts at 60 us.
class WakeSpin {
public:
    void setFixed(double seconds) { fixed_ = seconds; }   // < 0: adaptive (default); 0: never spin; > 0: always that long
    bool fixed() const { return fixed_ >= 0; }
    double spin() const { return fixed_ >= 0 ? fixed_ : spin_; }
    void observe(double oversleep);
private:
    static constexpr double kMargin = 20e-6, kMin = 20e-6, kMax = 300e-6, kNoisyMax = 60e-6;
    static constexpr int kWindow = 1000;
    TailHist hist_{5e-6};
    double spin_ = kNoisyMax, fixed_ = -1;
};

// A pause instruction for spin-waits (lets the other hyper-thread on the core run).
inline void cpuRelax() {
#if defined(__i386__) || defined(__x86_64__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield");
#endif
}
