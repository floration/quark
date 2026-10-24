// modesim_logic.h: the mode logic language behind plain types (this file must not include src/game/gamemodes.h).
#include "modesim_logic.h"
#include "game/modelogic.h"

ModeCompile compileModeText(const std::string& text, int base, const std::vector<std::string>& extraErrors) {
    ModeCompile c;
    ModeDiag d;
    auto L = compileModeLogic(text, base, &d);
    if (L) {
        c.name = L->name; c.hash = L->hash; c.label = modeLabel(*L); c.base = L->base;
        static const char* nn[] = {"hill", "sites", "cart", "points", "checkpoints"};
        for (int i = 0; i < 5; i++) if (modeLogicNeeds(*L) >> i & 1) c.needs.push_back(nn[i]);
    }
    for (auto& e : extraErrors) d.errors.push_back({0, 0, e});
    c.ok = L && d.errors.empty();
    c.diagJson = modeDiagJson(d, c.ok ? L.get() : nullptr);
    return c;
}

int modeBaseFromName(const std::string& name) { return modeBaseByName(name); }
const char* modeSlotNameOf(int slot) { return modeSlotName(slot); }
