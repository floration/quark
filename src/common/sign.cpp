#include "sign.h"
#include "u8file.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
extern "C" {
#include "../../third_party/monocypher/monocypher.h"
#include "../../third_party/monocypher/monocypher-ed25519.h"
}
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

namespace sign {

// Generated for the owner on 2026-10-22; the private half is the QUARK_SIGNING_KEY repo secret (docs/SIGNING.md).
const char* kReleasePublicKey = "0b059c406106eed2be15b6d840166ec6bcb72817450bfa2dcae561c07b6b8b7e";

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool fromHex(const std::string& in, uint8_t* out, size_t n) {
    std::string h = trim(in);
    if (h.size() != n * 2) return false;
    for (size_t i = 0; i < n; i++) {
        int v = 0;
        for (int k = 0; k < 2; k++) {
            char c = h[i * 2 + k];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return false;
            v = v * 16 + d;
        }
        out[i] = (uint8_t)v;
    }
    return true;
}

std::string toHex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

bool randomBytes(uint8_t* p, size_t n) {
#ifdef _WIN32
    return BCryptGenRandom(nullptr, p, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    FILE* f = fopen("/dev/urandom", "rb");
    if (!f) return false;
    bool ok = fread(p, 1, n, f) == n;
    fclose(f);
    return ok;
#endif
}

bool expand(const std::string& privHex, uint8_t sk[64], uint8_t pk[32]) {
    uint8_t seed[32];
    if (!fromHex(privHex, seed, 32)) return false;
    crypto_ed25519_key_pair(sk, pk, seed); // wipes seed
    return true;
}

bool hashFile(const std::string& path, uint8_t hash[64], std::string* err) {
    FILE* f = u8fopen(path, "rb");
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    crypto_sha512_ctx ctx;
    crypto_sha512_init(&ctx);
    std::vector<uint8_t> buf(1 << 16);
    size_t got;
    while ((got = fread(buf.data(), 1, buf.size(), f)) > 0) crypto_sha512_update(&ctx, buf.data(), got);
    bool bad = ferror(f) != 0;
    fclose(f);
    crypto_sha512_final(&ctx, hash);
    if (bad) { if (err) *err = "cannot read " + path; return false; }
    return true;
}

bool signHash(const uint8_t hash[64], const std::string& privHex, std::string* sigHex) {
    uint8_t sk[64], pk[32], sig[64];
    if (!expand(privHex, sk, pk)) return false;
    crypto_ed25519_ph_sign(sig, sk, hash);
    crypto_wipe(sk, sizeof sk);
    *sigHex = toHex(sig, 64);
    return true;
}

bool verifyHash(const uint8_t hash[64], const std::string& sigHex, const std::string& pubHex) {
    uint8_t sig[64], pk[32];
    if (!fromHex(sigHex, sig, 64) || !fromHex(pubHex, pk, 32)) return false;
    return crypto_ed25519_ph_check(sig, pk, hash) == 0;
}

} // namespace

bool keygen(std::string* privHex, std::string* pubHex) {
    uint8_t seed[32];
    if (!randomBytes(seed, 32)) return false;
    *privHex = toHex(seed, 32);
    crypto_wipe(seed, sizeof seed);
    return publicFromPrivate(*privHex, pubHex);
}

bool publicFromPrivate(const std::string& privHex, std::string* pubHex) {
    uint8_t sk[64], pk[32];
    if (!expand(privHex, sk, pk)) return false;
    crypto_wipe(sk, sizeof sk);
    *pubHex = toHex(pk, 32);
    return true;
}

bool signData(const std::string& data, const std::string& privHex, std::string* sigHex) {
    uint8_t h[64];
    crypto_sha512(h, (const uint8_t*)data.data(), data.size());
    return signHash(h, privHex, sigHex);
}

bool signFile(const std::string& path, const std::string& privHex, std::string* sigHex, std::string* err) {
    uint8_t h[64];
    if (!hashFile(path, h, err)) return false;
    if (!signHash(h, privHex, sigHex)) { if (err) *err = "bad private key (expected 64 hex digits)"; return false; }
    return true;
}

bool verifyData(const std::string& data, const std::string& sigHex, const std::string& pubHex) {
    uint8_t h[64];
    crypto_sha512(h, (const uint8_t*)data.data(), data.size());
    return verifyHash(h, sigHex, pubHex);
}

bool verifyFile(const std::string& path, const std::string& sigHex, const std::string& pubHex, std::string* err) {
    uint8_t h[64];
    if (!hashFile(path, h, err)) return false;
    if (!verifyHash(h, sigHex, pubHex)) { if (err) *err = "signature does not match"; return false; }
    return true;
}

bool signMessage(const std::string& msg, const std::string& privHex, std::string* sigHex) {
    uint8_t sk[64], pk[32], sig[64];
    if (!expand(privHex, sk, pk)) return false;
    crypto_ed25519_sign(sig, sk, (const uint8_t*)msg.data(), msg.size());
    crypto_wipe(sk, sizeof sk);
    *sigHex = toHex(sig, 64);
    return true;
}

bool verifyMessage(const std::string& msg, const std::string& sigHex, const std::string& pubHex) {
    uint8_t sig[64], pk[32];
    if (!fromHex(sigHex, sig, 64) || !fromHex(pubHex, pk, 32)) return false;
    return crypto_ed25519_check(sig, pk, (const uint8_t*)msg.data(), msg.size()) == 0;
}

} // namespace sign
