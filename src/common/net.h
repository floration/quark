// Cross-platform UDP sockets (Winsock on Windows, BSD sockets elsewhere).
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

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
};

// Monotonic time in seconds since first call.
double timeNow();
// Tests only: run the clock faster than real time (simulation is unchanged, just compressed).
void setTimeScale(double scale);
void sleepMs(int ms);
// Gives the calling thread above-normal scheduling priority (server thread inside the client).
void raiseThreadPriority();
