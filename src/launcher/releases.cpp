#include "releases.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace releases {

const char* channelName(Channel c) { return c == Channel::Testing ? "testing" : "production"; }
Channel channelFromName(const std::string& s) { return s == "testing" ? Channel::Testing : Channel::Production; }

namespace {
// "1.17.0-test.3+build" -> core {1,17,0}, pre {"test","3"} (build metadata ignored)
void splitVersion(const std::string& v, std::vector<long>* core, std::vector<std::string>* pre) {
    std::string s = v.substr(0, v.find('+'));
    size_t dash = s.find('-');
    std::string c = s.substr(0, dash), p = dash == std::string::npos ? "" : s.substr(dash + 1);
    size_t i = 0;
    while (i <= c.size()) {
        size_t j = c.find('.', i);
        if (j == std::string::npos) j = c.size();
        core->push_back(strtol(c.substr(i, j - i).c_str(), nullptr, 10));
        i = j + 1;
    }
    while (core->size() < 3) core->push_back(0);
    if (p.empty()) return;
    i = 0;
    while (i <= p.size()) {
        size_t j = p.find('.', i);
        if (j == std::string::npos) j = p.size();
        pre->push_back(p.substr(i, j - i));
        i = j + 1;
    }
}
bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char ch) { return isdigit((unsigned char)ch); });
}
} // namespace

int versionCmp(const std::string& a, const std::string& b) {
    std::vector<long> ca, cb;
    std::vector<std::string> pa, pb;
    splitVersion(a, &ca, &pa);
    splitVersion(b, &cb, &pb);
    for (size_t i = 0; i < std::max(ca.size(), cb.size()); i++) {
        long x = i < ca.size() ? ca[i] : 0, y = i < cb.size() ? cb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    if (pa.empty() != pb.empty()) return pa.empty() ? 1 : -1; // a release beats its prereleases
    for (size_t i = 0; i < std::min(pa.size(), pb.size()); i++) {
        bool na = allDigits(pa[i]), nb = allDigits(pb[i]);
        if (na && nb) {
            long x = strtol(pa[i].c_str(), nullptr, 10), y = strtol(pb[i].c_str(), nullptr, 10);
            if (x != y) return x < y ? -1 : 1;
        } else if (na != nb) return na ? -1 : 1;
        else if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    }
    if (pa.size() != pb.size()) return pa.size() < pb.size() ? -1 : 1;
    return 0;
}

std::string versionFromTag(const std::string& tag) {
    size_t i = 0;
    while (i < tag.size() && !isdigit((unsigned char)tag[i])) i++;
    return tag.substr(i);
}

// ---- a small JSON reader: just enough for the GitHub releases list ----------------------------------------
namespace {
struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    std::string s;                                    // Str (also the raw text of a Num)
    std::vector<Json> items;                          // Arr
    std::vector<std::pair<std::string, Json>> fields; // Obj
    const Json* get(const char* k) const {
        for (auto& f : fields) if (f.first == k) return &f.second;
        return nullptr;
    }
    std::string str(const char* k) const { const Json* j = get(k); return j && j->type == Str ? j->s : ""; }
    bool flag(const char* k) const { const Json* j = get(k); return j && j->type == Bool && j->b; }
};

struct Parser {
    const std::string& t;
    size_t i = 0;
    int depth = 0;
    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) i++; }
    bool lit(const char* w) {
        size_t n = strlen(w);
        if (t.compare(i, n, w) != 0) return false;
        i += n;
        return true;
    }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    bool hex4(unsigned* v) {
        if (i + 4 > t.size()) return false;
        *v = 0;
        for (int k = 0; k < 4; k++) {
            char c = t[i++];
            *v <<= 4;
            if (c >= '0' && c <= '9') *v |= c - '0';
            else if (c >= 'a' && c <= 'f') *v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') *v |= c - 'A' + 10;
            else return false;
        }
        return true;
    }
    bool string(std::string* out) {
        if (i >= t.size() || t[i] != '"') return false;
        i++;
        while (i < t.size()) {
            char c = t[i++];
            if (c == '"') return true;
            if (c != '\\') { *out += c; continue; }
            if (i >= t.size()) return false;
            char e = t[i++];
            switch (e) {
                case 'n': *out += '\n'; break;
                case 't': *out += '\t'; break;
                case 'r': case 'b': case 'f': break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(&cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && t.compare(i, 2, "\\u") == 0) { // surrogate pair
                        unsigned lo;
                        i += 2;
                        if (!hex4(&lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(*out, cp);
                    break;
                }
                default: *out += e;
            }
        }
        return false;
    }
    bool value(Json* v) {
        if (++depth > 64) return false;
        ws();
        bool ok = true;
        if (i >= t.size()) ok = false;
        else if (t[i] == '"') { v->type = Json::Str; ok = string(&v->s); }
        else if (t[i] == '{') {
            v->type = Json::Obj;
            i++;
            ws();
            if (i < t.size() && t[i] == '}') i++;
            else for (;;) {
                std::string k;
                ws();
                if (!string(&k)) { ok = false; break; }
                ws();
                if (i >= t.size() || t[i++] != ':') { ok = false; break; }
                v->fields.emplace_back(std::move(k), Json());
                if (!value(&v->fields.back().second)) { ok = false; break; }
                ws();
                if (i < t.size() && t[i] == ',') { i++; continue; }
                if (i < t.size() && t[i] == '}') { i++; break; }
                ok = false;
                break;
            }
        } else if (t[i] == '[') {
            v->type = Json::Arr;
            i++;
            ws();
            if (i < t.size() && t[i] == ']') i++;
            else for (;;) {
                v->items.emplace_back();
                if (!value(&v->items.back())) { ok = false; break; }
                ws();
                if (i < t.size() && t[i] == ',') { i++; continue; }
                if (i < t.size() && t[i] == ']') { i++; break; }
                ok = false;
                break;
            }
        } else if (lit("true")) { v->type = Json::Bool; v->b = true; }
        else if (lit("false")) { v->type = Json::Bool; }
        else if (lit("null")) {}
        else {
            size_t s = i;
            while (i < t.size() && (isdigit((unsigned char)t[i]) || (t[i] && strchr("+-.eE", t[i])))) i++;
            ok = i > s;
            v->type = Json::Num;
            v->s = t.substr(s, i - s);
        }
        depth--;
        return ok;
    }
};
bool endsWith(const std::string& s, const std::string& w) { return s.size() >= w.size() && s.compare(s.size() - w.size(), w.size(), w) == 0; }
} // namespace

bool parse(const std::string& json, const std::string& platform, std::vector<Release>* out) {
    Json root;
    Parser p{json};
    if (!p.value(&root) || root.type != Json::Arr) return false;
    std::string want = "/quark-" + platform + ".qpak";
    for (const Json& r : root.items) {
        if (r.type != Json::Obj) continue;
        Release rel;
        rel.tag = r.str("tag_name");
        rel.version = versionFromTag(rel.tag);
        rel.name = r.str("name");
        rel.body = r.str("body");
        rel.prerelease = r.flag("prerelease");
        rel.draft = r.flag("draft");
        if (const Json* assets = r.get("assets"))
            for (const Json& a : assets->items) {
                std::string u = a.str("browser_download_url");
                if (endsWith(u, want)) rel.url = u;
                else if (endsWith(u, want + ".sig")) rel.sigUrl = u;
            }
        out->push_back(rel);
    }
    return true;
}

const char* const kFirstSigned = "1.17.0-0";

bool fetchAll(const PageGetter& getPage, const std::string& platform, std::vector<Release>* out, bool* complete, std::string* err) {
    std::vector<Release> all;
    *complete = false;
    for (int page = 1;; page++) {
        std::string json, e;
        std::vector<Release> got;
        if (!getPage(page, &json, &e)) {
            if (page == 1) { *err = e; return false; }
            break;
        }
        // a JSON array of release objects; anything else (an API error object) on a later page = stop there
        if (!parse(json, platform, &got)) {
            if (page == 1) { *err = "unreadable release list"; return false; }
            break;
        }
        bool fresh = got.empty();
        for (const Release& r : got)
            fresh = fresh || std::none_of(all.begin(), all.end(), [&](const Release& x) { return x.tag == r.tag; });
        if (!fresh) break; // the same releases again: the server ignores ?page=, don't loop
        all.insert(all.end(), got.begin(), got.end());
        if ((int)got.size() < kPageSize) { *complete = true; break; } // a short page is the last one
    }
    out->insert(out->end(), all.begin(), all.end());
    return true;
}

std::vector<Release> forChannel(const std::vector<Release>& all, Channel c) {
    std::vector<Release> list;
    for (const Release& r : all) {
        if (r.draft || r.prerelease != (c == Channel::Testing)) continue;
        if (r.version.empty() || r.url.empty()) continue;
        if (!r.isSigned() && versionCmp(r.version, kFirstSigned) >= 0) continue; // signed-era version without a .sig
        auto dup = std::find_if(list.begin(), list.end(), [&](const Release& x) { return x.version == r.version; });
        if (dup != list.end()) {
            if (!dup->isSigned() && r.isSigned()) *dup = r;
            continue;
        }
        list.push_back(r);
    }
    std::stable_sort(list.begin(), list.end(), [](const Release& x, const Release& y) { return versionCmp(x.version, y.version) > 0; });
    return list;
}

int latestIndex(const std::vector<Release>& list) {
    for (size_t i = 0; i < list.size(); i++) if (list[i].isSigned()) return (int)i;
    return -1;
}

std::string asciiText(const std::string& u) {
    std::string o;
    for (size_t i = 0; i < u.size();) {
        unsigned char c = (unsigned char)u[i];
        if (c < 0x80) { o += (char)c; i++; continue; }
        int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        unsigned cp = n == 4 ? c & 7 : n == 3 ? c & 15 : n == 2 ? c & 31 : 0;
        for (int k = 1; k < n && i + k < u.size(); k++) cp = cp << 6 | ((unsigned char)u[i + k] & 63);
        i += n;
        switch (cp) {
            case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: o += '-'; break;
            case 0x2018: case 0x2019: case 0x201B: case 0x2032: o += '\''; break;
            case 0x201C: case 0x201D: case 0x201F: case 0x2033: o += '"'; break;
            case 0x2026: o += "..."; break;
            case 0x2022: case 0x00B7: o += '-'; break;
            case 0x00A0: case 0x2009: case 0x202F: o += ' '; break;
            case 0x2192: o += "->"; break;
            case 0x00D7: o += 'x'; break;
            default: o += '?';
        }
    }
    return o;
}

bool shouldAutoInstall(const std::string& target, const std::string& installed, bool pinned) {
    if (target.empty()) return false;
    if (installed.empty()) return true;
    if (pinned) return target != installed;
    return versionCmp(target, installed) > 0;
}

Config parseConfig(const std::string& text) {
    Config c;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        std::string line = text.substr(i, j - i);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("pin=", 0) == 0) c.pin = line.substr(4).empty() ? "latest" : line.substr(4);
        else if (line.rfind("channel=", 0) == 0) c.channel = channelFromName(line.substr(8));
        i = j + 1;
    }
    return c;
}

std::string formatConfig(const Config& c) {
    return std::string("channel=") + channelName(c.channel) + "\npin=" + (c.pin.empty() ? "latest" : c.pin) + "\n";
}

int selectIndex(const std::vector<Release>& list, const Config& c) {
    if (c.pinned())
        for (size_t i = 0; i < list.size(); i++) if (list[i].version == c.pin) return (int)i;
    return std::max(0, latestIndex(list));
}

Plan startupPlan(const std::vector<Release>& list, const Config& c, const std::string& installed, bool complete) {
    Plan p;
    if (list.empty()) return p;
    p.index = selectIndex(list, c);
    p.pinned = c.pinned() && list[p.index].version == c.pin;
    if (c.pinned() && !p.pinned && !complete) return p; // the pin may be on a page we couldn't read: change nothing
    const Release& r = list[p.index];
    if (!r.isSigned()) { // only ever the player's pin (LATEST is always signed)
        p.confirm = p.pinned && r.version != installed;
        return p;
    }
    p.install = shouldAutoInstall(r.version, installed, p.pinned);
    return p;
}

} // namespace releases
