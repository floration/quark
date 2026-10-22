#include "net.h"
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <mmsystem.h>
  typedef int socklen_t;
#else
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <cerrno>
#endif

std::string NetAddr::toString() const {
    char b[32];
    snprintf(b, sizeof b, "%u.%u.%u.%u:%u", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255, port);
    return b;
}

bool NetAddr::parse(const std::string& s, uint16_t defaultPort, NetAddr& out) {
    std::string host = s;
    uint16_t port = defaultPort;
    size_t c = s.rfind(':');
    if (c != std::string::npos) {
        host = s.substr(0, c);
        int p = atoi(s.c_str() + c + 1);
        if (p <= 0 || p > 65535) return false;
        port = (uint16_t)p;
    }
    if (host.empty() || host == "localhost") host = "127.0.0.1";
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    out.ip = ntohl(((sockaddr_in*)res->ai_addr)->sin_addr.s_addr);
    out.port = port;
    freeaddrinfo(res);
    return true;
}

bool netInit() {
#ifdef _WIN32
    WSADATA d;
    timeBeginPeriod(1); // 1 ms sleep/timer precision (default is 15.6 ms)
    return WSAStartup(MAKEWORD(2, 2), &d) == 0;
#else
    return true;
#endif
}
void netShutdown() {
#ifdef _WIN32
    timeEndPeriod(1);
    WSACleanup();
#endif
}

bool UdpSocket::open(uint16_t port, bool allowBroadcast) {
    close();
    Handle s = (Handle)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kInvalid) return false;
    int one = 1;
    if (allowBroadcast) setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&one, sizeof one);
    int bufsz = 1 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&bufsz, sizeof bufsz);
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&bufsz, sizeof bufsz);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(s, (sockaddr*)&a, sizeof a) != 0) {
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
        return false;
    }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    // Stop ICMP port-unreachable from surfacing as WSAECONNRESET on recvfrom.
    #ifndef SIO_UDP_CONNRESET
    #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
    #endif
    BOOL f = FALSE; DWORD ret = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &f, sizeof f, nullptr, 0, &ret, nullptr, nullptr);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    fd_ = s;
    return true;
}

void UdpSocket::close() {
    if (fd_ == kInvalid) return;
#ifdef _WIN32
    closesocket(fd_);
#else
    ::close(fd_);
#endif
    fd_ = kInvalid;
}

bool UdpSocket::send(const NetAddr& to, const void* data, size_t len) {
    if (fd_ == kInvalid) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(to.ip);
    a.sin_port = htons(to.port);
    return sendto(fd_, (const char*)data, (int)len, 0, (sockaddr*)&a, sizeof a) == (int)len;
}

int UdpSocket::recv(NetAddr& from, void* buf, size_t cap) {
    if (fd_ == kInvalid) return 0;
    for (;;) {
        sockaddr_in a{};
        socklen_t al = sizeof a;
        int n = (int)recvfrom(fd_, (char*)buf, (int)cap, 0, (sockaddr*)&a, &al);
        if (n < 0) {
#ifdef _WIN32
            int e = WSAGetLastError();
            if (e == WSAECONNRESET || e == WSAEMSGSIZE) continue;
#else
            if (errno == EINTR || errno == ECONNREFUSED) continue;
#endif
            return 0;
        }
        from.ip = ntohl(a.sin_addr.s_addr);
        from.port = ntohs(a.sin_port);
        return n;
    }
}

bool UdpSocket::waitUs(int us) {
    if (fd_ == kInvalid) return false;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd_, &set);
    timeval tv{us / 1000000, us % 1000000};
    return select((int)fd_ + 1, &set, nullptr, nullptr, &tv) > 0;
}

bool UdpSocket::wait(int timeoutMs) {
    if (fd_ == kInvalid) return false;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd_, &set);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    return select((int)fd_ + 1, &set, nullptr, nullptr, &tv) > 0;
}

uint16_t UdpSocket::localPort() const {
    sockaddr_in a{};
    socklen_t al = sizeof a;
    getsockname(fd_, (sockaddr*)&a, &al);
    return ntohs(a.sin_port);
}

static double g_timeScale = 1.0, g_scaleBaseReal = 0.0, g_scaleBaseGame = 0.0;

static double realTime() {
    using clk = std::chrono::steady_clock;
    static const clk::time_point start = clk::now();
    return std::chrono::duration<double>(clk::now() - start).count();
}

double timeNow() {
    if (g_timeScale == 1.0) return realTime() - g_scaleBaseReal + g_scaleBaseGame;
    return g_scaleBaseGame + (realTime() - g_scaleBaseReal) * g_timeScale;
}

void setTimeScale(double scale) {
    double now = timeNow();
    g_scaleBaseReal = realTime();
    g_scaleBaseGame = now;
    g_timeScale = scale;
}

#ifdef _WIN32
// MinGW's sleep_for ignores timeBeginPeriod and sleeps ~15 ms per call; Sleep honours netInit's 1 ms period.
void sleepMs(int ms) { Sleep((DWORD)ms); }
#else
void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
#endif

void raiseThreadPriority() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
}
