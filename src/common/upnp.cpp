#include "upnp.h"
#include "net.h"
#include <cstring>
#include <cstdio>
#include <vector>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef int socklen_t;
  #define CLOSESOCK closesocket
  typedef SOCKET sock_t;
  static bool sockValid(sock_t s) { return s != INVALID_SOCKET; }
#else
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #define CLOSESOCK ::close
  typedef int sock_t;
  static bool sockValid(sock_t s) { return s >= 0; }
#endif

namespace {

void setTimeouts(sock_t s, int ms) {
#ifdef _WIN32
    DWORD t = ms;
#else
    timeval t{ms / 1000, (ms % 1000) * 1000};
#endif
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof t);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&t, sizeof t);
}

std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

struct Url { std::string host; uint16_t port = 80; std::string path = "/"; };

bool parseUrl(const std::string& u, Url& out) {
    std::string s = u;
    if (lower(s.substr(0, 7)) != "http://") return false;
    s = s.substr(7);
    size_t slash = s.find('/');
    std::string hp = slash == std::string::npos ? s : s.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : s.substr(slash);
    size_t colon = hp.find(':');
    out.host = colon == std::string::npos ? hp : hp.substr(0, colon);
    out.port = colon == std::string::npos ? 80 : (uint16_t)atoi(hp.c_str() + colon + 1);
    return !out.host.empty();
}

// Blocking HTTP/1.0 request; returns the full response (headers + body).
std::string httpRequest(const Url& url, const std::string& method, const std::string& extraHeaders,
                        const std::string& body, std::string* localIp = nullptr) {
    NetAddr addr;
    if (!NetAddr::parse(url.host, url.port, addr)) return "";
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!sockValid(s)) return "";
    setTimeouts(s, 3000);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(addr.ip);
    a.sin_port = htons(addr.port);
    if (connect(s, (sockaddr*)&a, sizeof a) != 0) { CLOSESOCK(s); return ""; }
    if (localIp) {
        sockaddr_in l{};
        socklen_t ll = sizeof l;
        getsockname(s, (sockaddr*)&l, &ll);
        char buf[32];
        uint32_t ip = ntohl(l.sin_addr.s_addr);
        snprintf(buf, sizeof buf, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
        *localIp = buf;
    }
    std::string req = method + " " + url.path + " HTTP/1.0\r\nHost: " + url.host + ":" + std::to_string(url.port) +
                      "\r\n" + extraHeaders + "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    send(s, req.data(), (int)req.size(), 0);
    std::string resp;
    char buf[4096];
    for (;;) {
        int n = (int)recv(s, buf, sizeof buf, 0);
        if (n <= 0) break;
        resp.append(buf, n);
        if (resp.size() > (1 << 20)) break;
    }
    CLOSESOCK(s);
    return resp;
}

std::string xmlTag(const std::string& xml, const std::string& tag, size_t from = 0, size_t* endPos = nullptr) {
    std::string lx = lower(xml), lt = lower(tag);
    size_t a = lx.find("<" + lt + ">", from);
    if (a == std::string::npos) {
        // tolerate namespace prefixes like <s:tag>
        a = lx.find(":" + lt + ">", from);
        if (a == std::string::npos) return "";
        a = lx.find('>', a);
    } else a = lx.find('>', a);
    size_t b = lx.find("</", a);
    if (b == std::string::npos) return "";
    if (endPos) *endPos = b;
    std::string v = xml.substr(a + 1, b - a - 1);
    while (!v.empty() && isspace((unsigned char)v.back())) v.pop_back();
    while (!v.empty() && isspace((unsigned char)v[0])) v.erase(0, 1);
    return v;
}

std::string soap(const std::string& control, const std::string& service, const std::string& action, const std::string& args) {
    Url u;
    if (!parseUrl(control, u)) return "";
    std::string body =
        "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" + action + " xmlns:u=\"" + service + "\">" +
        args + "</u:" + action + "></s:Body></s:Envelope>\r\n";
    std::string headers = "Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + service + "#" + action + "\"\r\n";
    return httpRequest(u, "POST", headers, body);
}

bool httpOk(const std::string& resp) { return resp.size() > 12 && resp.compare(9, 3, "200") == 0; }

} // namespace

void PortMapper::setStatus(const std::string& s) { std::lock_guard<std::mutex> l(mu_); status_ = s; }
std::string PortMapper::status() { std::lock_guard<std::mutex> l(mu_); return status_; }
std::string PortMapper::externalIp() { std::lock_guard<std::mutex> l(mu_); return extIp_; }

void PortMapper::start(uint16_t port) {
    stop();
    port_ = port;
    done_ = false;
    ok_ = false;
    setStatus("searching for router (UPnP)...");
    thread_ = std::thread([this] { worker(); });
}

void PortMapper::stop() {
    if (thread_.joinable()) thread_.join();
    if (mapped_) {
        std::string args = "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port_) +
                           "</NewExternalPort><NewProtocol>UDP</NewProtocol>";
        soap(controlUrl_, serviceType_, "DeletePortMapping", args);
        mapped_ = false;
    }
}

void PortMapper::worker() {
    // 1. SSDP discovery
    UdpSocket ssdp;
    std::vector<std::string> locations;
    if (ssdp.open(0)) {
        const char* targets[] = {"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                                 "urn:schemas-upnp-org:service:WANIPConnection:1",
                                 "urn:schemas-upnp-org:device:InternetGatewayDevice:2"};
        NetAddr mc{(239u << 24) | (255u << 16) | (255u << 8) | 250u, 1900};
        for (const char* st : targets) {
            std::string msg = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") + st + "\r\n\r\n";
            ssdp.send(mc, msg.data(), msg.size());
        }
        double until = timeNow() + 2.5;
        char buf[2048];
        while (timeNow() < until) {
            ssdp.wait(100);
            NetAddr from;
            int n;
            while ((n = ssdp.recv(from, buf, sizeof buf - 1)) > 0) {
                buf[n] = 0;
                std::string r = buf, lr = lower(r);
                size_t p = lr.find("\nlocation:");
                if (p == std::string::npos) continue;
                size_t e = r.find('\r', p + 10);
                std::string loc = r.substr(p + 10, e - p - 10);
                while (!loc.empty() && loc[0] == ' ') loc.erase(0, 1);
                bool dup = false;
                for (auto& l : locations) dup |= l == loc;
                if (!dup) locations.push_back(loc);
            }
            if (!locations.empty() && timeNow() > until - 1.5) break;
        }
    }
    if (locations.empty()) {
        setStatus("UPnP: no router found - forward UDP port " + std::to_string(port_) + " manually");
        done_ = true;
        return;
    }
    // 2. Fetch device descriptions and find a WAN connection service.
    for (const auto& loc : locations) {
        Url u;
        if (!parseUrl(loc, u)) continue;
        std::string localIp;
        std::string desc = httpRequest(u, "GET", "", "", &localIp);
        if (desc.empty()) continue;
        size_t pos = 0;
        for (;;) {
            size_t end;
            std::string st = xmlTag(desc, "serviceType", pos, &end);
            if (st.empty()) break;
            pos = end + 1;
            if (st.find("WANIPConnection") == std::string::npos && st.find("WANPPPConnection") == std::string::npos) continue;
            std::string ctrl = xmlTag(desc, "controlURL", pos);
            if (ctrl.empty()) continue;
            if (lower(ctrl.substr(0, 7)) != "http://") {
                if (ctrl[0] != '/') ctrl = "/" + ctrl;
                ctrl = "http://" + u.host + ":" + std::to_string(u.port) + ctrl;
            }
            controlUrl_ = ctrl;
            serviceType_ = st;
            localIp_ = localIp;
            break;
        }
        if (!controlUrl_.empty()) break;
    }
    if (controlUrl_.empty()) {
        setStatus("UPnP: router does not support port mapping - forward UDP port " + std::to_string(port_) + " manually");
        done_ = true;
        return;
    }
    // 3. Add the mapping (some routers reject infinite leases, so retry with one).
    bool added = false;
    for (int lease : {0, 7200}) {
        std::string args = "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port_) +
                           "</NewExternalPort><NewProtocol>UDP</NewProtocol><NewInternalPort>" + std::to_string(port_) +
                           "</NewInternalPort><NewInternalClient>" + localIp_ +
                           "</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>quark</NewPortMappingDescription>"
                           "<NewLeaseDuration>" + std::to_string(lease) + "</NewLeaseDuration>";
        if (httpOk(soap(controlUrl_, serviceType_, "AddPortMapping", args))) { added = true; break; }
    }
    std::string ip = xmlTag(soap(controlUrl_, serviceType_, "GetExternalIPAddress", ""), "NewExternalIPAddress");
    {
        std::lock_guard<std::mutex> l(mu_);
        extIp_ = ip;
    }
    if (added) {
        mapped_ = true;
        ok_ = true;
        setStatus("UPnP: UDP port " + std::to_string(port_) + " open" + (ip.empty() ? "" : " - public address " + ip + ":" + std::to_string(port_)));
    } else {
        setStatus("UPnP: router refused mapping - forward UDP port " + std::to_string(port_) + " to " + localIp_ + " manually");
    }
    done_ = true;
}
