#include "lang.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include "lang_data.h"   // (generated: tools/lang/embed.py, the tables and atlases in assets/lang)
#include "overrides.h"   // (decodeImageGrey)
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace lang {
namespace detail { bool g_active = false; }

const std::vector<Language>& languages() {
    // the gaming top ten (owner, 2026-11-02: "gaming list yeah"); atlas: the glyph atlas past ASCII it draws with
    static const std::vector<Language> k = {
        {"en", "ENGLISH", "base"},
        {"zh-CN", "\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87", "zh-CN"},   // 简体中文
        {"ru", "\xD0\xA0\xD0\xA3\xD0\xA1\xD0\xA1\xD0\x9A\xD0\x98\xD0\x99", "base"},   // РУССКИЙ
        {"es", "ESPA\xC3\x91OL", "base"},
        {"pt-BR", "PORTUGU\xC3\x8AS (BRASIL)", "base"},
        {"de", "DEUTSCH", "base"},
        {"ja", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "ja"},   // 日本語
        {"fr", "FRAN\xC3\x87" "AIS", "base"},
        {"ko", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4", "ko"},   // 한국어
        {"tr", "T\xC3\x9CRK\xC3\x87" "E", "base"},
    };
    return k;
}

int indexOf(const std::string& code) {
    const auto& L = languages();
    for (size_t i = 0; i < L.size(); i++) if (code == L[i].code) return (int)i;
    return -1;
}

static std::string fromLocale(std::string s) {   // "pt_BR.UTF-8", "zh-Hans-CN", "de-DE" -> one of ours
    for (char& c : s) c = c == '_' ? '-' : (char)std::tolower((unsigned char)c);
    s = s.substr(0, s.find('.'));
    auto starts = [&](const char* p) { return s.compare(0, strlen(p), p) == 0; };
    if (starts("zh")) return "zh-CN";   // (Traditional Chinese readers read Simplified; better than English)
    if (starts("pt")) return "pt-BR";
    for (const char* c : {"ru", "es", "de", "ja", "fr", "ko", "tr"}) if (starts(c)) return c;
    return "en";
}

std::string systemLanguage() {
#ifdef _WIN32
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0) {
        std::string s;
        for (const wchar_t* p = name; *p; p++) s += (char)(*p < 128 ? *p : '?');
        return fromLocale(s);
    }
    return "en";
#else
    for (const char* v : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* e = getenv(v);
        if (e && *e && strcmp(e, "C") != 0 && strcmp(e, "POSIX") != 0) return fromLocale(e);
    }
    return "en";
#endif
}

std::string resolve(const std::string& setting) {
    if (setting.empty() || setting == "auto" || indexOf(setting) < 0) return systemLanguage();
    return setting;
}

// ---- the tables ----

static std::string g_code = "en";
static int g_gen = 0;
static std::unordered_map<std::string, std::string> g_table;   // key -> translation (the language in use)
static std::unordered_map<std::string, std::string> g_cache;   // drawn string -> what's drawn
struct Pattern { std::string key, val; };
static std::vector<Pattern> g_patterns;   // keys with "{}" (a word or name put in: "WARMUP - {} TO VOTE START")
static bool g_clearCache = false;

static const EmbeddedLang* findTable(const std::string& code) {
    for (const EmbeddedLang* e = kEmbeddedLangs; e->code; e++) if (code == e->code) return e;
    return nullptr;
}

template <class F> static void eachLine(const char* p, size_t n, F f) {
    const char* end = p + n;
    while (p < end) {
        const char* e = (const char*)memchr(p, '\n', (size_t)(end - p));
        if (!e) e = end;
        const char* q = e;
        if (q > p && q[-1] == '\r') q--;
        if (q > p) f(p, q);
        p = e + 1;
    }
}

static void parseTable(const EmbeddedLang& e, std::unordered_map<std::string, std::string>& out) {
    out.clear();
    g_patterns.clear();
    out.reserve(4096);
    eachLine(e.tsv, e.tsvSize, [&](const char* a, const char* b) {
        const char* t = (const char*)memchr(a, '\t', (size_t)(b - a));
        if (!t || t == a) return;
        std::string key(a, t), val(t + 1, b);
        if (val.empty() || val == key) return;   // (untranslated: drawn as it is, no entry)
        if (key.find("{}") != std::string::npos && key != "{}") g_patterns.push_back({key, val});
        // "MATERIAL: X": X alone too (the editor's material list draws the names without the label)
        if (key.compare(0, 10, "MATERIAL: ") == 0) {
            const size_t c = val.find(": ");
            if (c != std::string::npos && key.size() > 10 && c + 2 < val.size()) out.emplace(key.substr(10), val.substr(c + 2));
        }
        out.emplace(std::move(key), std::move(val));
    });
}

bool setLanguage(const std::string& code) {
    if (code == g_code) return true;
    if (code == "en") {
        g_table.clear(); g_cache.clear(); g_patterns.clear();
        g_code = "en"; detail::g_active = false; g_gen++;
        return true;
    }
    const EmbeddedLang* e = findTable(code);
    if (!e) return false;
    parseTable(*e, g_table);
    g_cache.clear();
    g_code = code; detail::g_active = true; g_gen++;
    return true;
}

const std::string& current() { return g_code; }
int generation() { return g_gen; }
void frame() { if (g_clearCache || g_cache.size() > 8192) { g_cache.clear(); g_clearCache = false; } }

// ---- lookup ----

static bool isDigit(char c) { return c >= '0' && c <= '9'; }

// s with each number (digits, with '.' or ',' between digits) as '#'; the numbers in order
static bool numbered(const std::string& s, std::string& key, std::vector<std::string>& nums) {
    key.clear(); nums.clear();
    for (size_t i = 0; i < s.size();) {
        if (!isDigit(s[i])) { key += s[i++]; continue; }
        size_t j = i;
        while (j < s.size() && (isDigit(s[j]) || ((s[j] == '.' || s[j] == ',') && j + 1 < s.size() && isDigit(s[j + 1]) && j > i))) j++;
        nums.emplace_back(s, i, j - i);
        key += '#';
        i = j;
    }
    return !nums.empty();
}

static const std::string* lookup(const std::string& s) {
    auto it = g_table.find(s);
    return it == g_table.end() ? nullptr : &it->second;
}

// s against a pattern key: "{}" any text (shortest first), '#' a number, anything else itself
static size_t numberRun(const char* s) {
    size_t j = 0;
    while (isDigit(s[j]) || ((s[j] == '.' || s[j] == ',') && j > 0 && isDigit(s[j + 1]))) j++;
    return j;
}
static bool matchPattern(const char* p, const char* s, std::vector<std::string>& caps, std::vector<std::string>& nums) {
    for (;;) {
        if (!*p) return !*s;
        if (p[0] == '{' && p[1] == '}') {
            for (size_t n = 1; s[n - 1]; n++) {
                caps.emplace_back(s, n);
                if (matchPattern(p + 2, s + n, caps, nums)) return true;
                caps.pop_back();
            }
            return false;
        }
        if (*p == '#') {
            const size_t n = numberRun(s);
            if (!n) return false;
            nums.emplace_back(s, n);
            if (matchPattern(p + 1, s + n, caps, nums)) return true;
            nums.pop_back();
            return false;
        }
        if (*p != *s) return false;
        p++; s++;
    }
}

static std::string translate(const std::string& s, int depth);

// the translation of s, or empty (no change)
static std::string translate(const std::string& s, int depth) {
    if (s.empty() || depth > 3) return {};
    if (const std::string* t = lookup(s)) return *t;
    // numbers: "KILLS: 12" as "KILLS: #", each '#' of the translation the next number
    {
        std::string key;
        std::vector<std::string> nums;
        if (numbered(s, key, nums))
            if (const std::string* t = lookup(key)) {
                std::string out;
                size_t k = 0;
                for (char c : *t) {
                    if (c == '#' && k < nums.size()) out += nums[k++];
                    else out += c;
                }
                if (k == nums.size()) return out;
            }
    }
    // a pattern key: "{}" put back (translated, if it's a key itself), '#' the numbers
    for (const Pattern& pt : g_patterns) {
        if (pt.key[0] != '{' && pt.key[0] != '#' && pt.key[0] != s[0]) continue;   // (most fail on the first character)
        std::vector<std::string> caps, nums;
        if (!matchPattern(pt.key.c_str(), s.c_str(), caps, nums)) continue;
        std::string out;
        size_t ci = 0, ni = 0;
        for (size_t i = 0; i < pt.val.size(); i++) {
            if (pt.val[i] == '{' && i + 1 < pt.val.size() && pt.val[i + 1] == '}' && ci < caps.size()) {
                std::string t = translate(caps[ci], depth + 1);
                out += t.empty() ? caps[ci] : t;
                ci++; i++;
            } else if (pt.val[i] == '#' && ni < nums.size()) out += nums[ni++];
            else out += pt.val[i];
        }
        if (ci == caps.size() && ni == nums.size()) return out;
    }
    // a count before a word ("1 ROCKET": the weapon slots)
    {
        const size_t n = numberRun(s.c_str());
        if (n > 0 && n + 1 < s.size() && s[n] == ' ') {
            std::string r = translate(s.substr(n + 1), depth + 1);
            if (!r.empty()) return s.substr(0, n + 1) + r;
        }
    }
    // decorations: spaces, "> " ... " <" (the picked row), a trailing ">" or ":" (a page link, a label)
    {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '>' || s[a] == '<' || s[a] == '*' || s[a] == '-')) a++;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '>' || s[b - 1] == '<' || s[b - 1] == ':' || s[b - 1] == '*')) b--;
        if (a < b && (a > 0 || b < s.size())) {
            std::string core = translate(s.substr(a, b - a), depth + 1);
            if (!core.empty()) return s.substr(0, a) + core + s.substr(b);
        }
    }
    // "LABEL: VALUE", "A - B": each side on its own
    for (const char* sep : {": ", " - ", " / ", "  "}) {
        const size_t p = s.find(sep);
        if (p == std::string::npos || p == 0) continue;
        const size_t q = p + strlen(sep);
        std::string l = translate(s.substr(0, p), depth + 1), r = q < s.size() ? translate(s.substr(q), depth + 1) : std::string();
        if (l.empty() && sep[0] == ':') {   // ("PLAYER MODEL: STANDARD": the key is the label with its colon, "PLAYER MODEL:")
            const std::string lc = translate(s.substr(0, p + 1), depth + 1);
            if (!lc.empty()) return lc + " " + (r.empty() ? s.substr(q) : r);   // (the language's own colon: fr "MODELE :")
        }
        if (l.empty() && r.empty()) continue;
        return (l.empty() ? s.substr(0, p) : l) + sep + (r.empty() ? s.substr(q) : r);
    }
    return {};
}

// QUARK_LANG_MISSES=<file>: every drawn string with words in it that found no translation, written there at exit
// (finding the lines the code builds that no key covers: docs/UI_TEST.md)
struct Misses {
    const char* path = getenv("QUARK_LANG_MISSES");
    std::unordered_set<std::string> seen;
    ~Misses() {
        if (!path || seen.empty()) return;
        if (FILE* f = fopen(path, "wb")) {
            std::vector<std::string> v(seen.begin(), seen.end());
            std::sort(v.begin(), v.end());
            for (const std::string& m : v) fprintf(f, "%s\n", m.c_str());
            fclose(f);
        }
    }
};
static Misses g_misses;

const std::string& tr(const std::string& s) {
    if (!detail::g_active || s.empty()) return s;
    auto it = g_cache.find(s);
    if (it != g_cache.end()) return it->second.empty() ? s : it->second;
    std::string t = translate(s, 0);
    if (t == s) t.clear();
    if (t.empty() && g_misses.path) {
        int letters = 0;
        for (char c : s) letters += (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (letters >= 3) g_misses.seen.insert(s);
    }
    auto& v = g_cache.emplace(s, std::move(t)).first->second;   // (an empty value: drawn as it is)
    return v.empty() ? s : v;
}

// ---- glyphs ----

static const EmbeddedAtlas* findAtlas(const char* name) {
    for (const EmbeddedAtlas* e = kEmbeddedAtlases; e->name; e++) if (!strcmp(name, e->name)) return e;
    return nullptr;
}

bool loadGlyphs(const std::string& code, ExtGlyphs& out) {
    out = ExtGlyphs();
    const int i = indexOf(code);
    if (i < 0) return false;
    const EmbeddedAtlas* a = findAtlas(languages()[i].atlas);
    if (!a) return false;
    // the glyph list: "cols C cell N" then one "codepoint advance" a line, in atlas order (tools/lang/make_atlas.py)
    bool head = true, ok = true;
    eachLine(a->glyphs, a->glyphsSize, [&](const char* p, const char* e) {
        std::string line(p, e);
        if (head) { ok = sscanf(line.c_str(), "cols %d cell %d", &out.cols, &out.cell) == 2; head = false; return; }
        unsigned cp = 0; float adv = 0;
        if (sscanf(line.c_str(), "%x %f", &cp, &adv) == 2) { out.cp.push_back(cp); out.adv.push_back(adv); }
    });
    if (!ok || out.cols <= 0 || out.cell <= 0 || out.cp.empty()) { out = ExtGlyphs(); return false; }
    out.rows = (int)((out.cp.size() + out.cols - 1) / out.cols);
    int w = 0, h = 0;
    if (!decodeImageGrey(a->png, a->pngSize, w, h, out.tex) || w != out.cols * out.cell || h < out.rows * out.cell) { out = ExtGlyphs(); return false; }
    out.texW = w; out.texH = h;
    return true;
}

// ---- checks (tests) ----

static std::vector<std::string> keyList() {
    std::vector<std::string> k;
    eachLine(kLangKeys, kLangKeysSize, [&](const char* a, const char* b) { k.emplace_back(a, b); });
    return k;
}

size_t keyCount() { return keyList().size(); }
bool hasKey(const std::string& key) { auto k = keyList(); return std::find(k.begin(), k.end(), key) != k.end(); }

bool checkTables(std::vector<std::string>& problems) {
    const size_t before = problems.size();
    const std::vector<std::string> keys = keyList();
    std::unordered_set<std::string> keySet(keys.begin(), keys.end());
    for (const Language& L : languages()) {
        if (!strcmp(L.code, "en")) continue;
        const EmbeddedLang* e = findTable(L.code);
        if (!e) { problems.push_back(std::string(L.code) + ": no table"); continue; }
        size_t n = 0, bad = 0;
        std::unordered_set<std::string> seen;
        eachLine(e->tsv, e->tsvSize, [&](const char* a, const char* b) {
            n++;
            const char* t = (const char*)memchr(a, '\t', (size_t)(b - a));
            if (!t) { if (bad++ < 5) problems.push_back(std::string(L.code) + ": line " + std::to_string(n) + " has no tab"); return; }
            std::string key(a, t), val(t + 1, b);
            if (!keySet.count(key) && bad++ < 5) problems.push_back(std::string(L.code) + ": not a key: " + key);
            seen.insert(key);
            if (std::count(key.begin(), key.end(), '#') != std::count(val.begin(), val.end(), '#') && bad++ < 5)
                problems.push_back(std::string(L.code) + ": '#' count differs: " + key + " -> " + val);
            if (memchr(val.data(), '\t', val.size()) && bad++ < 5) problems.push_back(std::string(L.code) + ": tab in " + key);
        });
        size_t missing = 0;
        for (const std::string& k : keys) if (!seen.count(k)) missing++;
        if (missing) problems.push_back(std::string(L.code) + ": " + std::to_string(missing) + " keys untranslated");
        // every character it draws is in its atlas
        ExtGlyphs g;
        if (!loadGlyphs(L.code, g)) { problems.push_back(std::string(L.code) + ": no glyph atlas"); continue; }
        std::unordered_set<uint32_t> have(g.cp.begin(), g.cp.end());
        size_t absent = 0;
        std::string first;
        for (size_t i = 0; i < e->tsvSize;) {
            const unsigned char c = (unsigned char)e->tsv[i];
            if (c < 0x80) { i++; continue; }
            uint32_t cp = 0;
            int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
            cp = len == 1 ? c : c & (0x7F >> len);
            for (int k = 1; k < len && i + k < e->tsvSize; k++) cp = (cp << 6) | (e->tsv[i + k] & 0x3F);
            if (!have.count(cp)) { if (!absent++) first = std::string(e->tsv + i, (size_t)len); }
            i += len;
        }
        if (absent) problems.push_back(std::string(L.code) + ": " + std::to_string(absent) + " characters not in its atlas (first: " + first + ")");
    }
    // every language's own name draws in every atlas (the LANGUAGE list shows them all)
    for (const Language& A : languages()) {
        ExtGlyphs g;
        if (!loadGlyphs(A.code, g)) continue;
        std::unordered_set<uint32_t> have(g.cp.begin(), g.cp.end());
        for (const Language& L : languages())
            for (const char* p = L.name; *p;) {
                const unsigned char c = (unsigned char)*p;
                int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
                uint32_t cp = len == 1 ? c : c & (0x7F >> len);
                for (int k = 1; k < len && p[k]; k++) cp = (cp << 6) | (p[k] & 0x3F);
                if (c >= 0x80 && !have.count(cp)) { problems.push_back(std::string(A.atlas) + " atlas: no glyph for " + L.name); break; }
                p += len;
            }
    }
    return problems.size() == before;
}

}  // namespace lang
