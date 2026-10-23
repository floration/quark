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
  #include <ifaddrs.h>
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

std::vector<uint32_t> localIpv4Addresses() {
    std::vector<uint32_t> out;
#ifdef _WIN32
    char host[256];   // (Winsock resolves the machine's own name to every adapter's address: no iphlpapi needed)
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (gethostname(host, sizeof host) == 0 && getaddrinfo(host, nullptr, &hints, &res) == 0) {
        for (addrinfo* a = res; a; a = a->ai_next) out.push_back(ntohl(((sockaddr_in*)a->ai_addr)->sin_addr.s_addr));
        freeaddrinfo(res);
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* i = list; i; i = i->ifa_next)
            if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET) out.push_back(ntohl(((sockaddr_in*)i->ifa_addr)->sin_addr.s_addr));
        freeifaddrs(list);
    }
#endif
    return out;
}

bool netInit() {
#ifdef _WIN32
    WSADATA d;
    timeBeginPeriod(1); // 1 ms sleep/timer precision (default is 15.6 ms)
    // Windows 11 stops honouring timeBeginPeriod for a process whose window is minimised or covered (and may run it on
    // efficiency cores): a hosted server would tick in ~15 ms bursts. Opt out of both (Windows 8+; older ones ignore it).
    {
        struct PowerThrottling { ULONG Version, ControlMask, StateMask; } pt{1, 0x1 | 0x4, 0};   // EXECUTION_SPEED | IGNORE_TIMER_RESOLUTION, both off
        using SetInfo = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
        if (HMODULE k = GetModuleHandleW(L"kernel32.dll"))
            if (auto f = (SetInfo)(void*)GetProcAddress(k, "SetProcessInformation")) f(GetCurrentProcess(), 4 /*ProcessPowerThrottling*/, &pt, sizeof pt);
    }
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
    if (readEvent_) WSACloseEvent((WSAEVENT)readEvent_);
    if (timer_) CloseHandle((HANDLE)timer_);
    readEvent_ = timer_ = nullptr;
    waitSetup_ = false;
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
#ifdef _WIN32
    if (!waitSetup_) {   // (once: a socket that can't have them keeps using select)
        waitSetup_ = true;
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, Windows 10 1803+*/, TIMER_ALL_ACCESS);
        WSAEVENT ev = timer_ ? WSACreateEvent() : WSA_INVALID_EVENT;
        if (ev != WSA_INVALID_EVENT && WSAEventSelect(fd_, ev, FD_READ) == 0) readEvent_ = ev;
        else { if (ev != WSA_INVALID_EVENT) WSACloseEvent(ev); if (timer_) CloseHandle((HANDLE)timer_); timer_ = nullptr; }
    }
    if (readEvent_) {
        // The event stays set from a packet the caller has already drained: that wakes this wait at once (one spare
        // loop), then it's reset here; recv re-arms FD_READ, so a packet arriving after that sets it again.
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)us * 10;   // relative, 100 ns units
        if (us <= 0 || !SetWaitableTimer((HANDLE)timer_, &due, 0, nullptr, nullptr, FALSE)) return false;
        HANDLE h[2] = {(HANDLE)readEvent_, (HANDLE)timer_};
        DWORD r = WaitForMultipleObjects(2, h, FALSE, (DWORD)(us / 1000 + 50));
        if (r == WAIT_OBJECT_0) { WSAResetEvent((WSAEVENT)readEvent_); return true; }
        return false;
    }
#endif
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

int readRawMouse(double* dx, double* dy) {
#ifdef _WIN32
    alignas(8) static thread_local BYTE buf[16384];
    static thread_local LONG lastAbsX = 0, lastAbsY = 0;
    static thread_local bool haveAbs = false;
    int total = 0;
    for (int pass = 0; pass < 8; pass++) {
        UINT size = sizeof buf;
        UINT count = GetRawInputBuffer((PRAWINPUT)buf, &size, sizeof(RAWINPUTHEADER));
        if (count == 0 || count == (UINT)-1) break;
        PRAWINPUT ri = (PRAWINPUT)buf;
        for (UINT i = 0; i < count; i++, ri = (PRAWINPUT)(((ULONG_PTR)ri + ri->header.dwSize + 7) & ~(ULONG_PTR)7)) {   // (blocks are 8-byte aligned)
            if (ri->header.dwType != RIM_TYPEMOUSE) continue;
            const RAWMOUSE& m = ri->data.mouse;
            if (m.usFlags & MOUSE_MOVE_ABSOLUTE) {   // (remote desktop, tablets: positions, as GLFW handles them)
                if (haveAbs) { *dx += m.lLastX - lastAbsX; *dy += m.lLastY - lastAbsY; }
                lastAbsX = m.lLastX; lastAbsY = m.lLastY; haveAbs = true;
            } else { *dx += m.lLastX; *dy += m.lLastY; }
        }
        MSG msg;   // (their messages: one per report, in the same order)
        for (UINT i = 0; i < count && PeekMessageW(&msg, nullptr, WM_INPUT, WM_INPUT, PM_REMOVE); i++) {}
        total += (int)count;
    }
    return total;
#else
    (void)dx; (void)dy;
    return 0;
#endif
}

void waitUntil(double t) {
#ifdef _WIN32
    // Sleep(1) wakes on the next 1 ms timer tick: 1 to 2 ms, so stopping 1.5 ms early still overshot by up to 0.5 ms
    // now and then (a frame cap's own late frames, in the 1% lows). A high-resolution waitable timer (Windows 10 1803+)
    // wakes within a fraction of a millisecond; the last 0.3 ms is spun. Older Windows: Sleep(1) with a 2.1 ms margin.
    static thread_local HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
    double left = t - timeNow() - 0.0003;
    if (timer && left > 0) {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(left * 1e7);   // relative, 100 ns units
        if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, (DWORD)(left * 1000) + 50);
    } else {
        while (timeNow() < t - 0.0021) Sleep(1);
    }
#else
    while (timeNow() < t - 0.0015) sleepMs(1);
#endif
    while (timeNow() < t) cpuRelax();
}

void raiseThreadPriority() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
}
