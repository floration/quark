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
void sleepMs(int ms);
// Windows: every pending raw mouse report in one read (GetRawInputBuffer), summed into dx / dy; their WM_INPUT messages
// are taken off the queue (exactly as many, oldest first). Returns the reports read; 0 elsewhere. A fast mouse (8 kHz)
// sends ~7 reports a frame, and each went through its own message, window procedure and two GetRawInputData calls.
int readRawMouse(double* dx, double* dy);
// Returns at timeNow() >= t, on time: sleeps (a high-resolution timer on Windows) until just before, then spins.
void waitUntil(double t);
// Gives the calling thread above-normal scheduling priority (server thread inside the client).
void raiseThreadPriority();
// A pause instruction for spin-waits (lets the other hyper-thread on the core run).
inline void cpuRelax() {
#if defined(__i386__) || defined(__x86_64__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield");
#endif
}
