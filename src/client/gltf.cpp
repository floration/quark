#include "gltf.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

// ---------------------------------------------------------------------------------------------------------------
// base64

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const uint8_t* d, size_t n) {
    std::string s;
    s.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? (uint32_t)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        s += kB64[v >> 18 & 63]; s += kB64[v >> 12 & 63];
        s += i + 1 < n ? kB64[v >> 6 & 63] : '=';
        s += i + 2 < n ? kB64[v & 63] : '=';
    }
    return s;
}

bool base64Decode(const std::string& s, std::vector<uint8_t>& out) {
    int8_t rev[256];
    memset(rev, -1, sizeof rev);
    for (int i = 0; i < 64; i++) rev[(uint8_t)kB64[i]] = (int8_t)i;
    rev[(uint8_t)'-'] = 62; rev[(uint8_t)'_'] = 63;   // (url-safe too)
    uint32_t acc = 0; int bits = 0;
    for (char c : s) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = rev[(uint8_t)c];
        if (v < 0) return false;
        acc = acc << 6 | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((uint8_t)(acc >> bits)); }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// a minimal JSON reader

namespace {
struct J {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    double n = 0; bool b = false; std::string s;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;
    const J* get(const char* k) const { for (auto& kv : o) if (kv.first == k) return &kv.second; return nullptr; }
    double num(const char* k, double def) const { const J* v = get(k); return v && v->t == NUM ? v->n : def; }
    int inum(const char* k, int def) const { return (int)num(k, def); }
    std::string str(const char* k) const { const J* v = get(k); return v && v->t == STR ? v->s : std::string(); }
    size_t size() const { return t == ARR ? a.size() : 0; }
};

struct Parser {
    const char* p; const char* e; bool bad = false;
    void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
    bool lit(const char* s) { size_t n = strlen(s); if ((size_t)(e - p) >= n && !memcmp(p, s, n)) { p += n; return true; } return false; }
    std::string string() {
        std::string out;
        p++;   // opening quote
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                p++;
                switch (*p) {
                    case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break; case 'f': out += '\f'; break;
                    case 'u': {
                        if (e - p < 5) { bad = true; return out; }
                        unsigned cp = (unsigned)strtoul(std::string(p + 1, p + 5).c_str(), nullptr, 16);
                        p += 4;
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) { out += (char)(0xC0 | cp >> 6); out += (char)(0x80 | (cp & 63)); }
                        else { out += (char)(0xE0 | cp >> 12); out += (char)(0x80 | (cp >> 6 & 63)); out += (char)(0x80 | (cp & 63)); }
                    } break;
                    default: out += *p;
                }
                p++;
            } else out += *p++;
        }
        if (p >= e) bad = true; else p++;
        return out;
    }
    J value(int depth = 0) {
        J v;
        ws();
        if (p >= e || depth > 64) { bad = true; return v; }
        if (*p == '{') {
            v.t = J::OBJ; p++; ws();
            if (p < e && *p == '}') { p++; return v; }
            while (!bad) {
                ws();
                if (p >= e || *p != '"') { bad = true; break; }
                std::string k = string();
                ws();
                if (p >= e || *p != ':') { bad = true; break; }
                p++;
                v.o.emplace_back(k, value(depth + 1));
                ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == '}') { p++; break; }
                bad = true;
            }
        } else if (*p == '[') {
            v.t = J::ARR; p++; ws();
            if (p < e && *p == ']') { p++; return v; }
            while (!bad) {
                v.a.push_back(value(depth + 1));
                ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == ']') { p++; break; }
                bad = true;
            }
        } else if (*p == '"') { v.t = J::STR; v.s = string(); }
        else if (lit("true")) { v.t = J::BOOL; v.b = true; }
        else if (lit("false")) { v.t = J::BOOL; }
        else if (lit("null")) {}
        else {
            char* end = nullptr;
            std::string tmp(p, std::min<size_t>(64, (size_t)(e - p)));
            v.n = strtod(tmp.c_str(), &end);
            if (end == tmp.c_str()) { bad = true; return v; }
            v.t = J::NUM; p += end - tmp.c_str();
        }
        return v;
    }
};

// column-major 4x4 (doubles)
struct M4 {
    double m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    M4 operator*(const M4& o) const {
        M4 r;
        for (int c = 0; c < 4; c++)
            for (int rr = 0; rr < 4; rr++) {
                double s = 0;
                for (int k = 0; k < 4; k++) s += m[k * 4 + rr] * o.m[c * 4 + k];
                r.m[c * 4 + rr] = s;
            }
        return r;
    }
};

M4 nodeMatrix(const J& n) {
    M4 r;
    if (const J* mt = n.get("matrix")) { if (mt->size() == 16) for (int i = 0; i < 16; i++) r.m[i] = mt->a[i].n; return r; }
    double t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
    if (const J* v = n.get("translation")) if (v->size() == 3) for (int i = 0; i < 3; i++) t[i] = v->a[i].n;
    if (const J* v = n.get("rotation")) if (v->size() == 4) for (int i = 0; i < 4; i++) q[i] = v->a[i].n;
    if (const J* v = n.get("scale")) if (v->size() == 3) for (int i = 0; i < 3; i++) s[i] = v->a[i].n;
    double x = q[0], y = q[1], z = q[2], w = q[3];
    double R[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),     // column 0
                   2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),     // column 1
                   2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};    // column 2
    for (int c = 0; c < 3; c++) for (int rr = 0; rr < 3; rr++) r.m[c * 4 + rr] = R[c * 3 + rr] * s[c];
    r.m[12] = t[0]; r.m[13] = t[1]; r.m[14] = t[2];
    return r;
}

struct Doc {
    J root;
    std::vector<std::vector<uint8_t>> buffers;
    std::string err;
    // reads accessor `ai` as floats (comps per element), normalised integers mapped to 0..1
    bool read(int ai, std::vector<float>& out, int& comps) {
        const J* acc = root.get("accessors");
        if (!acc || ai < 0 || ai >= (int)acc->size()) { err = "bad accessor"; return false; }
        const J& a = acc->a[ai];
        std::string type = a.str("type");
        comps = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : type == "MAT4" ? 16 : 0;
        int ct = a.inum("componentType", 0);
        double cn = a.num("count", 0);
        bool norm = a.get("normalized") && a.get("normalized")->b;
        int csize = ct == 5120 || ct == 5121 ? 1 : ct == 5122 || ct == 5123 ? 2 : ct == 5125 || ct == 5126 ? 4 : 0;
        if (!comps || !csize || !(cn >= 0 && cn < 2147483648.0)) { err = "unsupported accessor"; return false; }
        const int count = (int)cn;
        const size_t elem = (size_t)comps * csize;
        // every size is checked before anything is allocated (a bogus count used to allocate first, up to 128 GB)
        if (!a.get("bufferView")) {   // all zeros (sparse accessors are not supported)
            if (count > (1 << 24)) { err = "accessor out of range"; return false; }   // (no buffer to bound it by)
            out.assign((size_t)count * comps, 0.0f);
            return true;
        }
        const J* views = root.get("bufferViews");
        int vi = a.inum("bufferView", -1);
        if (!views || vi < 0 || vi >= (int)views->size()) { err = "bad bufferView"; return false; }
        const J& v = views->a[vi];
        int bi = v.inum("buffer", 0);
        if (bi < 0 || bi >= (int)buffers.size()) { err = "bad buffer"; return false; }
        const std::vector<uint8_t>& buf = buffers[bi];
        double vo = v.num("byteOffset", 0), ao = a.num("byteOffset", 0), vs = v.num("byteStride", 0);
        if (!(vo >= 0 && ao >= 0 && vs >= 0 && vo + ao <= (double)buf.size() && vs <= 65536)) { err = "accessor out of range"; return false; }
        size_t off = (size_t)vo + (size_t)ao;
        size_t stride = (size_t)vs;
        if (!stride) stride = elem;
        // off + stride * (count - 1) + elem <= size, without overflowing
        if (count && (elem > buf.size() - off || (size_t)(count - 1) > (buf.size() - off - elem) / stride)) { err = "accessor out of range"; return false; }
        out.assign((size_t)count * comps, 0.0f);
        for (int i = 0; i < count; i++)
            for (int c = 0; c < comps; c++) {
                const uint8_t* p = &buf[off + stride * i + (size_t)c * csize];
                float f = 0;
                switch (ct) {
                    case 5126: memcpy(&f, p, 4); break;
                    case 5121: f = norm ? *p / 255.0f : *p; break;
                    case 5120: f = norm ? std::max(-1.0f, (int8_t)*p / 127.0f) : (int8_t)*p; break;
                    case 5123: { uint16_t u; memcpy(&u, p, 2); f = norm ? u / 65535.0f : u; } break;
                    case 5122: { int16_t u; memcpy(&u, p, 2); f = norm ? std::max(-1.0f, u / 32767.0f) : u; } break;
                    case 5125: { uint32_t u; memcpy(&u, p, 4); f = (float)u; } break;
                }
                out[(size_t)i * comps + c] = f;
            }
        return true;
    }
};

bool addPrimitive(Doc& d, const J& prim, const M4& xf, MeshData& out) {
    int mode = prim.inum("mode", 4);
    if (mode != 4 && mode != 5 && mode != 6) return true;   // points / lines: nothing to draw
    const J* attrs = prim.get("attributes");
    if (!attrs || !attrs->get("POSITION")) return true;
    std::vector<float> pos, nrm, col, idxf;
    int pc = 0, nc = 0, cc = 0, ic = 0;
    if (!d.read(attrs->inum("POSITION", -1), pos, pc) || pc != 3) { if (d.err.empty()) d.err = "POSITION must be VEC3"; return false; }
    size_t nv = pos.size() / 3;
    if (attrs->get("NORMAL") && (!d.read(attrs->inum("NORMAL", -1), nrm, nc) || nc != 3)) nrm.clear();
    if (attrs->get("COLOR_0") && (!d.read(attrs->inum("COLOR_0", -1), col, cc) || (cc != 3 && cc != 4))) col.clear();
    // (one per vertex or none: a shorter list was read past its end for the vertices it misses)
    if (nrm.size() != nv * 3) nrm.clear();
    if (col.size() != nv * cc) col.clear();
    std::vector<uint32_t> idx;
    if (prim.get("indices")) {
        if (!d.read(prim.inum("indices", -1), idxf, ic) || ic != 1) { if (d.err.empty()) d.err = "bad indices"; return false; }
        for (float f : idxf) idx.push_back((uint32_t)f);
    } else for (uint32_t i = 0; i < nv; i++) idx.push_back(i);
    for (uint32_t i : idx) if (i >= nv) { d.err = "index out of range"; return false; }
    std::vector<uint32_t> tris;   // strips / fans to a triangle list
    if (mode == 4) tris = idx;
    else for (size_t i = 2; i < idx.size(); i++) {
        if (mode == 5) { if (i % 2 == 0) tris.insert(tris.end(), {idx[i - 2], idx[i - 1], idx[i]}); else tris.insert(tris.end(), {idx[i - 1], idx[i - 2], idx[i]}); }
        else tris.insert(tris.end(), {idx[0], idx[i - 1], idx[i]});
    }
    // material: name (tint slot) and base colour
    MeshPrim mp;
    double base[4] = {1, 1, 1, 1};
    mp.material = "body";
    const J* mats = d.root.get("materials");
    int mi = prim.inum("material", -1);
    if (mats && mi >= 0 && mi < (int)mats->size()) {
        const J& m = mats->a[mi];
        if (!m.str("name").empty()) mp.material = m.str("name");
        if (const J* pbr = m.get("pbrMetallicRoughness"))
            if (const J* f = pbr->get("baseColorFactor")) if (f->size() == 4) for (int i = 0; i < 4; i++) base[i] = f->a[i].n;
        if (m.str("alphaMode") == "BLEND") mp.alpha = (float)base[3];
    }
    mp.slot = (uint8_t)tintSlotFromName(mp.material);
    // transform: positions by xf, normals by its inverse transpose (cofactors); a mirroring transform flips winding
    const double* m = xf.m;
    double c00 = m[5] * m[10] - m[6] * m[9], c01 = m[6] * m[8] - m[4] * m[10], c02 = m[4] * m[9] - m[5] * m[8];
    double c10 = m[9] * m[2] - m[10] * m[1], c11 = m[10] * m[0] - m[8] * m[2], c12 = m[8] * m[1] - m[9] * m[0];
    double c20 = m[1] * m[6] - m[2] * m[5], c21 = m[2] * m[4] - m[0] * m[6], c22 = m[0] * m[5] - m[1] * m[4];
    double det = m[0] * c00 + m[4] * c10 + m[8] * c20;
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        uint32_t tri[3] = {tris[t], tris[t + 1], tris[t + 2]};
        if (det < 0) std::swap(tri[1], tri[2]);
        Vec3 wp[3];
        for (int k = 0; k < 3; k++) {
            const float* p = &pos[tri[k] * 3];
            wp[k] = Vec3((float)(m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]), (float)(m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]),
                         (float)(m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]));
        }
        Vec3 flat = cross(wp[1] - wp[0], wp[2] - wp[0]);
        float fl = length(flat);
        flat = fl > 1e-12f ? flat / fl : Vec3(0, 1, 0);
        for (int k = 0; k < 3; k++) {
            uint32_t v = tri[k];
            Vec3 n = flat;
            if (!nrm.empty()) {
                const float* q = &nrm[v * 3];
                // cofactor matrix (rows c0*, c1*, c2*) is the inverse transpose times det
                Vec3 nn((float)(c00 * q[0] + c10 * q[1] + c20 * q[2]), (float)(c01 * q[0] + c11 * q[1] + c21 * q[2]), (float)(c02 * q[0] + c12 * q[1] + c22 * q[2]));
                if (det < 0) nn = nn * -1.0f;
                float l = length(nn);
                if (l > 1e-12f) n = nn / l;
            }
            float r = 1, g = 1, b = 1;
            if (!col.empty()) { r = col[v * cc]; g = col[v * cc + 1]; b = col[v * cc + 2]; }
            mp.pos.insert(mp.pos.end(), {wp[k].x, wp[k].y, wp[k].z});
            mp.nrm.insert(mp.nrm.end(), {n.x, n.y, n.z});
            mp.col.insert(mp.col.end(), {(float)(r * base[0]), (float)(g * base[1]), (float)(b * base[2])});
            mp.idx.push_back((uint32_t)mp.idx.size());
        }
    }
    if (!mp.idx.empty()) out.prims.push_back(std::move(mp));
    return true;
}

bool addNode(Doc& d, int ni, const M4& parent, MeshData& out, int depth) {
    const J* nodes = d.root.get("nodes");
    if (!nodes || ni < 0 || ni >= (int)nodes->size() || depth > 64) { d.err = "bad node"; return false; }
    const J& n = nodes->a[ni];
    M4 xf = parent * nodeMatrix(n);
    if (n.get("mesh")) {
        const J* meshes = d.root.get("meshes");
        int mi = n.inum("mesh", -1);
        if (!meshes || mi < 0 || mi >= (int)meshes->size()) { d.err = "bad mesh"; return false; }
        if (const J* prims = meshes->a[mi].get("primitives"))
            for (const J& p : prims->a) if (!addPrimitive(d, p, xf, out)) return false;
    }
    if (const J* ch = n.get("children"))
        for (const J& c : ch->a) if (!addNode(d, (int)c.n, xf, out, depth + 1)) return false;
    return true;
}

std::string fmt(double v) { char b[32]; snprintf(b, sizeof b, "%.7g", std::fabs(v) < 1e-12 ? 0.0 : v); return b; }
} // namespace

bool loadGltf(const uint8_t* data, size_t size, const GltfResolver& resolve, MeshData& out, std::string* err) {
    out.prims.clear();
    Doc d;
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    const uint8_t* json = data; size_t jsonLen = size;
    std::vector<uint8_t> glbBin;
    bool glb = size >= 12 && !memcmp(data, "glTF", 4);
    if (glb) {
        uint32_t len; memcpy(&len, data + 8, 4);
        size_t p = 12;
        json = nullptr;
        while (p + 8 <= size && p + 8 <= len) {
            uint32_t cl, ct; memcpy(&cl, data + p, 4); memcpy(&ct, data + p + 4, 4);
            if (p + 8 + cl > size) return fail("truncated .glb");
            if (ct == 0x4E4F534A) { json = data + p + 8; jsonLen = cl; }        // JSON
            else if (ct == 0x004E4942) glbBin.assign(data + p + 8, data + p + 8 + cl); // BIN
            p += 8 + ((cl + 3) & ~3u);
        }
        if (!json) return fail(".glb without JSON");
    }
    Parser ps{(const char*)json, (const char*)json + jsonLen};
    d.root = ps.value();
    if (ps.bad || d.root.t != J::OBJ) return fail("not valid glTF JSON");
    if (const J* bufs = d.root.get("buffers"))
        for (size_t i = 0; i < bufs->size(); i++) {
            const J& b = bufs->a[i];
            std::string uri = b.str("uri");
            std::vector<uint8_t> bytes;
            if (uri.empty()) { if (!glb || i != 0) return fail("buffer without data"); bytes = glbBin; }
            else if (uri.compare(0, 5, "data:") == 0) {
                size_t comma = uri.find(',');
                if (comma == std::string::npos || uri.find(";base64") == std::string::npos || !base64Decode(uri.substr(comma + 1), bytes)) return fail("bad data: URI");
            } else if (!resolve || !resolve(uri, bytes)) return fail("missing buffer file " + uri);
            d.buffers.push_back(std::move(bytes));
        }
    const J* scenes = d.root.get("scenes");
    M4 id;
    bool ok = true;
    if (scenes && scenes->size()) {
        int si = d.root.inum("scene", 0);
        if (si < 0 || si >= (int)scenes->size()) si = 0;
        if (const J* roots = scenes->a[si].get("nodes"))
            for (const J& r : roots->a) if (!(ok = addNode(d, (int)r.n, id, out, 0))) break;
    } else if (const J* meshes = d.root.get("meshes")) {   // no scene: every mesh as is
        for (const J& m : meshes->a) {
            if (const J* prims = m.get("primitives"))
                for (const J& p : prims->a) if (!(ok = addPrimitive(d, p, id, out))) break;
            if (!ok) break;
        }
    }
    if (!ok) return fail(d.err);
    if (out.prims.empty()) return fail("no triangles");
    return true;
}

std::string writeGltf(const MeshData& mesh, const std::string& name) {
    std::vector<uint8_t> bin;
    auto put = [&](const void* p, size_t n) { size_t at = bin.size(); bin.resize(at + n); memcpy(&bin[at], p, n); while (bin.size() % 4) bin.push_back(0); return at; };
    std::string views, accs, prims, mats;
    int nv = 0, na = 0;
    auto view = [&](size_t off, size_t len, int target) {
        if (nv) views += ",";
        views += "{\"buffer\":0,\"byteOffset\":" + std::to_string(off) + ",\"byteLength\":" + std::to_string(len) + ",\"target\":" + std::to_string(target) + "}";
        return nv++;
    };
    auto accessor = [&](int v, int ctype, size_t count, const char* type, const std::string& extra) {
        if (na) accs += ",";
        accs += "{\"bufferView\":" + std::to_string(v) + ",\"componentType\":" + std::to_string(ctype) + ",\"count\":" + std::to_string(count) +
                ",\"type\":\"" + type + "\"" + extra + "}";
        return na++;
    };
    for (size_t i = 0; i < mesh.prims.size(); i++) {
        const MeshPrim& p = mesh.prims[i];
        size_t n = p.pos.size() / 3;
        Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
        for (size_t k = 0; k < n; k++) { Vec3 v(p.pos[k * 3], p.pos[k * 3 + 1], p.pos[k * 3 + 2]); mn = vmin(mn, v); mx = vmax(mx, v); }
        int aPos = accessor(view(put(p.pos.data(), p.pos.size() * 4), p.pos.size() * 4, 34962), 5126, n, "VEC3",
                            ",\"min\":[" + fmt(mn.x) + "," + fmt(mn.y) + "," + fmt(mn.z) + "],\"max\":[" + fmt(mx.x) + "," + fmt(mx.y) + "," + fmt(mx.z) + "]");
        int aNrm = accessor(view(put(p.nrm.data(), p.nrm.size() * 4), p.nrm.size() * 4, 34962), 5126, n, "VEC3", "");
        int aCol = accessor(view(put(p.col.data(), p.col.size() * 4), p.col.size() * 4, 34962), 5126, n, "VEC3", "");
        int aIdx = accessor(view(put(p.idx.data(), p.idx.size() * 4), p.idx.size() * 4, 34963), 5125, p.idx.size(), "SCALAR", "");
        if (i) { prims += ","; mats += ","; }
        prims += "{\"attributes\":{\"POSITION\":" + std::to_string(aPos) + ",\"NORMAL\":" + std::to_string(aNrm) + ",\"COLOR_0\":" + std::to_string(aCol) +
                 "},\"indices\":" + std::to_string(aIdx) + ",\"material\":" + std::to_string(i) + ",\"mode\":4}";
        mats += "{\"name\":\"" + p.material + "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1," + fmt(p.alpha) +
                "],\"metallicFactor\":0,\"roughnessFactor\":1}" + (p.alpha < 1 ? ",\"alphaMode\":\"BLEND\"" : "") + "}";
    }
    std::string s = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"quark exportassets\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
                    "\"nodes\":[{\"name\":\"" + name + "\",\"mesh\":0}],\"meshes\":[{\"name\":\"" + name + "\",\"primitives\":[" + prims + "]}],"
                    "\"materials\":[" + mats + "],\"accessors\":[" + accs + "],\"bufferViews\":[" + views + "],"
                    "\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + ",\"uri\":\"data:application/octet-stream;base64," +
                    base64Encode(bin.data(), bin.size()) + "\"}]}\n";
    return s;
}
