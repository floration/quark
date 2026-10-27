// A small JSON value, reader and writer for quark_maptool (and the bot editor's commands linked into it).
// No dependencies; objects keep their key order (answers read the way they were written).
#pragma once
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Json {
public:
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ };
    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : t_(BOOL), b_(b) {}
    Json(int v) : t_(NUM), n_(v) {}
    Json(unsigned v) : t_(NUM), n_(v) {}
    Json(long v) : t_(NUM), n_((double)v) {}
    Json(long long v) : t_(NUM), n_((double)v) {}
    Json(unsigned long v) : t_(NUM), n_((double)v) {}
    Json(unsigned long long v) : t_(NUM), n_((double)v) {}
    Json(double v) : t_(NUM), n_(v) {}
    // floats are written with the shortest digits that read back as the same float (map coordinates round-trip)
    Json(float v) : t_(NUM), n_(v), f32_(true) {}
    Json(const char* s) : t_(STR), s_(s) {}
    Json(std::string s) : t_(STR), s_(std::move(s)) {}
    static Json array() { Json j; j.t_ = ARR; return j; }
    static Json object() { Json j; j.t_ = OBJ; return j; }

    Type type() const { return t_; }
    bool isNull() const { return t_ == NUL; }
    bool isNum() const { return t_ == NUM; }
    bool isStr() const { return t_ == STR; }
    bool isArr() const { return t_ == ARR; }
    bool isObj() const { return t_ == OBJ; }
    bool isBool() const { return t_ == BOOL; }

    double num(double def = 0) const { return t_ == NUM ? n_ : t_ == BOOL ? (b_ ? 1 : 0) : def; }
    float f(float def = 0) const { return t_ == NUM ? (float)n_ : def; }
    int i(int def = 0) const { return t_ == NUM ? (int)n_ : t_ == BOOL ? (int)b_ : def; }
    bool boolean(bool def = false) const { return t_ == BOOL ? b_ : t_ == NUM ? n_ != 0 : def; }
    const std::string& str() const { static const std::string e; return t_ == STR ? s_ : e; }
    std::string str(const std::string& def) const { return t_ == STR ? s_ : def; }

    // arrays
    size_t size() const { return t_ == ARR ? a_.size() : t_ == OBJ ? o_.size() : 0; }
    const Json& operator[](size_t k) const { static const Json n; return t_ == ARR && k < a_.size() ? a_[k] : n; }
    const Json& operator[](int k) const { return (*this)[(size_t)(k < 0 ? (size_t)-1 : (size_t)k)]; }
    Json& push(Json v) { if (t_ != ARR) { *this = array(); } a_.push_back(std::move(v)); return a_.back(); }
    const std::vector<Json>& items() const { static const std::vector<Json> e; return t_ == ARR ? a_ : e; }
    std::vector<Json>& items() { if (t_ != ARR) *this = array(); return a_; }

    // objects
    bool has(const std::string& k) const { return find(k) != nullptr; }
    const Json& operator[](const std::string& k) const { static const Json n; const Json* v = find(k); return v ? *v : n; }
    const Json& operator[](const char* k) const { return (*this)[std::string(k)]; }
    Json& set(const std::string& k, Json v) {
        if (t_ != OBJ) *this = object();
        for (auto& kv : o_) if (kv.first == k) { kv.second = std::move(v); return kv.second; }
        o_.emplace_back(k, std::move(v));
        return o_.back().second;
    }
    const std::vector<std::pair<std::string, Json>>& members() const { static const std::vector<std::pair<std::string, Json>> e; return t_ == OBJ ? o_ : e; }

    // text
    std::string dump() const { std::string s; write(s); return s; }
    void write(std::string& out) const;
    // false: not JSON (err says where)
    static bool parse(const std::string& text, Json& out, std::string* err = nullptr);

private:
    const Json* find(const std::string& k) const {
        if (t_ != OBJ) return nullptr;
        for (auto& kv : o_) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    Type t_ = NUL;
    bool b_ = false;
    double n_ = 0;
    bool f32_ = false;
    std::string s_;
    std::vector<Json> a_;
    std::vector<std::pair<std::string, Json>> o_;
};

void jsonWriteString(std::string& out, const std::string& s);
void jsonWriteFloat(std::string& out, float v);     // shortest digits that read back as the same float (NaN/inf: null)
void jsonWriteDouble(std::string& out, double v);
