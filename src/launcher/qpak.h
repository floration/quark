// .qpak update packages (made by tools/pack.py): "QPAK1\n", then per file: u32 nameLen, name, u32 mode, u64 size,
// bytes. Little endian. Read by the launcher (after the signature checks, docs/SIGNING.md) and fuzzed (tests/fuzz).
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <istream>
#include <string>

namespace qpak {

// A qpak entry name is safe when it is a plain relative path inside the install: no root name (C:, \\server), no
// root directory (/ or \), no "..", no "." or empty part (it must name a file), no ':' or NUL.
bool safeEntryName(const std::string& name);

// Reads a whole package from `in` (`size` bytes long): every entry is checked (name safe, size within what's left,
// so a lying size never drives an allocation) and handed to `file`, which returns false to stop (err = its reason).
// False on any malformed package, with the reason in err.
using FileSink = std::function<bool(const std::string& name, uint32_t mode, const char* data, size_t n, std::string* err)>;
bool read(std::istream& in, uint64_t size, const FileSink& file, std::string* err);

// Extracts `pak` into `stage` (a fresh directory); the caller swaps it into place on success, so a failed or
// interrupted install never leaves a half-updated game.
bool extract(const std::filesystem::path& pak, const std::filesystem::path& stage, std::string* err);

} // namespace qpak
