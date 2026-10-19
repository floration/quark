// Byte-oriented packet writer/reader. Little endian on the wire.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include "qmath.h"

constexpr int kMaxPacket = 1400; // stay under typical MTU

class Writer {
public:
    Writer(uint8_t* buf, int cap) : buf_(buf), cap_(cap) {}
    void u8(uint8_t v) { if (pos_ + 1 <= cap_) buf_[pos_++] = v; else overflow_ = true; }
    void u16(uint16_t v) { u8(v & 255); u8(v >> 8); }
    void u32(uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
    void i8(int8_t v) { u8((uint8_t)v); }
    void i16(int16_t v) { u16((uint16_t)v); }
    void f32(float f) { uint32_t v; memcpy(&v, &f, 4); u32(v); }
    void vec(const Vec3& v) { f32(v.x); f32(v.y); f32(v.z); }
    // 1/64 m precision, +-512m range.
    void vecq(const Vec3& v) { for (int i = 0; i < 3; i++) i16((int16_t)clampf(std::round(v[i] * 64.0f), -32767, 32767)); }
    void str(const std::string& s) {
        size_t n = s.size() > 255 ? 255 : s.size();
        u8((uint8_t)n);
        for (size_t i = 0; i < n; i++) u8((uint8_t)s[i]);
    }
    int size() const { return pos_; }
    bool overflowed() const { return overflow_; }
    uint8_t* data() { return buf_; }
private:
    uint8_t* buf_;
    int cap_, pos_ = 0;
    bool overflow_ = false;
};

class Reader {
public:
    Reader(const uint8_t* buf, int len) : buf_(buf), len_(len) {}
    uint8_t u8() { if (pos_ < len_) return buf_[pos_++]; bad_ = true; return 0; }
    uint16_t u16() { uint16_t a = u8(); return a | (uint16_t)(u8() << 8); }
    uint32_t u32() { uint32_t a = u16(); return a | ((uint32_t)u16() << 16); }
    int8_t i8() { return (int8_t)u8(); }
    int16_t i16() { return (int16_t)u16(); }
    float f32() { uint32_t v = u32(); float f; memcpy(&f, &v, 4); return std::isfinite(f) ? f : 0.0f; }
    Vec3 vec() { Vec3 v; v.x = f32(); v.y = f32(); v.z = f32(); return v; }
    Vec3 vecq() { Vec3 v; for (int i = 0; i < 3; i++) v[i] = i16() / 64.0f; return v; }
    std::string str() {
        int n = u8();
        std::string s;
        for (int i = 0; i < n; i++) s += (char)u8();
        return s;
    }
    bool bad() const { return bad_; }
    int remaining() const { return len_ - pos_; }
private:
    const uint8_t* buf_;
    int len_, pos_ = 0;
    bool bad_ = false;
};
