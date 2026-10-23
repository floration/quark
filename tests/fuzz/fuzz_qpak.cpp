// libFuzzer harness: the launcher's .qpak reader (src/launcher/qpak.cpp; it runs after the signature checks, but a
// reader bug must not depend on them). Every entry it hands out must be a safe name that stays inside the install
// folder, with no more bytes than the package had.
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include "launcher/qpak.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::istringstream in(std::string((const char*)data, size));
    const std::filesystem::path stage = "/install/stage";
    size_t total = 0;
    std::string err;
    qpak::read(in, size, [&](const std::string& name, uint32_t, const char*, size_t n, std::string*) {
        total += n + name.size();
        std::filesystem::path out = (stage / std::filesystem::u8path(name)).lexically_normal();
        std::string rel = out.lexically_relative(stage).generic_string();
        if (!qpak::safeEntryName(name) || rel.empty() || rel == "." || rel.rfind("..", 0) == 0 || total > size) abort();
        return true;
    }, &err);
    return 0;
}
