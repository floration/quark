// Lowercase hex strings (ranked keys, ids, signatures, match ids, list tokens): one check for all of them.
#pragma once
#include <cstddef>
#include <string>

// Exactly n characters, each 0-9 or a-f.
inline bool isLowerHex(const std::string& s, size_t n) {
    if (s.size() != n) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
