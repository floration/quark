#include "http.h"
#include <cstdio>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

bool httpGetText(const std::string& url, std::string& out, int timeoutMs, std::string* err) {
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
    DWORD protos = 0x00000800 /*TLS1_2*/ | 0x00000200 /*TLS1_1*/;
    WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof protos);
    WinHttpSetTimeouts(s, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    bool ok = r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
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
#else
bool httpGetText(const std::string& url, std::string& out, int timeoutMs, std::string* err) {
    out.clear();
    for (char ch : url)
        if (ch == '\'' || ch == '\\' || (unsigned char)ch < 32) { if (err) *err = "bad url"; return false; }
    std::string cmd = "curl -fsS -A quark --max-time " + std::to_string(timeoutMs / 1000 + 1) + " '" + url + "' 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) { if (err) *err = "could not run curl"; return false; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0 && out.size() < (1 << 20)) out.append(buf, n);
    int rc = pclose(p);
    if (rc != 0) { if (err) *err = "request failed"; return false; }
    return true;
}
#endif
