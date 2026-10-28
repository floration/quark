#include "json.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

void jsonWriteString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += (char)c;
        }
    }
    out += '"';
}

void jsonWriteFloat(std::string& out, float v) {
    if (!std::isfinite(v)) { out += "null"; return; }
    char b[32];
    for (int p = 6; p <= 9; p++) {
        snprintf(b, sizeof b, "%.*g", p, (double)v);
        if (std::strtof(b, nullptr) == v) break;
    }
    out += b;
}

void jsonWriteDouble(std::string& out, double v) {
    if (!std::isfinite(v)) { out += "null"; return; }
    char b[40];
    if (v == std::floor(v) && std::fabs(v) < 1e15) snprintf(b, sizeof b, "%.0f", v);
    else {
        for (int p = 10; p <= 17; p++) {
            snprintf(b, sizeof b, "%.*g", p, v);
            if (std::strtod(b, nullptr) == v) break;
        }
    }
    out += b;
}

void Json::write(std::string& out) const {
    switch (t_) {
        case NUL: out += "null"; break;
        case BOOL: out += b_ ? "true" : "false"; break;
        case NUM: if (f32_) jsonWriteFloat(out, (float)n_); else jsonWriteDouble(out, n_); break;
        case STR: jsonWriteString(out, s_); break;
        case ARR:
            out += '[';
            for (size_t k = 0; k < a_.size(); k++) { if (k) out += ','; a_[k].write(out); }
            out += ']';
            break;
        case OBJ:
            out += '{';
            for (size_t k = 0; k < o_.size(); k++) {
                if (k) out += ',';
                jsonWriteString(out, o_[k].first);
                out += ':';
                o_[k].second.write(out);
            }
            out += '}';
            break;
    }
}

namespace {
struct Reader {
    const char* p;
    const char* end;
    const char* begin;
    std::string err;
    int depth = 0;
    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
    bool fail(const char* what) { if (err.empty()) err = std::string(what) + " at byte " + std::to_string(p - begin); return false; }
    static void utf8(std::string& s, uint32_t c) {
        if (c < 0x80) s += (char)c;
        else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 63)); s += (char)(0x80 | (c & 63)); }
        else { s += (char)(0xF0 | (c >> 18)); s += (char)(0x80 | ((c >> 12) & 63)); s += (char)(0x80 | ((c >> 6) & 63)); s += (char)(0x80 | (c & 63)); }
    }
    bool hex4(uint32_t& v) {
        if (end - p < 4) return fail("bad \\u escape");
        v = 0;
        for (int k = 0; k < 4; k++) {
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0'; else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10; else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return fail("bad \\u escape");
        }
        return true;
    }
    bool string(std::string& s) {
        p++;   // "
        while (p < end && *p != '"') {
            char c = *p++;
            if (c != '\\') { s += c; continue; }
            if (p >= end) return fail("unterminated string");
            char e = *p++;
            switch (e) {
                case '"': s += '"'; break; case '\\': s += '\\'; break; case '/': s += '/'; break;
                case 'b': s += '\b'; break; case 'f': s += '\f'; break; case 'n': s += '\n'; break;
                case 'r': s += '\r'; break; case 't': s += '\t'; break;
                case 'u': {
                    uint32_t c1 = 0;
                    if (!hex4(c1)) return false;
                    if (c1 >= 0xD800 && c1 < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        uint32_t c2 = 0;
                        if (!hex4(c2)) return false;
                        c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
                    }
                    utf8(s, c1);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        if (p >= end) return fail("unterminated string");
        p++;
        return true;
    }
    bool value(Json& out) {
        ws();
        if (p >= end) return fail("unexpected end");
        if (++depth > 256) return fail("nested too deep");
        bool ok = true;
        char c = *p;
        if (c == '{') {
            out = Json::object();
            p++; ws();
            if (p < end && *p == '}') p++;
            else for (;;) {
                ws();
                if (p >= end || *p != '"') { ok = fail("expected a key"); break; }
                std::string k;
                if (!string(k)) { ok = false; break; }
                ws();
                if (p >= end || *p != ':') { ok = fail("expected ':'"); break; }
                p++;
                Json v;
                if (!value(v)) { ok = false; break; }
                out.set(k, std::move(v));
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; break; }
                ok = fail("expected ',' or '}'");
                break;
            }
        } else if (c == '[') {
            out = Json::array();
            p++; ws();
            if (p < end && *p == ']') p++;
            else for (;;) {
                Json v;
                if (!value(v)) { ok = false; break; }
                out.push(std::move(v));
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; break; }
                ok = fail("expected ',' or ']'");
                break;
            }
        } else if (c == '"') {
            std::string s;
            ok = string(s);
            out = Json(std::move(s));
        } else if (c == 't' && end - p >= 4 && !strncmp(p, "true", 4)) { p += 4; out = Json(true); }
        else if (c == 'f' && end - p >= 5 && !strncmp(p, "false", 5)) { p += 5; out = Json(false); }
        else if (c == 'n' && end - p >= 4 && !strncmp(p, "null", 4)) { p += 4; out = Json(); }
        else if (c == '-' || (c >= '0' && c <= '9')) {
            char buf[64];
            size_t n = 0;
            while (p < end && n < sizeof buf - 1 && (strchr("+-.eE", *p) || (*p >= '0' && *p <= '9'))) buf[n++] = *p++;
            buf[n] = 0;
            char* e = nullptr;
            double v = std::strtod(buf, &e);
            if (e != buf + n) ok = fail("bad number");
            out = Json(v);
        } else ok = fail("unexpected character");
        depth--;
        return ok;
    }
};
}  // namespace

bool Json::parse(const std::string& text, Json& out, std::string* err) {
    Reader r{text.data(), text.data() + text.size(), text.data(), std::string(), 0};
    out = Json();
    bool ok = r.value(out);
    if (ok) { r.ws(); if (r.p != r.end) ok = r.fail("trailing characters"); }
    if (!ok && err) *err = r.err;
    return ok;
}
