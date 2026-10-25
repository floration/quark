// Movement equations: the parser, the static resolver, the bytecode compiler and its interpreter (moveexpr.h).
#include "moveexpr.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include "pmove.h"

namespace mexpr {

// ---- settings and inputs ------------------------------------------------------------------------------------------
int settingIndex(const std::string& key) {
    if (key == "gravity") return 0;
    if (key == "speed") return 1;
    if (key == "jump") return 2;
    for (int k = 0; k < MV_COUNT; k++) if (key == moveParamKey(k)) return 3 + k;
    return -1;
}
const char* settingKey(int s) { return s == 0 ? "gravity" : s == 1 ? "speed" : s == 2 ? "jump" : s < kSettings ? moveParamKey(s - 3) : "?"; }
float settingMin(int s) { return s == 0 ? 0.1f : s <= 2 ? 0.25f : 0.1f; }
float settingMax(int s) { return s <= 2 ? 3.0f : 5.0f; }
float settingBase(int s) { return s == 0 ? moveBaseGravity() : s == 1 ? moveBaseSpeed() : s == 2 ? moveBaseJump() : moveParamBase(s - 3); }

static const InputInfo kInputs[P_COUNT] = {
    {"speed", "m/s", 0, 50, 0, "speed (3D)"},
    {"hspeed", "m/s", 0, 50, 0, "horizontal speed"},
    {"vspeed", "m/s", -40, 20, 0, "vertical speed (up is +)"},
    {"air", "s", 0, 3, 0, "time in the air since leaving the floor (0 on the floor)"},
    {"ground", "s", 0, 3, 0, "time on the floor since landing (0 in the air)"},
    {"onground", "", 0, 1, 1, "1 on the floor, 0 in the air"},
    {"charges", "", 0, 10, (float)kDashCharges, "dash charges held"},
    {"dashing", "", 0, 1, 0, "1 while a dash runs"},
    {"hooked", "", 0, 1, 0, "1 while your grapple holds"},
    {"height", "m", 0, 50, 0, "height above the floor below you (0 on it, 50 at most)"},
};
const InputInfo& inputInfo(int i) { return kInputs[i < 0 || i >= P_COUNT ? 0 : i]; }
void restInputs(double* in) { for (int i = 0; i < P_COUNT; i++) in[i] = kInputs[i].rest; }

namespace {
enum Code : uint8_t {
    C_CONST, C_INPUT, C_SET,
    C_NEG, C_NOT, C_SQRT, C_ABS, C_EXP, C_LOG, C_FLOOR, C_CEIL, C_ROUND, C_SIGN,     // 1 operand
    C_ADD, C_SUB, C_MUL, C_DIV, C_POW, C_LT, C_LE, C_GT, C_GE, C_EQ, C_NE, C_AND, C_OR,
    C_MIN, C_MAX, C_STEP, C_EASE,                                                    // 2 operands (C_EASE: arg = mode)
    C_SEL, C_CLAMP, C_LERP,                                                          // 3
    C_CURVE,                                                                         // 5 (arg = mode)
    C_CURVEK,                                                                        // x, with x0 x1 y0 y1 at consts[idx..]
    // compiled only (never in a tree): one operand a constant from the pool (consts[idx]), so it isn't pushed
    C_ADDK, C_SUBK, C_RSUBK, C_MULK, C_DIVK, C_RDIVK,                               // x+k, x-k, k-x, x*k, x/k, k/x
    C_OPK,                                                                           // op2(arg, x, k)
    C_SELK,                                                                          // c ? consts[idx] : consts[idx + 1]
};
int arity(uint8_t c) { return c <= C_SET ? 0 : c <= C_SIGN ? 1 : c <= C_EASE ? 2 : c <= C_LERP ? 3 : c == C_CURVE ? 5 : 1; }

const std::vector<const char*> kEase = {"linear", "in", "out", "inout", "in3", "out3", "inout3", "smooth", "smoother",
                                        "expo", "outexpo", "back", "outback"};
}

const std::vector<const char*>& easeModes() { return kEase; }

const std::vector<FuncInfo>& functions() {
    static const std::vector<FuncInfo> f = {
        {"sqrt", 1, 1, "sqrt(x)", "square root (NaN below 0)"},
        {"abs", 1, 1, "abs(x)", "absolute value"},
        {"min", 2, 8, "min(a, b, ...)", "the smallest"},
        {"max", 2, 8, "max(a, b, ...)", "the largest"},
        {"pow", 2, 2, "pow(a, b)", "a to the power b (same as a ^ b)"},
        {"exp", 1, 1, "exp(x)", "e to the power x"},
        {"log", 1, 1, "log(x)", "natural logarithm (NaN below 0)"},
        {"floor", 1, 1, "floor(x)", "round down"},
        {"ceil", 1, 1, "ceil(x)", "round up"},
        {"round", 1, 1, "round(x)", "round to nearest (halves away from 0)"},
        {"sign", 1, 1, "sign(x)", "-1, 0 or 1"},
        {"clamp", 3, 3, "clamp(x, lo, hi)", "x held to lo..hi"},
        {"lerp", 3, 3, "lerp(a, b, t)", "a + (b - a) t (t not clamped)"},
        {"step", 2, 2, "step(edge, x)", "1 when x >= edge, else 0"},
        {"ease", 1, 2, "ease(t, \"mode\")", "an easing of t (clamped to 0..1); mode defaults to \"linear\""},
        {"curve", 5, 6, "curve(x, x0, x1, y0, y1, \"mode\")", "x0..x1 mapped to y0..y1 with that easing, flat outside (mode defaults to \"linear\")"},
    };
    return f;
}

// ---- deterministic math -----------------------------------------------------------------------------------------
namespace {
constexpr double kLn2Hi = 6.93147180369123816490e-01, kLn2Lo = 1.90821492927058770002e-10, kLog2e = 1.44269504088896338700e+00;
constexpr double kLn2 = 6.93147180559945286227e-01;
double pow2i(int k) {   // 2^k for k in -1022..1023, exactly (built from the bits: no ldexp)
    uint64_t b = (uint64_t)(k + 1023) << 52;
    double d;
    memcpy(&d, &b, 8);
    return d;
}
double expPoly(double r) {   // e^r for |r| <= ~0.35: Taylor to r^14 (the next term is < 1e-19)
    static const double c[15] = {1.0, 1.0, 1.0 / 2, 1.0 / 6, 1.0 / 24, 1.0 / 120, 1.0 / 720, 1.0 / 5040, 1.0 / 40320, 1.0 / 362880,
                                 1.0 / 3628800, 1.0 / 39916800, 1.0 / 479001600, 1.0 / 6227020800.0, 1.0 / 87178291200.0};
    double p = c[14];
    for (int i = 13; i >= 0; i--) p = p * r + c[i];
    return p;
}
double scale2(double p, int k) {   // p * 2^k, p in about 0.7..1.42
    if (k > 1023) { p *= 2; k--; }
    if (k < -1022) return 0.0;      // (the multipliers never get near this)
    return p * pow2i(k);
}
} // namespace

double detExp(double x) {
    if (x != x) return x;
    if (x > 709.78) return HUGE_VAL;
    if (x < -708.0) return 0.0;
    const double kd = std::floor(x * kLog2e + 0.5);
    const double r = (x - kd * kLn2Hi) - kd * kLn2Lo;
    return scale2(expPoly(r), (int)kd);
}
double detExp2(double x) {
    if (x != x) return x;
    if (x >= 1024.0) return HUGE_VAL;
    if (x < -1022.0) return 0.0;
    const double kd = std::floor(x + 0.5);
    return scale2(expPoly((x - kd) * kLn2), (int)kd);
}
double detLog(double x) {
    if (x != x || x < 0) return NAN;
    if (x == 0) return -HUGE_VAL;
    if (x == HUGE_VAL) return x;
    int e = 0;
    if (x < 2.2250738585072014e-308) { x *= 18014398509481984.0; e = -54; }   // (subnormal: x 2^54 first)
    uint64_t b;
    memcpy(&b, &x, 8);
    e += (int)((b >> 52) & 0x7ff) - 1022;
    b = (b & 0x800fffffffffffffull) | (uint64_t)1022 << 52;   // m in [0.5, 1)
    double m;
    memcpy(&m, &b, 8);
    if (m < 0.70710678118654752440) { m *= 2; e--; }       // m in [0.707, 1.414)
    const double s = (m - 1) / (m + 1), s2 = s * s;          // |s| <= 0.1716
    double p = 1.0 / 21;
    for (int k = 19; k >= 1; k -= 2) p = p * s2 + 1.0 / k;   // 1 + s2/3 + s2^2/5 + ... + s2^10/21
    const double lnm = 2 * s * p;
    return e * kLn2Hi + (lnm + e * kLn2Lo);
}
double detPow(double a, double b) {
    if (b == 0) return 1.0;
    if (a != a || b != b) return NAN;
    const bool integer = std::floor(b) == b;
    const double nb = std::fabs(b);
    if (nb <= 64 && (integer || (a >= 0 && std::floor(nb * 2) == nb * 2))) {   // (exact squarings, x^2 is x * x; a half: x sqrt x)
        int n = (int)nb;
        double r = 1, x = a;
        while (n) { if (n & 1) r *= x; x *= x; n >>= 1; }
        if (!integer) r *= std::sqrt(a);
        return b < 0 ? 1 / r : r;
    }
    if (a == 0) return b > 0 ? 0.0 : HUGE_VAL;
    if (a < 0) {
        if (!integer) return NAN;
        const double h = b * 0.5, r = detExp(b * detLog(-a));
        return std::floor(h) != h ? -r : r;
    }
    return detExp(b * detLog(a));
}

double ease(int mode, double t) {
    if (t != t) return t;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    constexpr double c1 = 1.70158, c3 = c1 + 1;
    const double u = 1 - t;
    switch (mode) {
    default: case 0: return t;
    case 1: return t * t;
    case 2: return 1 - u * u;
    case 3: return t < 0.5 ? 2 * t * t : 1 - 2 * u * u;
    case 4: return t * t * t;
    case 5: return 1 - u * u * u;
    case 6: return t < 0.5 ? 4 * t * t * t : 1 - 4 * u * u * u;
    case 7: return t * t * (3 - 2 * t);
    case 8: return t * t * t * (t * (t * 6 - 15) + 10);
    case 9: return t == 0 ? 0.0 : detExp2(10 * t - 10);
    case 10: return t == 1 ? 1.0 : 1 - detExp2(-10 * t);
    case 11: return c3 * t * t * t - c1 * t * t;
    case 12: return 1 - (c3 * u * u * u - c1 * u * u);
    }
}

namespace {
double curveFn(double x, double x0, double x1, double y0, double y1, int mode) {
    const double t = x1 == x0 ? (x < x0 ? 0.0 : 1.0) : (x - x0) / (x1 - x0);
    return y0 + (y1 - y0) * ease(mode, t);
}
double sel(bool c) { return c ? 1.0 : 0.0; }
// One shared definition of every operation: the folder (static parts) and the interpreter give the same bits.
inline double op1(uint8_t c, double a) {
    switch (c) {
    case C_NEG: return -a;
    case C_NOT: return sel(a == 0);
    case C_SQRT: return std::sqrt(a);
    case C_ABS: return std::fabs(a);
    case C_EXP: return detExp(a);
    case C_LOG: return detLog(a);
    case C_FLOOR: return std::floor(a);
    case C_CEIL: return std::ceil(a);
    case C_ROUND: return std::round(a);
    default: return a > 0 ? 1.0 : a < 0 ? -1.0 : a;   // C_SIGN (0 and NaN as they are)
    }
}
inline double op2(uint8_t c, uint8_t arg, double a, double b) {
    switch (c) {
    case C_ADD: return a + b;
    case C_SUB: return a - b;
    case C_MUL: return a * b;
    case C_DIV: return a / b;
    case C_POW: return detPow(a, b);
    case C_LT: return sel(a < b);
    case C_LE: return sel(a <= b);
    case C_GT: return sel(a > b);
    case C_GE: return sel(a >= b);
    case C_EQ: return sel(a == b);
    case C_NE: return sel(a != b);
    case C_AND: return sel(a != 0 && b != 0);
    case C_OR: return sel(a != 0 || b != 0);
    case C_MIN: return b < a ? b : a;
    case C_MAX: return b > a ? b : a;
    case C_STEP: return sel(b >= a);
    default: return ease(arg, a);   // C_EASE (b unused: the mode is the arg)
    }
}
inline double op3(uint8_t c, double a, double b, double d) {
    switch (c) {
    case C_SEL: return a != 0 ? b : d;
    case C_CLAMP: return a < b ? b : a > d ? d : a;
    default: return a + (b - a) * d;   // C_LERP
    }
}
inline float finish(double v, float lo, float hi, float fallback) {
    if (v != v) return fallback;
    return (float)(v < lo ? (double)lo : v > hi ? (double)hi : v);
}
} // namespace

void Program::run(const double* in, MoveTuning& t) const {
    double st[32];
    const Op* ops0 = ops.data();
    const double* k = consts.data();
    for (const Item& it : items) {
        int sp = 0;
        for (const Op *o = ops0 + it.first, *e = o + it.count; o < e; o++) {
            const uint8_t c = o->code;
            switch (c) {
            case C_CONST: st[sp++] = k[o->idx]; break;
            case C_INPUT: st[sp++] = in[o->idx]; break;
            case C_SET: st[sp++] = settingGet(t, o->idx); break;
            case C_NEG: case C_NOT: case C_SQRT: case C_ABS: case C_EXP: case C_LOG: case C_FLOOR: case C_CEIL: case C_ROUND: case C_SIGN:
                st[sp - 1] = op1(c, st[sp - 1]); break;
            case C_ADD: sp--; st[sp - 1] = st[sp - 1] + st[sp]; break;   // (the commonest, inline)
            case C_MUL: sp--; st[sp - 1] = st[sp - 1] * st[sp]; break;
            case C_SEL: case C_CLAMP: case C_LERP: sp -= 2; st[sp - 1] = op3(c, st[sp - 1], st[sp], st[sp + 1]); break;
            case C_CURVE: sp -= 4; st[sp - 1] = curveFn(st[sp - 1], st[sp], st[sp + 1], st[sp + 2], st[sp + 3], o->arg); break;
            case C_CURVEK: st[sp - 1] = curveFn(st[sp - 1], k[o->idx], k[o->idx + 1], k[o->idx + 2], k[o->idx + 3], o->arg); break;
            case C_EASE: st[sp - 1] = ease(o->arg, st[sp - 1]); break;
            case C_ADDK: st[sp - 1] = st[sp - 1] + k[o->idx]; break;
            case C_SUBK: st[sp - 1] = st[sp - 1] - k[o->idx]; break;
            case C_RSUBK: st[sp - 1] = k[o->idx] - st[sp - 1]; break;
            case C_MULK: st[sp - 1] = st[sp - 1] * k[o->idx]; break;
            case C_DIVK: st[sp - 1] = st[sp - 1] / k[o->idx]; break;
            case C_RDIVK: st[sp - 1] = k[o->idx] / st[sp - 1]; break;
            case C_OPK: st[sp - 1] = op2(o->arg, 0, st[sp - 1], k[o->idx]); break;
            case C_SELK: st[sp - 1] = st[sp - 1] != 0 ? k[o->idx] : k[o->idx + 1]; break;
            default: sp--; st[sp - 1] = op2(c, o->arg, st[sp - 1], st[sp]); break;
            }
        }
        settingSet(t, it.setting, finish(st[0], it.lo, it.hi, it.fallback));
    }
}

// ---- parser ---------------------------------------------------------------------------------------------------
namespace {
enum Kind : uint8_t { K_NUM, K_INPUT, K_SET, K_STR, K_OP };
struct Node { Kind kind; uint8_t code = 0, arg = 0; double num = 0; int idx = 0; std::vector<int> kids; explicit Node(Kind k) : kind(k) {} };
constexpr int kMaxNodes = 256, kMaxDepth = 100, kMaxStack = 32;   // (depth: about 50 nested brackets, a few KB of stack)

// Decimal text to a double by our own arithmetic (strtod depends on the locale, and not every C library rounds it the
// same way): up to 19 significant digits as an integer, then one multiply or divide by an exact power of ten (10^22 at
// most is exact; beyond, repeated). The same bits on every platform; within an ulp or two of the nearest double.
double decimal(const std::string& t) {
    uint64_t mant = 0;
    int digits = 0, e10 = 0;
    size_t i = 0;
    bool point = false;
    for (; i < t.size(); i++) {
        const char c = t[i];
        if (c == '.') { point = true; continue; }
        if (c < '0' || c > '9') break;
        if (digits < 19) { if (mant || c != '0') { mant = mant * 10 + (uint64_t)(c - '0'); digits++; } if (point) e10--; }
        else if (!point) e10++;
    }
    if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
        i++;
        int sign = 1, ev = 0;
        if (i < t.size() && (t[i] == '+' || t[i] == '-')) sign = t[i++] == '-' ? -1 : 1;
        for (; i < t.size() && t[i] >= '0' && t[i] <= '9'; i++) if (ev < 10000) ev = ev * 10 + (t[i] - '0');
        e10 += sign * ev;
    }
    double v = (double)mant;
    if (mant == 0) return 0.0;
    static const double p10[23] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16,
                                   1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    while (e10 > 22) { v *= 1e22; e10 -= 22; if (v > 1e300) return HUGE_VAL; }
    while (e10 < -22) { v /= 1e22; e10 += 22; if (v == 0) return 0.0; }
    return e10 >= 0 ? v * p10[e10] : v / p10[-e10];
}

struct Parser {
    const std::string& s;
    std::vector<Node>& nodes;
    size_t i = 0;
    int depth = 0;
    std::string err;
    Parser(const std::string& text, std::vector<Node>& n) : s(text), nodes(n) {}

    bool fail(const std::string& m, size_t at) { if (err.empty()) err = "col " + std::to_string(at + 1) + ": " + m; return false; }
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++; }
    bool eat(const char* t) { ws(); size_t n = strlen(t); if (s.compare(i, n, t) == 0) { i += n; return true; } return false; }
    int add(Node n) {
        if ((int)nodes.size() >= kMaxNodes) { fail("too long (at most " + std::to_string(kMaxNodes) + " parts)", i); return -1; }
        nodes.push_back(std::move(n));
        return (int)nodes.size() - 1;
    }
    int op(uint8_t code, std::vector<int> kids, uint8_t arg = 0) {
        for (int k : kids) if (nodes[k].kind == K_STR) { fail(kStrOnly, i); return -1; }
        Node n(K_OP); n.code = code; n.arg = arg; n.kids = std::move(kids); return add(std::move(n));
    }
    static constexpr const char* kStrOnly = "a quoted name is only an ease mode (the last argument of ease / curve)";
    static bool identStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
    static bool identChar(char c) { return identStart(c) || (c >= '0' && c <= '9'); }

    int parse() {
        int r = cond();
        ws();
        if (r >= 0 && i < s.size()) { fail(std::string("unexpected '") + s[i] + "'", i); return -1; }
        if (r >= 0 && nodes[r].kind == K_STR) { fail(kStrOnly, 0); return -1; }
        return err.empty() ? r : -1;
    }
    int cond() {
        if (++depth > kMaxDepth) { fail("nested too deep", i); return -1; }
        int c = orx();
        if (c >= 0 && eat("?")) {
            int a = cond();
            if (a < 0) return -1;
            if (!eat(":")) { fail("expected ':' of '? :'", i); return -1; }
            int b = cond();
            if (b < 0) return -1;
            c = op(C_SEL, {c, a, b});
        }
        depth--;
        return c;
    }
    int orx() {
        int a = andx();
        while (a >= 0 && eat("||")) { int b = andx(); if (b < 0) return -1; a = op(C_OR, {a, b}); }
        return a;
    }
    int andx() {
        int a = cmp();
        while (a >= 0 && eat("&&")) { int b = cmp(); if (b < 0) return -1; a = op(C_AND, {a, b}); }
        return a;
    }
    int cmp() {
        int a = sum();
        if (a < 0) return -1;
        static const struct { const char* t; Code c; } ops[] = {{"<=", C_LE}, {">=", C_GE}, {"==", C_EQ}, {"!=", C_NE}, {"<", C_LT}, {">", C_GT}};
        for (auto& o : ops)
            if (eat(o.t)) { int b = sum(); if (b < 0) return -1; return op(o.c, {a, b}); }
        return a;
    }
    int sum() {
        int a = prod();
        while (a >= 0) {
            Code c;
            if (eat("+")) c = C_ADD; else if (eat("-")) c = C_SUB; else break;
            int b = prod();
            if (b < 0) return -1;
            a = op(c, {a, b});
        }
        return a;
    }
    int prod() {
        int a = unary();
        while (a >= 0) {
            Code c;
            if (eat("*")) c = C_MUL; else if (eat("/")) c = C_DIV; else break;
            int b = unary();
            if (b < 0) return -1;
            a = op(c, {a, b});
        }
        return a;
    }
    int unary() {
        if (++depth > kMaxDepth) { fail("nested too deep", i); return -1; }
        int r;
        ws();
        if (eat("-")) { int a = unary(); r = a < 0 ? -1 : op(C_NEG, {a}); }
        else if (eat("+")) r = unary();
        else if (i < s.size() && s[i] == '!' && !(i + 1 < s.size() && s[i + 1] == '=')) { i++; int a = unary(); r = a < 0 ? -1 : op(C_NOT, {a}); }
        else r = power();
        depth--;
        return r;
    }
    int power() {   // right-associative, binds tighter than unary minus on its left: -2^2 = -4, 2^-1 = 0.5
        int a = primary();
        if (a >= 0 && eat("^")) { int b = unary(); if (b < 0) return -1; a = op(C_POW, {a, b}); }
        return a;
    }
    int primary() {
        ws();
        if (i >= s.size()) { fail("unexpected end", i); return -1; }
        const size_t at = i;
        const char c = s[i];
        if (c == '(') {
            i++;
            int a = cond();
            if (a < 0) return -1;
            if (!eat(")")) { fail("expected ')'", i); return -1; }
            return a;
        }
        if ((c >= '0' && c <= '9') || c == '.') {
            size_t j = i;
            while (j < s.size() && s[j] >= '0' && s[j] <= '9') j++;
            if (j < s.size() && s[j] == '.') { j++; while (j < s.size() && s[j] >= '0' && s[j] <= '9') j++; }
            if (j == i + 1 && c == '.') { fail("expected a number", at); return -1; }
            if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
                size_t k = j + 1;
                if (k < s.size() && (s[k] == '+' || s[k] == '-')) k++;
                if (k < s.size() && s[k] >= '0' && s[k] <= '9') { while (k < s.size() && s[k] >= '0' && s[k] <= '9') k++; j = k; }
            }
            Node n(K_NUM);
            n.num = decimal(s.substr(i, j - i));
            i = j;
            if (!std::isfinite(n.num)) { fail("number out of range", at); return -1; }
            return add(n);
        }
        if (c == '"' || c == '\'') {
            size_t j = s.find(c, i + 1);
            if (j == std::string::npos) { fail("unclosed string", at); return -1; }
            Node n(K_STR);
            std::string m = s.substr(i + 1, j - i - 1);
            i = j + 1;
            for (size_t k = 0; k < kEase.size(); k++) if (m == kEase[k]) { n.arg = (uint8_t)k; return add(n); }
            std::string list;
            for (const char* e : kEase) list += std::string(list.empty() ? "" : ", ") + e;
            fail("unknown ease mode \"" + m + "\" (" + list + ")", at);
            return -1;
        }
        if (!identStart(c)) { fail(std::string("unexpected '") + c + "'", at); return -1; }
        size_t j = i;
        while (j < s.size() && identChar(s[j])) j++;
        std::string name = s.substr(i, j - i);
        i = j;
        if ((name == "p" || name == "base") && i < s.size() && s[i] == '.') {
            size_t k = ++i;
            while (k < s.size() && identChar(s[k])) k++;
            std::string sub = s.substr(i, k - i);
            i = k;
            if (name == "p") {
                for (int q = 0; q < P_COUNT; q++) if (sub == kInputs[q].name) { Node n(K_INPUT); n.idx = q; return add(n); }
                std::string list;
                for (auto& q : kInputs) list += std::string(list.empty() ? "" : ", ") + "p." + q.name;
                fail("unknown input 'p." + sub + "' (" + list + ")", at);
                return -1;
            }
            int st = settingIndex(sub);
            if (st < 0) { fail("unknown setting 'base." + sub + "'", at); return -1; }
            Node n(K_NUM);
            n.num = settingBase(st);
            return add(n);
        }
        ws();
        if (i < s.size() && s[i] == '(') {
            i++;
            const FuncInfo* f = nullptr;
            for (auto& fi : functions()) if (name == fi.name) f = &fi;
            if (!f) { fail("unknown function '" + name + "'", at); return -1; }
            std::vector<int> args;
            ws();
            if (!(i < s.size() && s[i] == ')'))
                for (;;) {
                    int a = cond();
                    if (a < 0) return -1;
                    args.push_back(a);
                    if (eat(",")) continue;
                    break;
                }
            if (!eat(")")) { fail("expected ')' or ',' in " + name + "()", i); return -1; }
            int n = (int)args.size();
            if (n < f->minArgs || n > f->maxArgs) { fail(name + "() takes " + (f->minArgs == f->maxArgs ? std::to_string(f->minArgs) : std::to_string(f->minArgs) + " to " + std::to_string(f->maxArgs)) + " arguments: " + f->sig, at); return -1; }
            const bool easing = name == "ease" || name == "curve";
            uint8_t mode = 0;
            for (int k = 0; k < n; k++) {
                const bool isStr = nodes[args[k]].kind == K_STR;
                const bool modeSlot = easing && k == (name == "ease" ? 1 : 5);
                if (isStr != modeSlot) { fail(modeSlot ? name + "()'s mode must be a quoted name, e.g. \"inout\"" : kStrOnly, at); return -1; }
                if (isStr) mode = nodes[args[k]].arg;
            }
            if (easing && n == (name == "ease" ? 2 : 6)) args.pop_back();
            if (name == "sqrt") return op(C_SQRT, args);
            if (name == "abs") return op(C_ABS, args);
            if (name == "exp") return op(C_EXP, args);
            if (name == "log") return op(C_LOG, args);
            if (name == "floor") return op(C_FLOOR, args);
            if (name == "ceil") return op(C_CEIL, args);
            if (name == "round") return op(C_ROUND, args);
            if (name == "sign") return op(C_SIGN, args);
            if (name == "pow") return op(C_POW, args);
            if (name == "step") return op(C_STEP, args);
            if (name == "clamp") return op(C_CLAMP, args);
            if (name == "lerp") return op(C_LERP, args);
            if (name == "ease") { Node z(K_NUM); int zi = add(z); if (zi < 0) return -1; return op(C_EASE, {args[0], zi}, mode); }
            if (name == "curve") return op(C_CURVE, args, mode);
            // min / max: a chain of pairs
            const Code code = name == "min" ? C_MIN : C_MAX;
            int a = args[0];
            for (int k = 1; k < n && a >= 0; k++) a = op(code, {a, args[k]});
            return a;
        }
        if (name == "pi") { Node n(K_NUM); n.num = 3.14159265358979323846; return add(n); }
        int st = settingIndex(name);
        if (st >= 0) { Node n(K_SET); n.idx = st; return add(n); }
        fail("unknown name '" + name + "' (a setting key, p.<input>, base.<key>, pi, or a function call)", at);
        return -1;
    }
};

struct Expr {
    int setting = -1;
    std::vector<Node> nodes;
    int root = -1;
    bool ok = false, dynamic = false, cyclic = false;
    std::vector<int> deps;          // settings referred to (each once)
    uint32_t inputs = 0, allInputs = 0;
};

void collect(Expr& e, int n) {
    const Node& d = e.nodes[n];
    if (d.kind == K_INPUT) e.inputs |= 1u << d.idx;
    if (d.kind == K_SET) { bool have = false; for (int s : e.deps) have |= s == d.idx; if (!have) e.deps.push_back(d.idx); }
    for (int k : d.kids) collect(e, k);
}

// Is this subtree a constant (no inputs, no dynamic setting)? dyn[s]: setting s is dynamic.
bool isConst(const Expr& e, int n, const bool* dyn) {
    const Node& d = e.nodes[n];
    if (d.kind == K_INPUT) return false;
    if (d.kind == K_SET) return !dyn[d.idx];
    for (int k : d.kids) if (!isConst(e, k, dyn)) return false;
    return true;
}
// Evaluates a constant subtree (settings read from t: the static ones are already resolved there).
double evalNode(const Expr& e, int n, const MoveTuning& t) {
    const Node& d = e.nodes[n];
    switch (d.kind) {
    case K_NUM: return d.num;
    case K_SET: return settingGet(t, d.idx);
    case K_INPUT: case K_STR: return NAN;   // (never: constant() excluded inputs; strings are modes)
    default: break;
    }
    double v[5] = {};
    for (size_t k = 0; k < d.kids.size() && k < 5; k++) v[k] = evalNode(e, d.kids[k], t);
    switch (arity(d.code)) {
    case 1: return op1(d.code, v[0]);
    case 2: return op2(d.code, d.arg, v[0], v[1]);
    case 3: return op3(d.code, v[0], v[1], v[2]);
    default: return curveFn(v[0], v[1], v[2], v[3], v[4], d.arg);   // C_CURVE
    }
}

struct Emitter {
    Program& p;
    const Expr& e;
    const MoveTuning& t;
    const bool* dyn;
    int sp = 0, maxSp = 0;
    bool ok = true;
    void push(Op o, int delta) {
        if (p.ops.size() >= 65535 || p.consts.size() >= 65000) { ok = false; return; }
        p.ops.push_back(o);
        sp += delta;
        if (sp > maxSp) maxSp = sp;
    }
    uint16_t constant(double v) { p.consts.push_back(v); return (uint16_t)(p.consts.size() - 1); }
    void emit(int n) {
        if (!ok) return;
        const Node& d = e.nodes[n];
        if (isConst(e, n, dyn)) { push({C_CONST, 0, constant(evalNode(e, n, t))}, 1); return; }
        if (d.kind == K_INPUT) { push({C_INPUT, 0, (uint16_t)d.idx}, 1); p.inputs |= 1u << d.idx; return; }
        if (d.kind == K_SET) { push({C_SET, 0, (uint16_t)d.idx}, 1); return; }
        if (d.code == C_CURVE) {   // bounds all constant (the usual case): one op that reads them from the pool
            bool k = true;
            for (size_t q = 1; q < 5; q++) k &= isConst(e, d.kids[q], dyn);
            if (k) {
                emit(d.kids[0]);
                uint16_t at = constant(evalNode(e, d.kids[1], t));
                for (size_t q = 2; q < 5; q++) constant(evalNode(e, d.kids[q], t));
                push({C_CURVEK, d.arg, at}, 0);
                return;
            }
        }
        if (d.code == C_EASE) { emit(d.kids[0]); push({C_EASE, d.arg, 0}, 0); return; }
        if (d.code == C_SEL && isConst(e, d.kids[1], dyn) && isConst(e, d.kids[2], dyn)) {   // c ? k1 : k2
            emit(d.kids[0]);
            uint16_t at = constant(evalNode(e, d.kids[1], t));
            constant(evalNode(e, d.kids[2], t));
            push({C_SELK, 0, at}, 0);
            return;
        }
        if (arity(d.code) == 2 && d.code != C_EASE) {   // one side constant: no push for it (IEEE + * == != && || commute exactly)
            const bool kr = isConst(e, d.kids[1], dyn), kl = isConst(e, d.kids[0], dyn);
            if (kr || kl) {
                const uint8_t c = d.code;
                uint8_t code = 0, arg = 0;
                bool swap = false;
                if (kr) {
                    code = c == C_ADD ? C_ADDK : c == C_SUB ? C_SUBK : c == C_MUL ? C_MULK : c == C_DIV ? C_DIVK : C_OPK;
                    arg = c;
                } else {
                    swap = true;
                    switch (c) {
                    case C_ADD: code = C_ADDK; break;
                    case C_MUL: code = C_MULK; break;
                    case C_SUB: code = C_RSUBK; break;
                    case C_DIV: code = C_RDIVK; break;
                    case C_EQ: case C_NE: case C_AND: case C_OR: code = C_OPK; arg = c; break;
                    case C_LT: code = C_OPK; arg = C_GT; break;   // k < x is x > k (NaN: false both ways)
                    case C_GT: code = C_OPK; arg = C_LT; break;
                    case C_LE: code = C_OPK; arg = C_GE; break;
                    case C_GE: code = C_OPK; arg = C_LE; break;
                    default: swap = false; break;   // (pow, min, max, step with the constant first: the general way)
                    }
                }
                if (code) {
                    emit(d.kids[swap ? 1 : 0]);
                    push({code, arg, constant(evalNode(e, d.kids[swap ? 0 : 1], t))}, 0);
                    return;
                }
            }
        }
        for (int k : d.kids) emit(k);
        push({d.code, d.arg, 0}, 1 - (int)d.kids.size());
    }
};
} // namespace

void build(const std::vector<std::pair<std::string, std::string>>& exprs, MoveTuning& t, Program& prog, Report* rep) {
    prog = Program();
    std::vector<Expr> ex(exprs.size());
    std::vector<ItemReport> items(exprs.size());
    int bySetting[kSettings];
    for (int& b : bySetting) b = -1;
    float plain[kSettings];
    for (int s = 0; s < kSettings; s++) plain[s] = settingGet(t, s);
    for (size_t i = 0; i < exprs.size(); i++) {
        ItemReport& r = items[i];
        Expr& e = ex[i];
        r.key = exprs[i].first;
        r.text = exprs[i].second;
        e.setting = settingIndex(r.key);
        if (e.setting < 0) { r.error = "'" + r.key + "' isn't a movement setting"; continue; }
        r.value = plain[e.setting];
        if (r.text.size() > (size_t)kMaxText) { r.error = "longer than " + std::to_string(kMaxText) + " characters"; continue; }
        Parser ps(r.text, e.nodes);
        e.root = ps.parse();
        if (e.root < 0) { r.error = ps.err.empty() ? "empty" : ps.err; continue; }
        collect(e, e.root);
        for (int s : e.deps) r.deps.push_back(settingKey(s));
        r.inputs = e.inputs;
        if (bySetting[e.setting] >= 0) { items[bySetting[e.setting]].error = "set again below"; ex[bySetting[e.setting]].ok = false; }
        e.ok = true;
        bySetting[e.setting] = (int)i;
    }
    // cycles: depth-first over the expressions (edges: the settings each refers to that have an expression)
    const int n = (int)ex.size();
    std::vector<int> color(n, 0), stack, order;
    auto okAt = [&](int s) { int v = bySetting[s]; return v >= 0 && ex[v].ok && !ex[v].cyclic ? v : -1; };
    std::function<void(int)> dfs;   // (at most kSettings deep)
    dfs = [&](int u) {
        color[u] = 1;
        stack.push_back(u);
        for (int s : ex[u].deps) {
            int v = okAt(s);
            if (v < 0) continue;
            if (color[v] == 1) {   // a back edge: everything on the stack from v round to u is a cycle
                size_t from = 0;
                while (stack[from] != v) from++;
                std::string path;
                for (size_t q = from; q < stack.size(); q++) path += settingKey(ex[stack[q]].setting) + std::string(" -> ");
                path += settingKey(ex[v].setting);
                for (size_t q = from; q < stack.size(); q++)
                    if (!ex[stack[q]].cyclic) { ex[stack[q]].cyclic = true; items[stack[q]].error = "cycle: " + path; }
            } else if (color[v] == 0) dfs(v);
        }
        stack.pop_back();
        color[u] = 2;
    };
    for (int u = 0; u < n; u++) if (ex[u].ok && color[u] == 0) dfs(u);
    for (auto& e : ex) if (e.cyclic) e.ok = false;
    // dependency order (post-order: what an expression refers to comes first), without the cycles
    std::fill(color.begin(), color.end(), 0);
    std::function<void(int)> post;
    post = [&](int u) {
        color[u] = 1;
        for (int s : ex[u].deps) { int v = okAt(s); if (v >= 0 && color[v] == 0) post(v); }
        order.push_back(u);
    };
    for (int u = 0; u < n; u++) if (ex[u].ok && color[u] == 0) post(u);
    bool dyn[kSettings] = {};
    for (int u : order) {
        Expr& e = ex[u];
        e.allInputs = e.inputs;
        for (int s : e.deps) { int v = okAt(s); if (v >= 0) e.allInputs |= ex[v].allInputs; }
        e.dynamic = e.allInputs != 0;
        dyn[e.setting] = e.dynamic;
    }
    // static: resolved now, in order; dynamic: compiled, in order
    for (int u : order) {
        Expr& e = ex[u];
        const int s = e.setting;
        if (e.dynamic) {
            Emitter em{prog, e, t, dyn};
            const size_t first = prog.ops.size();
            em.emit(e.root);
            if (!em.ok || em.maxSp > kMaxStack) {
                prog.ops.resize(first);
                items[u].error = "too complex (deeper than " + std::to_string(kMaxStack) + " values at once)";
                e.ok = false;
                dyn[s] = false;   // (anything that read it reads its fixed value: built later in the order, so folded)
                continue;
            }
            Program::Item it;
            it.setting = (uint8_t)s;
            it.first = (uint16_t)first;
            it.count = (uint16_t)(prog.ops.size() - first);
            it.lo = settingMin(s); it.hi = settingMax(s); it.fallback = plain[s];
            prog.items.push_back(it);
        } else {
            settingSet(t, s, finish(evalNode(e, e.root, t), settingMin(s), settingMax(s), plain[s]));
        }
    }
    if (!prog.empty()) { double in[P_COUNT]; restInputs(in); prog.run(in, t); }
    if (rep) {
        rep->items.clear();
        rep->order.clear();
        for (int u = 0; u < n; u++) {
            ItemReport& r = items[u];
            r.ok = ex[u].ok;
            r.dynamic = ex[u].ok && ex[u].dynamic;
            r.allInputs = ex[u].ok ? ex[u].allInputs : ex[u].inputs;
            if (ex[u].setting >= 0) r.value = settingGet(t, ex[u].setting);
        }
        for (int u : order) if (ex[u].ok) rep->order.push_back(u);
        rep->items = std::move(items);
    }
}

} // namespace mexpr
