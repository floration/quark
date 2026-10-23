// libFuzzer harness: Map::parse (a .qmap from a server's download, your maps folder or the editor). A map that
// parses must also validate, serialize, and parse again from its own serialization.
// Seeds: tests/data/fuzz/map (small hand-written maps) plus tests/data/*.qmap; any of build/maps/*.qmap work too.
#include <cstdint>
#include <cstdlib>
#include <string>
#include "game/map.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 256 * 1024) return 0;   // (the format has no size limit of its own; downloads stop at kMaxMapBytes)
    Map m;
    std::string err;
    if (!m.parse(std::string((const char*)data, size), &err)) return 0;
    std::string text = m.serialize();
    Map again;
    std::string err2;
    // a map the game accepted must survive the editor's save: that's what an edit session sends to everyone
    if (!again.parse(text, &err2)) {
        fprintf(stderr, "parsed map doesn't re-parse from its serialization: %s\n", err2.c_str());
        abort();
    }
    return 0;
}
