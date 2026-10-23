#include "qpak.h"
#include <cstring>
#include <fstream>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace qpak {

bool safeEntryName(const std::string& name) {
    if (name.empty() || name.find('\0') != std::string::npos || name.find(':') != std::string::npos) return false;
    if (name[0] == '/' || name[0] == '\\') return false;
    if (name.find("..") != std::string::npos) return false; // also catches ..\ on Windows
    // every part names something: no "." (a name "." or "a/." is the folder itself, not a file), no empty part
    // ("a//b", a trailing "/"): what pack.py writes, and nothing that resolves to the stage folder itself
    for (size_t i = 0; i <= name.size();) {
        size_t j = name.find_first_of("/\\", i);
        if (j == std::string::npos) j = name.size();
        if (j == i || name.compare(i, j - i, ".") == 0) return false;
        i = j + 1;
    }
    fs::path p = fs::u8path(name);
    return p.is_relative() && !p.has_root_name() && !p.has_root_directory();
}

bool read(std::istream& in, uint64_t remaining, const FileSink& file, std::string* err) {
    char magic[6];
    if (remaining < 6 || !in.read(magic, 6) || memcmp(magic, "QPAK1\n", 6) != 0) { *err = "corrupt download"; return false; }
    remaining -= 6;
    auto rd = [&](void* p, size_t n) {
        if (n > remaining) return false;
        in.read((char*)p, (std::streamsize)n);
        if (!in) return false;
        remaining -= n;
        return true;
    };
    std::vector<char> buf;
    while (remaining > 0) {
        uint32_t nl, mode;
        uint64_t size;
        if (!rd(&nl, 4)) { *err = "corrupt archive"; return false; }
        if (nl == 0 || nl > 4096) { *err = "corrupt archive"; return false; }
        std::string name(nl, 0);
        if (!rd(&name[0], nl) || !rd(&mode, 4) || !rd(&size, 8)) { *err = "corrupt archive"; return false; }
        if (!safeEntryName(name)) { *err = "unsafe path in archive"; return false; }
        if (size > remaining) { *err = "truncated archive"; return false; } // never trust size for the allocation
        buf.resize((size_t)size);
        if (size && !rd(buf.data(), (size_t)size)) { *err = "truncated archive"; return false; }
        if (!file(name, mode, buf.data(), (size_t)size, err)) return false;
    }
    return true;
}

static bool extractImpl(const fs::path& pak, const fs::path& stage, std::string* err);
bool extract(const fs::path& pak, const fs::path& stage, std::string* err) {
    try { return extractImpl(pak, stage, err); }
    catch (const std::exception& e) { *err = std::string("corrupt archive (") + e.what() + ")"; return false; }   // (ENG-2: bad_alloc, a bad path)
}

static bool extractImpl(const fs::path& pak, const fs::path& stage, std::string* err) {
    std::error_code ec;
    uint64_t size = fs::file_size(pak, ec);
    if (ec) { *err = "corrupt download"; return false; }
    std::ifstream f(pak, std::ios::binary);
    if (!f) { *err = "corrupt download"; return false; }
    return read(f, size, [&](const std::string& name, uint32_t mode, const char* data, size_t n, std::string* e) {
        fs::path out = stage / fs::u8path(name);
        std::error_code ec2;
        fs::create_directories(out.parent_path(), ec2);
        std::ofstream o(out, std::ios::binary | std::ios::trunc);
        if (!o) { *e = "cannot write " + out.string(); return false; }
        o.write(data, (std::streamsize)n);
        o.close();
        if (!o) { *e = "cannot write " + out.string() + " (disk full?)"; return false; }
#ifndef _WIN32
        chmod(out.string().c_str(), mode & 0777);
#endif
        return true;
    }, err);
}

} // namespace qpak
