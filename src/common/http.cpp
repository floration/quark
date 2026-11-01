#include "http.h"
#include <cstdio>
#include <cctype>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

static bool request(const wchar_t* method, const std::string& url, const std::string* body, std::string& out, int timeoutMs, std::string* err) {
    out.clear();
    std::wstring wurl(url.begin(), url.end()); // URLs here are ASCII
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[2048];
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { if (err) *err = "bad url"; return false; }
    HINTERNET s = WinHttpOpen(L"quark", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { if (err) *err = "WinHttpOpen failed"; return false; }
    DWORD protos = 0x00000800 /*TLS1_2*/ | 0x00002000 /*TLS1_3*/;
    if (!WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof protos)) { // pre-1903 Windows rejects the 1.3 bit
        protos = 0x00000800;
        WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof protos);
    }
    WinHttpSetTimeouts(s, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, method, path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    const wchar_t* hdr = L"Content-Type: application/x-www-form-urlencoded\r\n";
    bool ok = r && (body ? WinHttpSendRequest(r, hdr, (DWORD)-1L, (LPVOID)body->data(), (DWORD)body->size(), (DWORD)body->size(), 0)
                         : WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) &&
              WinHttpReceiveResponse(r, nullptr);
    DWORD status = 0, sz = sizeof status;
    if (ok) WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &sz, nullptr);
    if (ok && status != 200) { ok = false; if (err) *err = "HTTP " + std::to_string(status); }
    else if (!ok && err) *err = "connection failed";
    char buf[4096];
    while (ok && out.size() < (1 << 20)) {
        DWORD got = 0;
        if (!WinHttpReadData(r, buf, sizeof buf, &got)) { ok = false; break; }
        if (!got) break;
        out.append(buf, got);
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

bool httpGetText(const std::string& url, std::string& out, int timeoutMs, std::string* err) {
    return request(L"GET", url, nullptr, out, timeoutMs, err);
}
bool httpPostForm(const std::string& url, const std::string& body, std::string& out, int timeoutMs, std::string* err) {
    return request(L"POST", url, &body, out, timeoutMs, err);
}
#else
#include "proc.h"
// The system curl, run without a shell (argv array). Form bodies go through stdin (--data-binary @-) so
// secrets never appear on a command line (visible to every local user in ps / /proc).
static bool curlRun(const std::string& url, const std::string* body, std::string& out, int timeoutMs, std::string* err) {
    out.clear();
    for (char ch : url)
        if ((unsigned char)ch < 32) { if (err) *err = "bad url"; return false; }
    std::vector<std::string> argv = {"curl", "-fsSL", "-A", "quark", "--proto", "=https,http", "--proto-redir", "=https", "--max-time", std::to_string(timeoutMs / 1000 + 1)};
    if (body) { argv.push_back("--data-binary"); argv.push_back("@-"); }
    argv.push_back("--url");
    argv.push_back(url);
    int rc = procRun(argv, body ? *body : std::string(), [&](const char* d, size_t n) {
        out.append(d, n);
        return out.size() < (1 << 20);
    });
    if (rc != 0) { if (err) *err = rc < 0 ? "could not run curl" : rc == 22 ? "HTTP error status" : "request failed"; return false; } // (22: curl -f)
    return true;
}

bool httpGetText(const std::string& url, std::string& out, int timeoutMs, std::string* err) {
    return curlRun(url, nullptr, out, timeoutMs, err);
}

bool httpPostForm(const std::string& url, const std::string& body, std::string& out, int timeoutMs, std::string* err) {
    for (char ch : body)
        if (!(isalnum((unsigned char)ch) || std::string("=&:,._-").find(ch) != std::string::npos)) { if (err) *err = "bad body"; return false; }
    return curlRun(url, &body, out, timeoutMs, err);
}
#endif
