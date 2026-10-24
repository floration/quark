// libFuzzer harness: CustomRules::fromText, the custom-mode text a host sends everyone who joins (EV_RULES) and
// the custom_modes/*.qmode files. Rules that parse must round-trip: toText() parses back to the same text, with the
// unknown keys and the logic block kept verbatim; the network text (toNetText) carries neither and round-trips too.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "game/customrules.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 64 * 1024) return 0;
    CustomRules r;
    if (!r.fromText(std::string((const char*)data, size))) return 0;
    std::string text = r.toText();
    CustomRules again;
    if (!again.fromText(text) || again.toText() != text) {
        fprintf(stderr, "custom rules don't round-trip:\n%s\n---\n%s\n", text.c_str(), again.toText().c_str());
        abort();
    }
    if (again.extraKeys != r.extraKeys || again.logic != r.logic) {
        fprintf(stderr, "custom rules lose their unknown keys or logic:\n%s\n", text.c_str());
        abort();
    }
    std::string net = r.toNetText();
    CustomRules fromNet;
    if (!fromNet.fromText(net) || fromNet.toNetText() != net || !fromNet.extraKeys.empty() || !fromNet.logic.empty()) {
        fprintf(stderr, "custom rules network text doesn't round-trip:\n%s\n", net.c_str());
        abort();
    }
    return 0;
}
