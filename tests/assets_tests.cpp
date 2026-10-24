// Assets: the asset export (`quark_tests exportassets <dir>`: every default asset as a file, for the editors and the
// website's /assets/ page via tools/studio/build.py) and the owner's overrides (docs/ASSETS.md): models <-> glTF, the decoders, the font atlas,
// override lookup and the audio replacing its sounds.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "../src/client/audio.h"
#include "../src/client/fontatlas.h"
#include "../src/client/gltf.h"
#include "../src/client/models.h"
#include "../src/client/overrides.h"
#include "../src/client/uifonts.h"
#include "../src/common/sha256.h"
#include "../src/common/version.h"
#include "../src/game/map.h"
#include "official_map_data.h"   // (generated: every shipped map's text, the way the game embeds it)

namespace fs = std::filesystem;

static int g_afail = 0;

// The source tree and tests/data: where this was built, or QUARK_SOURCE_DIR in the environment (CI runs the
// cross-built Windows tests on a Windows runner, where the build machine's paths don't exist)
static fs::path srcDir() {
    const char* e = getenv("QUARK_SOURCE_DIR");
    return e && *e ? fs::path(e) : fs::path(QUARK_SOURCE_DIR);
}
static fs::path testData() {
    const char* e = getenv("QUARK_SOURCE_DIR");
    return e && *e ? fs::path(e) / "tests/data" : fs::path(QUARK_TEST_DATA);
}
#define ACHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_afail++; } } while (0)

namespace {
std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
bool spit(const fs::path& p, const std::string& data) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    return (bool)f.write(data.data(), (std::streamsize)data.size());
}
std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 32) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o + "\"";
}
// 16-bit mono PCM
std::string wavFile(const std::vector<float>& x, int rate) {
    std::string d;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) d += (char)(v >> (8 * i) & 255); };
    auto u16 = [&](uint16_t v) { d += (char)(v & 255); d += (char)(v >> 8); };
    uint32_t bytes = (uint32_t)x.size() * 2;
    d += "RIFF"; u32(36 + bytes); d += "WAVEfmt "; u32(16); u16(1); u16(1); u32((uint32_t)rate); u32((uint32_t)rate * 2); u16(2); u16(16);
    d += "data"; u32(bytes);
    for (float v : x) { int s = (int)std::lround(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f); u16((uint16_t)(int16_t)s); }
    return d;
}
float rms(const std::vector<float>& x) { double e = 0; for (float v : x) e += (double)v * v; return x.empty() ? 0 : (float)std::sqrt(e / x.size()); }

// When each sound plays (assets/sfx/custom/README.md), for the studio's descriptions
const char* soundWhen(int id) {
    static const char* d[SND_VOICE_THREE] = {"you hit someone for under 10 damage (the light hitsound) - very short, < 0.2 s", "you killed someone - < 0.6 s",
        "you took damage - < 0.3 s", "a footstep (yours and others', in 3D) - very short, < 0.15 s", "a jump - < 0.2 s",
        "landing from a fall - < 0.3 s", "rifle shot - < 0.3 s (automatic: many per second)", "rocket launcher fired - < 0.7 s",
        "knife swing / throw - < 0.35 s", "sniper shot - < 1 s", "laser (lightning gun), repeats while firing - very short, < 0.1 s",
        "shotgun blast - < 0.6 s", "sticky bomb launched - < 0.4 s", "crossbow bolt fired - < 0.5 s", "bubble gun shot - < 0.3 s",
        "syringe used / thrown - < 0.4 s", "a rocket / sticky explodes (3D) - < 1 s", "a sticky bomb sticks to something - < 0.2 s",
        "a crossbow bolt hits - < 0.3 s", "dash - < 0.4 s", "grappling hook fired - < 0.3 s", "grappling hook catches - < 0.3 s",
        "you died - < 0.7 s", "someone else's killstreak - < 0.6 s", "someone typing in chat (one key click) - very short, < 0.06 s",
        "race: you passed a checkpoint - < 0.35 s", "race: you crossed the finish - < 0.8 s",
        "you hit someone for 10 to 24 damage (the medium hitsound: heavier and louder than HIT) - short, < 0.25 s",
        "you hit someone for 25 to 50 damage (the heavy hitsound) - < 0.35 s",
        "you hit someone for more than 50 damage (the brutal hitsound: the heaviest, loudest) - < 0.5 s"};
    return id >= 0 && id < SND_VOICE_THREE && d[id] ? d[id] : "";
}
const char* kitTitle(int pack) { static const char* n[] = {"DEFAULT", "HEAVY METAL", "MAGICAL", "RETRO", "MOUTH"}; return n[pack]; }
std::string lineText(int id) { std::string s = Audio::soundName(id); for (char& c : s) c = c == '_' ? ' ' : (char)tolower((unsigned char)c); return s; }
const char* modelDesc(int kind) {
    switch (kind) {
        case MODEL_PLAYER: return "player model: metres, Y up, facing -Z, feet at the origin, 2 m tall. Materials named team* take the team colour (white = the team colour)";
        case MODEL_WEAPON: return "first-person weapon: metres, barrel along -Z, origin at the grip pivot. Materials team* = your accent colour, metal* / base* / glow* = the map's colours";
        case MODEL_PROJECTILE: return "projectile in flight: metres, origin at its centre, flying toward -Z. Materials team* take the shooter's team colour, glow* the map's glow";
        case MODEL_CROWN: return "leaderboard crown on a player's head: origin at the top of the head, front at -Z. Materials team* take the gold (#1) / silver colour";
    }
    return "";
}
const char* propDesc(int prop) {
    switch (prop) {
        case PROP_GUN_ARM: return "the arm and gun other players hold out: metres, origin at the shoulder, reaching along -Z (turned with their aim, scaled with their body). No colour slots";
        case PROP_ARM: return "the arm that holds out a weapon model (hide and seek / tag: whoever is it): origin at the shoulder, along -Z, the hand (the weapon's pivot) at z -0.5. No colour slots";
        case PROP_BOMB_PACK: return "the bomb on its carrier's back: in the player's space (feet at the origin, facing -Z, the pack at +Z). Moves down with a crouch. No colour slots";
        case PROP_BOMB: return "the bomb on the ground, dropped or planted: origin on the ground under it. Materials glow* blink red while a planted bomb ticks (drawn dark in the export)";
        case PROP_CART: return "the payload cart: origin on the track under its middle, front (-Z) the way it is pushed. Materials team* = the pushing team (white = that colour; grey when neutral), metal* / accent* / glow* = the map's colours";
        case PROP_GATE: return "a payload checkpoint: origin on the track, -Z the cart's way, posts either side (x +-1.8). Materials team* = grey, the attackers' colour once the cart has reached it";
    }
    return "";
}
// A model's default boxes with the export's colours (and each box's mul / add / crouch: a box model file)
void defaultParts(int kind, int i, ModelParts& parts) { exportModelParts(kind, i, parts); }
// The built-in model as drawn with this palette (and crouch: players)
void builtIn(int kind, int i, const ModelPalette& pal, float hs, ModelParts& out) {
    if (kind == MODEL_PLAYER) playerModelParts(i, hs, pal, out);
    else if (kind == MODEL_WEAPON) viewmodelParts(i, pal, out);
    else if (kind == MODEL_PROJECTILE) projectileParts(i, pal, 1.0f, 1.0f, 0.0f, out);
    else if (kind == MODEL_PROP) propParts(i, pal, out);
    else crownParts(pal, out);
}

struct Item { std::string id, category, kind, name, file, desc, kit; };
std::string manifestJson(const std::vector<Item>& items) {
    std::string j = "{\"version\":" + jsonStr(kQuarkVersion) + ",\"items\":[\n";
    for (size_t i = 0; i < items.size(); i++) {
        const Item& it = items[i];
        j += "{\"id\":" + jsonStr(it.id) + ",\"category\":" + jsonStr(it.category) + ",\"kind\":" + jsonStr(it.kind) + ",\"name\":" + jsonStr(it.name) +
             ",\"file\":" + jsonStr(it.file) + ",\"desc\":" + jsonStr(it.desc) + (it.kit.empty() ? "" : ",\"kit\":" + jsonStr(it.kit)) + "}" +
             (i + 1 < items.size() ? ",\n" : "\n");
    }
    return j + "]}\n";
}
std::string lineValue(const std::string& text, const std::string& key) {
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l)) if (l.compare(0, key.size(), key) == 0) return l.substr(key.size());
    return "";
}
} // namespace

// ---- the export ---------------------------------------------------------------------------------------------------
// <dir>/manifest.json plus every file it lists. What the game uses now: its built-in assets with the shipped overrides
// (assets/overrides/) applied, never this computer's own (the per-user overrides folder).
int exportAssets(const char* dirArg) {
    overrides::useUserFolder(false);
    fs::path dir = fs::u8path(dirArg);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    std::vector<Item> items;
    int bad = 0;
    auto add = [&](const Item& it, const std::string& data) {
        if (!spit(dir / fs::u8path(it.file), data)) { printf("exportassets: can't write %s\n", it.file.c_str()); bad++; return; }
        items.push_back(it);
    };
    // sounds: every kit, then every announcer voice
    Audio audio;
    audio.startOffline();
    for (int pack = 0; pack < Audio::kPacks; pack++)
        for (int id = 0; id < SND_VOICE_THREE; id++) {
            std::string kit = Audio::kitDir(pack), n = Audio::soundName(id);
            add({"sound/" + kit + "/" + n, "Sounds", "sound", n, "sounds/" + kit + "/" + n + ".wav",
                 std::string(kitTitle(pack)) + " kit: " + soundWhen(id), kit}, wavFile(audio.samples(pack, id), Audio::kRate));
        }
    for (int v = 0; v < kAnnouncerVoices; v++) {
        audio.setAnnouncer(v);
        std::string vd = Audio::voiceDir(v);
        for (int id = SND_VOICE_THREE; id < SND_COUNT; id++) {
            // (a line this voice doesn't have yet is silent in the game: a tenth of a second of silence here, so the
            // studio still lists it and a recording can be added)
            const std::vector<float>& x = audio.samples(0, id);
            std::string n = Audio::soundName(id);
            add({"voice/" + vd + "/" + n, "Voices", "sound", n, "voices/" + vd + "/" + n + ".wav",
                 std::string("announcer ") + announcerVoiceName(v) + ": \"" + lineText(id) + "\"" + (x.empty() ? " (not recorded yet: silent)" : ""), vd},
                wavFile(x.empty() ? std::vector<float>(Audio::kRate / 10, 0.0f) : x, Audio::kRate));
        }
    }
    audio.stop();
    // models, as the game draws them with the reference colours (models.h)
    ModelParts parts;
    for (int kind = 0; kind < MODEL_KINDS; kind++)
        for (int i = 0; i < modelCount(kind); i++) {
            if (!modelExists(kind, i)) continue;
            defaultParts(kind, i, parts);
            MeshData mesh;
            partsToMesh(parts, mesh);
            std::string path = modelPath(kind, i);
            add({"model/" + path.substr(7), "Models", "model", modelName(kind, i), path + ".gltf", kind == MODEL_PROP ? propDesc(i) : modelDesc(kind), modelKindName(kind)},
                writeGltf(mesh, modelName(kind, i), &parts));
        }
    // maps: the shipped text (the generated map, or its override)
    for (const OfficialMapData* m = kOfficialMapData; m->name; m++) {
        std::string text((const char*)m->data, m->size), name = m->name;
        std::string title = lineValue(text, "title "), mode = lineValue(text, "for "), setting = lineValue(text, "setting ");
        for (char& c : title) c = (char)toupper((unsigned char)c);
        std::string desc = mode.empty() ? "every mode" : "built for " + mode;
        if (!setting.empty()) desc += " - " + setting;
        add({"map/" + name, "Maps", "map", title.empty() ? name : title, "maps/" + name + ".qmap", desc, ""}, text);
    }
    // the UI font, the icon, the website's pictures (the shipped override where there is one)
    std::vector<uint8_t> ov;
    std::string matched;
    const fs::path src = srcDir();
    if (overrides::find("fonts/Rubik", {".ttf", ".otf"}, ov, &matched))
        add({"font/rubik", "Fonts", "font", "RUBIK", "fonts/Rubik" + matched.substr(matched.rfind('.')),
             "the game's text (menus, HUD), the default font, drawn bold", ""}, std::string(ov.begin(), ov.end()));
    else add({"font/rubik", "Fonts", "font", "RUBIK", "fonts/Rubik.ttf",
              "the game's text (menus, HUD), the default font, drawn bold; Rubik Project Authors, SIL OFL 1.1", ""}, slurp(src / "assets/fonts/Rubik.ttf"));
    if (overrides::find("fonts/WorkSans", {".ttf", ".otf"}, ov, &matched))
        add({"font/worksans", "Fonts", "font", "WORK SANS", "fonts/WorkSans" + matched.substr(matched.rfind('.')),
             "Settings > FONT: WORK SANS (the UI font of 1.17 - 1.18), drawn bold", ""}, std::string(ov.begin(), ov.end()));
    else add({"font/worksans", "Fonts", "font", "WORK SANS", "fonts/WorkSans.ttf",
              "Settings > FONT: WORK SANS (the UI font of 1.17 - 1.18), drawn bold; Work Sans Project Authors, SIL OFL 1.1", ""}, slurp(src / "assets/fonts/WorkSans.ttf"));
    // the free look-alikes of game fonts (Settings > FONT; uifonts.cpp's override paths; a player's own real font wins)
    struct LookAlike { const char* id; const char* name; const char* file; const char* desc; };
    static const LookAlike kLookAlikes[] = {
        {"font/bebasneue", "BEBAS NEUE", "BebasNeue-Regular.ttf", "Settings > FONT: the closest free match of Overwatch's font; Dharma Type, SIL OFL 1.1"},
        {"font/decalotype", "DECALOTYPE", "Decalotype-Bold.ttf", "Settings > FONT: the font Cocaine Diesel uses; SIL OFL 1.1"},
        {"font/librequake", "LIBREQUAKE 8X8", "LibreQuake-CONCHARS.png", "Settings > FONT: the closest free match of Quake's console font (a 16x16 sheet of 8x8 characters); LibreQuake, BSD-3-Clause"},
        {"font/dejavusans", "DEJAVU SANS", "DejaVuSans-Bold.ttf", "Settings > FONT: the closest free match of Half-Life 2's font; Bitstream Vera licence"},
        {"font/lilitaone", "LILITA ONE", "LilitaOne-Regular.ttf", "Settings > FONT: the closest free match of TF2 Build; SIL OFL 1.1"},
        {"font/jost", "JOST", "Jost.ttf", "Settings > FONT: the closest free match of TF2 Secondary, drawn SemiBold; SIL OFL 1.1"},
        {"font/barlowcondensed", "BARLOW CONDENSED", "BarlowCondensed-Bold.ttf", "Settings > FONT: the closest free match of Apex Legends' font; SIL OFL 1.1"},
    };
    for (const LookAlike& f : kLookAlikes) {
        std::error_code fec;
        if (!fs::exists(src / "assets/fonts" / f.file, fec)) continue;
        std::string file = f.file, base = "fonts/" + file.substr(0, file.rfind('.')), ext = file.substr(file.rfind('.'));
        bool png = ext == ".png";
        bool have = png ? overrides::find(base, {".png"}, ov, &matched) : overrides::find(base, {".ttf", ".otf"}, ov, &matched);
        if (have) add({f.id, "Fonts", png ? "image" : "font", f.name, base + matched.substr(matched.rfind('.')), f.desc, ""}, std::string(ov.begin(), ov.end()));
        else add({f.id, "Fonts", png ? "image" : "font", f.name, "fonts/" + file, f.desc, ""}, slurp(src / "assets/fonts" / f.file));
    }
    if (overrides::find("images/icon", {".png", ".jpg", ".jpeg"}, ov, &matched))
        add({"image/icon", "Images", "image", "GAME ICON", "images/icon" + matched.substr(matched.rfind('.')), "app, launcher, window and website icon", ""}, std::string(ov.begin(), ov.end()));
    else add({"image/icon", "Images", "image", "GAME ICON", "images/icon.png", "app, launcher, window and website icon (256 px)", ""}, slurp(src / "assets/quark.png"));
    std::vector<fs::path> pics;
    for (auto& e : fs::directory_iterator(src / "site/img", ec)) pics.push_back(e.path());
    std::sort(pics.begin(), pics.end());
    for (const fs::path& p : pics) {
        std::string ext = p.extension().string(), stem = p.stem().string();
        if (ext != ".jpg" && ext != ".png") continue;
        fs::path o = src / "assets/overrides/website" / p.filename();
        std::string upper = stem;
        for (char& c : upper) c = c == '_' || c == '-' ? ' ' : (char)toupper((unsigned char)c);
        add({"website/" + stem, "Website", "image", upper, "website/" + p.filename().string(), "a picture on the website (site/img/" + p.filename().string() + ")", ""},
            slurp(fs::exists(o, ec) ? o : p));
    }
    spit(dir / "manifest.json", manifestJson(items));
    int n[6] = {};
    for (auto& it : items) n[it.category == "Sounds" ? 0 : it.category == "Voices" ? 1 : it.category == "Models" ? 2 : it.category == "Maps" ? 3 : it.category == "Website" ? 5 : 4]++;
    printf("exportassets: %zu assets (%d sounds, %d voice lines, %d models, %d maps, %d font / icon, %d website pictures) -> %s\n",
           items.size(), n[0], n[1], n[2], n[3], n[4], n[5], dir.u8string().c_str());
    return bad;
}

// ---- tests ---------------------------------------------------------------------------------------------------------
static void testModelExport() {
    ModelParts parts;
    int models = 0;
    for (int kind = 0; kind < MODEL_KINDS; kind++)
        for (int i = 0; i < modelCount(kind); i++) {
            if (!modelExists(kind, i)) continue;
            models++;
            defaultParts(kind, i, parts);
            ACHECK(!parts.empty() && parts.size() <= kMaxDefaultParts);
            ACHECK(modelPath(kind, i).find("//") == std::string::npos && modelName(kind, i)[0]);
            MeshData a, b;
            partsToMesh(parts, a);
            ACHECK(a.triangleCount() == parts.size() * 12);
            std::string g = writeGltf(a, modelName(kind, i), &parts);
            std::string err;
            ModelParts boxes;
            bool ok = loadGltf((const uint8_t*)g.data(), g.size(), nullptr, b, &err, &boxes);
            if (!ok) printf("  %s: %s\n", modelPath(kind, i).c_str(), err.c_str());
            ACHECK(ok);
            // the box model round trip: the file's boxes are the export's exactly (every float as written), and drawn
            // with any palette (and crouched) they are the built-in model's boxes
            ACHECK(boxes.size() == parts.size());
            finishBoxModel(kind, i, boxes);
            for (size_t k = 0; k < boxes.size() && k < parts.size(); k++) {
                const ModelPart &p = parts[k], &q = boxes[k];
                bool same = !memcmp(&p.c, &q.c, sizeof p.c) && !memcmp(&p.h, &q.h, sizeof p.h) && !memcmp(&p.color, &q.color, sizeof p.color) &&
                            p.slot == q.slot && p.alpha == q.alpha && p.shadow == q.shadow && p.visOnly == q.visOnly && p.crouch == q.crouch &&
                            (p.slot == TINT_NONE || (!memcmp(&p.mul, &q.mul, sizeof p.mul) && !memcmp(&p.add, &q.add, sizeof p.add) && !memcmp(&p.teamMul, &q.teamMul, sizeof p.teamMul)));
                if (!same) printf("  %s box %zu differs after the round trip\n", modelPath(kind, i).c_str(), k);
                ACHECK(same);
            }
            for (int v = 0; v < 3; v++) {
                ModelPalette pal = referencePalette(kind, i);
                const Vec3 cols[3] = {{0.2f, 0.4f, 0.8f}, {1.0f, 0.3f, 0.25f}, {0.05f, 0.9f, 0.1f}};
                for (int s = 1; s < TINT_COUNT; s++) if (drivenSlots(kind, i) >> s & 1) pal.c[s] = cols[(v + s) % 3];
                const float hs = kind == MODEL_PLAYER && v == 1 ? kPlayerCrouchHeight / kPlayerHeight : 1.0f;
                ModelParts want, got;
                builtIn(kind, i, pal, hs, want);
                overrideBoxParts(kind, i, boxes, pal, hs, got);
                ACHECK(want.size() == got.size());
                for (size_t k = 0; k < want.size() && k < got.size(); k++) {
                    const ModelPart &w = want[k], &o = got[k];
                    bool geo = !memcmp(&w.c, &o.c, sizeof w.c) && !memcmp(&w.h, &o.h, sizeof w.h) && w.visOnly == o.visOnly && w.shadow == o.shadow && w.alpha == o.alpha;
                    bool col = length(w.color - o.color) < 1e-5f * std::max(1.0f, length(w.color));   // (within float rounding)
                    if (!geo || !col) printf("  %s box %zu drawn differently (palette %d)\n", modelPath(kind, i).c_str(), k, v);
                    ACHECK(geo && col);
                }
            }
            ACHECK(b.triangleCount() == a.triangleCount());
            Vec3 amn, amx, bmn, bmx;
            ACHECK(a.bounds(amn, amx) && b.bounds(bmn, bmx));
            ACHECK(length(amn - bmn) < 1e-5f && length(amx - bmx) < 1e-5f);
            for (auto& p : b.prims) ACHECK(p.slot == tintSlotFromName(p.material));
            if (kind == MODEL_PLAYER) ACHECK(amn.y > -0.01f && amx.y < 2.1f);   // feet on the ground, 2 m tall
            ModelMesh mm;
            meshToModel(b, mm);
            ACHECK(mm.opaque.size() + mm.translucent.size() == b.triangleCount() * 3 && mm.radius > 0);
        }
    ACHECK(models == 5 + 11 + 7 + 1 + 6);   // (weapons: 11 with the locator; props: GUN_ARM ARM BOMB_PACK BOMB CART GATE)
    ACHECK(modelPath(MODEL_PROP, PROP_CART) == "models/prop/CART" && modelPath(MODEL_PROP, PROP_BOMB_PACK) == "models/prop/BOMB_PACK");
    // a prop's slot colours come from the palette (the cart: 11 boxes, its band the team colour, its rim the accent)
    {
        ModelPalette cp;
        cp.c[TINT_TEAM] = {0.2f, 0.4f, 0.8f};
        cp.c[TINT_ACCENT] = {0.5f, 0.1f, 0.1f};
        propParts(PROP_CART, cp, parts);
        ACHECK(parts.size() == 11 && parts[5].slot == TINT_TEAM && length(parts[5].color - cp.c[TINT_TEAM] * 0.9f) < 1e-6f);
        ACHECK(parts[6].slot == TINT_ACCENT && length(parts[6].color - cp.c[TINT_ACCENT]) < 1e-6f);
        ACHECK(tintSlotFromName("Accent_rim") == TINT_ACCENT);
    }
    // the default parts are what drawWorld draws: a known box, the standard model's chest, team coloured
    ModelPalette pal;
    pal.c[TINT_TEAM] = {0.2f, 0.4f, 0.8f};
    playerModelParts(0, 1.0f, pal, parts);
    ACHECK(parts.size() == 4 && std::fabs(parts[1].c.y - 1.15f) < 1e-6f && length(parts[1].color - pal.c[TINT_TEAM]) < 1e-6f && parts[3].visOnly);
    ACHECK(tintSlotFromName("Team_Armour.001") == TINT_TEAM && tintSlotFromName("GLOW") == TINT_GLOW && tintSlotFromName("skin") == TINT_NONE);
}

// Box model files: what the loader takes and what it refuses (a refused file keeps the default)
static void testBoxModels() {
    ModelParts p;
    std::string err;
    const std::string box = "{\"c\":[0,1,0],\"h\":[0.2,0.3,0.4],\"slot\":\"team\",\"color\":[0.5,0.5,0.5]";
    ACHECK(parseBoxModel("{\"version\":2,\"boxes\":[" + box + ",\"mul\":[0.6,0.6,0.6],\"add\":[0.06,0.06,0.07],\"crouch\":true,\"name\":\"legs\"}]}", p, &err));
    ACHECK(p.size() == 1 && p[0].slot == TINT_TEAM && p[0].crouch && p[0].shadow && !p[0].visOnly && std::fabs(p[0].add.z - 0.07f) < 1e-7f);
    // no mul: the colour over the reference (team: white), as a mesh is tinted
    ACHECK(parseBoxModel("{\"version\":2,\"boxes\":[" + box + "}]}", p, &err));
    finishBoxModel(MODEL_PLAYER, 0, p);
    ACHECK(p.size() == 1 && std::fabs(p[0].mul.x - 0.5f) < 1e-7f && p[0].add.x == 0);
    ModelPalette team;
    team.c[TINT_TEAM] = {0.2f, 0.4f, 0.8f};
    ModelParts drawn;
    overrideBoxParts(MODEL_PLAYER, 0, p, team, 0.5f, drawn);
    ACHECK(drawn.size() == 1 && std::fabs(drawn[0].color.z - 0.4f) < 1e-6f && std::fabs(drawn[0].c.y - 0.5f) < 1e-6f && drawn[0].h.y == 0.3f);
    const char* bad[] = {
        "{\"version\":2,\"boxes\":[]}",                                                              // no boxes
        "{\"version\":2,\"boxes\":[{\"c\":[0,1],\"h\":[1,1,1],\"color\":[1,1,1]}]}",                   // c: 2 numbers
        "{\"version\":2,\"boxes\":[{\"c\":[0,1e999,0],\"h\":[1,1,1],\"color\":[1,1,1]}]}",             // not finite
        "{\"version\":2,\"boxes\":[{\"c\":[0,100,0],\"h\":[1,1,1],\"color\":[1,1,1]}]}",               // too far
        "{\"version\":2,\"boxes\":[{\"c\":[0,1,0],\"h\":[1,0,1],\"color\":[1,1,1]}]}",                 // flat
        "{\"version\":2,\"boxes\":[{\"c\":[0,1,0],\"h\":[1,-1,1],\"color\":[1,1,1]}]}",                // negative
        "{\"version\":2,\"boxes\":[{\"c\":[0,1,0],\"h\":[40,1,1],\"color\":[1,1,1]}]}",                // huge
        "{\"version\":2,\"boxes\":[{\"c\":[0,1,0],\"h\":[1,1,1],\"color\":[1,-1,1]}]}",               // colour < 0
        "{\"version\":2,\"boxes\":[{\"c\":[0,1,0],\"h\":[1,1,1],\"color\":[1,99,1]}]}",               // colour > 16
        "{\"version\":2,\"boxes\":[{\"c\":[0,1,0],\"h\":[1,1,1],\"color\":[1,1,1],\"slot\":\"neon\"}]}",  // unknown slot
        "{\"version\":2,\"boxes\":[{\"c\":[0,1,0],\"h\":[1,1,1],\"color\":[1,1,1],\"alpha\":0}]}",     // invisible
        "{\"version\":2,\"boxes\":[{\"c\":[0,1,0],\"h\":[1,1,1],\"color\":[1,1,1],\"shadow\":1}]}",    // not a bool
        "{\"version\":2,\"boxes\":[{\"c\":[0,1,0],\"h\":[1,1,1],\"color\":[1,1,1],\"slot\":\"team\",\"mul\":[1,1]}]}",
        "{\"version\":2,\"boxes\":[{\"c\":\"0 1 0\",\"h\":[1,1,1],\"color\":[1,1,1]}]}",
        "{\"version\":1,\"boxes\":[]}",                                                              // not version 2
        "{\"version\":2,\"boxes\":[{\"c\":[0,1,0]",                                                     // cut off
    };
    for (const char* b : bad) {
        bool ok = parseBoxModel(b, p, &err);
        if (ok) printf("  box model accepted: %s\n", b);
        ACHECK(!ok && p.empty());
    }
    std::string many = "{\"version\":2,\"boxes\":[";   // too many boxes
    for (size_t i = 0; i <= kMaxOverrideBoxes; i++) many += std::string(i ? "," : "") + "{\"c\":[0,1,0],\"h\":[1,1,1],\"color\":[1,1,1]}";
    ACHECK(!parseBoxModel(many + "]}", p, &err));
    // in a file: a bad box list fails the load (the default is kept); the model editor's older notes (version 1) and
    // no extras at all are a plain mesh
    ModelParts parts, boxes;
    playerModelParts(0, 1.0f, referencePalette(MODEL_PLAYER), parts);
    MeshData mesh, back;
    partsToMesh(parts, mesh);
    std::string g = writeGltf(mesh, "STANDARD");
    ACHECK(loadGltf((const uint8_t*)g.data(), g.size(), nullptr, back, &err, &boxes) && boxes.empty());
    parts[0].h.y = -1;
    g = writeGltf(mesh, "STANDARD", &parts);
    ACHECK(!loadGltf((const uint8_t*)g.data(), g.size(), nullptr, back, &err, &boxes) && boxes.empty() && err.find("box model") == 0);
    std::string v1 = writeGltf(mesh, "STANDARD");
    v1.replace(v1.find("\"primitives\""), 0, "\"extras\":{\"quark\":{\"version\":1,\"boxes\":[[{\"i\":0,\"name\":\"\",\"shadow\":true,\"visOnly\":false}]]}},");
    ACHECK(loadGltf((const uint8_t*)v1.data(), v1.size(), nullptr, back, &err, &boxes) && boxes.empty());
    // slots a model's drawing doesn't colour stay at the reference (an override's "glow" on a player: tint 1)
    ModelPalette pl;
    pl.c[TINT_TEAM] = {0.2f, 0.4f, 0.8f};
    for (int kind : {MODEL_PLAYER, MODEL_CROWN, MODEL_PROJECTILE}) {
        ModelPalette d = drivenPalette(kind, 0, pl);
        ACHECK(length(d.c[TINT_TEAM] - pl.c[TINT_TEAM]) < 1e-7f);
        for (int s : {(int)TINT_METAL, (int)TINT_BASE, (int)TINT_ACCENT}) ACHECK(length(d.c[s] - referencePalette(kind, 0).c[s]) < 1e-7f);
        if (kind != MODEL_PROJECTILE) ACHECK(length(d.c[TINT_GLOW] - referencePalette(kind, 0).c[TINT_GLOW]) < 1e-7f);
    }
}

static void testGltfLoader() {
    // a hand-written file: one triangle, a node moved +1 in x and scaled 2 (TRS), a "team" material with a colour,
    // no normals (made flat), indices as u16
    const float pos[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const uint16_t idx[3] = {0, 1, 2};
    std::vector<uint8_t> bin(sizeof pos + 8);
    memcpy(bin.data(), pos, sizeof pos);
    memcpy(bin.data() + sizeof pos, idx, sizeof idx);
    std::string json = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"children\":[1],\"translation\":[1,0,0]},{\"mesh\":0,\"scale\":[2,2,2]}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":0}]}],"
        "\"materials\":[{\"name\":\"team_cloth\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.5,0.25,1,1]}}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
        "\"buffers\":[{\"byteLength\":44}]}";
    while (json.size() % 4) json += ' ';
    // as a .glb (JSON chunk + BIN chunk)
    std::string glb = "glTF";
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) glb += (char)(v >> (8 * i) & 255); };
    u32(2); u32((uint32_t)(12 + 8 + json.size() + 8 + bin.size()));
    u32((uint32_t)json.size()); u32(0x4E4F534A); glb += json;
    u32((uint32_t)bin.size()); u32(0x004E4942); glb.append((const char*)bin.data(), bin.size());
    MeshData m;
    std::string err;
    bool ok = loadGltf((const uint8_t*)glb.data(), glb.size(), nullptr, m, &err);
    if (!ok) printf("  glb: %s\n", err.c_str());
    ACHECK(ok && m.prims.size() == 1);
    if (ok && m.prims.size() == 1) {
        const MeshPrim& p = m.prims[0];
        ACHECK(p.slot == TINT_TEAM && p.pos.size() == 9);
        ACHECK(std::fabs(p.pos[0] - 1) < 1e-6f && std::fabs(p.pos[3] - 3) < 1e-6f && std::fabs(p.pos[7] - 2) < 1e-6f);   // T * S
        ACHECK(std::fabs(p.nrm[2] - 1) < 1e-6f);                                                              // flat: +Z
        ACHECK(std::fabs(p.col[0] - 0.5f) < 1e-6f && std::fabs(p.col[1] - 0.25f) < 1e-6f);                    // baseColorFactor
    }
    // a separate buffer file, found through the resolver; junk is refused
    std::string sep = json;
    sep.replace(sep.find("{\"byteLength\":44}"), 17, "{\"byteLength\":44,\"uri\":\"tri.bin\"}");
    ok = loadGltf((const uint8_t*)sep.data(), sep.size(), [&](const std::string& uri, std::vector<uint8_t>& out) { out = bin; return uri == "tri.bin"; }, m, &err);
    ACHECK(ok && m.triangleCount() == 1);
    ACHECK(!loadGltf((const uint8_t*)"{\"nodes\":[", 10, nullptr, m, &err));
    ACHECK(!loadGltf((const uint8_t*)"glTF\2\0\0\0\xff\xff\0\0", 12, nullptr, m, &err));
}

static void testDecoders() {
    const fs::path data = testData();
    for (const char* f : {"tone.ogg", "tone.flac", "tone.mp3"}) {
        std::string b = slurp(data / f);
        std::vector<float> x;
        int rate = 0;
        std::string err;
        bool ok = decodeSound((const uint8_t*)b.data(), b.size(), x, rate, &err);
        if (!ok) printf("  %s: %s\n", f, err.c_str());
        ACHECK(ok && rate == 22050);
        ACHECK(x.size() > 22050 / 5 && x.size() < 22050 / 2 + 2048);   // 0.25 s (+ codec padding)
        ACHECK(rms(x) > 0.05f);                                         // (ffmpeg's sine is 1/8 of full scale: rms 0.088)
    }
    std::vector<float> tone(4800), back;
    for (size_t i = 0; i < tone.size(); i++) tone[i] = 0.5f * std::sin(i * 0.1f);
    std::string w = wavFile(tone, 48000);
    int rate = 0;
    ACHECK(decodeSound((const uint8_t*)w.data(), w.size(), back, rate) && rate == 48000 && back.size() == tone.size());
    ACHECK(std::fabs(back[100] - tone[100]) < 1e-3f);
    ACHECK(!decodeSound((const uint8_t*)"RIFF0000WAVE", 12, back, rate));
    std::string png = slurp(srcDir() / "assets/quark.png");
    int iw = 0, ih = 0;
    std::vector<uint8_t> rgba;
    ACHECK(decodeImage((const uint8_t*)png.data(), png.size(), iw, ih, rgba) && iw == 256 && ih == 256 && rgba.size() == 256u * 256 * 4);
}

static void testFontAtlas() {
    std::string ttf = slurp(srcDir() / "assets/fonts/WorkSans.ttf");
    const FontAtlasLayout L = {1056, 462, 66, 6.0f, 1.5f, 0.5f, 9.0f};   // font_ui.h
    FontAtlas a;
    std::string err;
    bool ok = buildFontAtlas((const uint8_t*)ttf.data(), ttf.size(), L, a, &err);
    if (!ok) printf("  font: %s\n", err.c_str());
    ACHECK(ok && a.tex.size() == (size_t)L.texW * L.texH);
    if (!ok) return;
    auto cellMax = [&](char ch) { int g = ch - 32, cx = g % 16 * L.cell, cy = g / 16 * L.cell, m = 0;
        for (int y = 0; y < L.cell; y++) for (int x = 0; x < L.cell; x++) m = std::max(m, (int)a.tex[(size_t)(cy + y) * L.texW + cx + x]);
        return m; };
    ACHECK(cellMax('H') > 140 && cellMax('g') > 140 && cellMax(' ') == 0);
    ACHECK(a.tex[(size_t)(6 * L.cell + 5) * L.texW + 5] == 255);                          // the solid block
    ACHECK(a.adv['W' - 32] > a.adv['i' - 32] && a.adv['A' - 32] > 3 && a.adv['A' - 32] < 8);
    // the cap top sits 0.75 units below the line top (+ padding), as tools/make_font_atlas.py lays it out
    int g = 'H' - 32, cx = g % 16 * L.cell, cy = g / 16 * L.cell, top = -1;
    for (int y = 0; y < L.cell && top < 0; y++) for (int x = 0; x < L.cell; x++) if (a.tex[(size_t)(cy + y) * L.texW + cx + x] >= 128) { top = y; break; }
    ACHECK(std::abs(top - (int)((1.5f + 0.75f) * 6)) <= 1);
    ACHECK(!buildFontAtlas((const uint8_t*)"not a font at all", 17, L, a, &err));
}

// Settings > FONT (uifonts.h): every built-in loads, a font is sized to Rubik's capitals, Quake's console font sheet
// (from a WAD2 / PAK too) becomes a pixel atlas, and a mods/fonts name can't leave that folder
static void testUiFonts() {
    printf("fonts: the built-ins, capital sizing, a pixel sheet, WAD2 / PAK\n");
    const FontAtlasLayout L = {1056, 462, 66, 6.0f, 1.5f, 0.5f, 9.0f, 5.25f};   // Renderer::uiFontLayout()
    auto inkRows = [&](const FontAtlas& a, char ch, int* top) {   // rows of the glyph's cell with ink (>= the edge)
        int g = ch - 32, cx = g % 16 * L.cell, cy = g / 16 * L.cell, n = 0;
        *top = -1;
        for (int y = 0; y < L.cell; y++) {
            bool any = false;
            for (int x = 0; x < L.cell; x++) any |= a.tex[(size_t)(cy + y) * L.texW + cx + x] >= 128;
            if (any) { n++; if (*top < 0) *top = y; }
        }
        return n;
    };
    // capitals as tall as Rubik's (5.25 units = 31.5 texels), their top 0.75 units below the line top
    std::string ttf = slurp(srcDir() / "assets/fonts/WorkSans.ttf");
    FontAtlas a;
    std::string err;
    ACHECK(buildFontAtlas((const uint8_t*)ttf.data(), ttf.size(), L, a, &err));
    int top = 0, rows = inkRows(a, 'H', &top);
    printf("  work sans at rubik's cap height: H %d texels tall, top at %d\n", rows, top);
    ACHECK(std::abs(rows - 31) <= 2 && std::abs(top - (int)((1.5f + 0.75f) * 6)) <= 1);
    // every built-in loads (the default: font_ui.h as it is, no atlas)
    std::unique_ptr<FontAtlas> fa;
    ACHECK(uifonts::load("rubik", L, fa, &err) && !fa);
    ACHECK(uifonts::load("", L, fa, &err) && !fa);
    for (const uifonts::Entry& e : uifonts::scan()) {
        if (e.user) continue;
        bool ok = uifonts::load(e.id, L, fa, &err);
        if (!ok) printf("  %s: %s\n", e.id.c_str(), err.c_str());
        ACHECK(ok && (e.id == "rubik" || (fa && fa->tex.size() == (size_t)L.texW * L.texH && fa->adv['H' - 32] > 2)));
    }
    // QUARK CLASSIC: the original metrics (6 units a glyph, 7 pixels from the line top, lower case as upper case)
    ACHECK(uifonts::load("classic", L, fa, &err) && fa);
    if (fa) {
        ACHECK(fa->adv['H' - 32] == 6.0f && fa->adv['i' - 32] == 6.0f && fa->minScale == 1.0f);
        rows = inkRows(*fa, 'H', &top);
        ACHECK(rows == 7 * 6 && top == (int)(1.5f * 6));
        ACHECK(!std::memcmp(&fa->tex[(size_t)(2 * L.cell + 5) * L.texW + 4 * L.cell], &fa->tex[(size_t)(4 * L.cell + 5) * L.texW + 4 * L.cell], 40));   // ('a' = 'A')
    }
    // a mods/fonts name stays in that folder; a missing file fails
    ACHECK(!uifonts::load("../quark.cfg", L, fa, &err) && !uifonts::load("no such font.ttf", L, fa, &err));
    // a Quake-style sheet: 1 unit a pixel from the line top, square, each glyph as wide as its ink (+ column 0)
    std::vector<uint8_t> sheet(128 * 128, 0);
    auto px = [&](int c, int x, int y) { sheet[(size_t)((c / 16) * 8 + y) * 128 + (c % 16) * 8 + x] = 37; };
    for (int y = 1; y < 8; y++) { px('H', 1, y); px('H', 5, y); }
    for (int x = 1; x <= 5; x++) px('H', x, 4);
    px('.', 3, 7);
    FontAtlas q;
    ACHECK(buildSheetAtlas(sheet.data(), 128, 128, L, q, &err));
    rows = inkRows(q, 'H', &top);
    ACHECK(rows == 7 * 6 && top == (int)((1.5f + 1) * 6));
    ACHECK(q.adv['H' - 32] == 6.0f && q.adv['.' - 32] == 4.0f && q.adv['A' - 32] == 4.0f && inkRows(q, 'A', &top) == 0 && q.minScale == 1.0f);
    std::vector<uint8_t> blank(128 * 128, 0);
    ACHECK(!buildSheetAtlas(blank.data(), 128, 128, L, q, &err));   // (no H)
    // ...read from gfx.wad's CONCHARS lump, and from a PAK holding that gfx.wad
    auto put32 = [](std::vector<uint8_t>& v, size_t at, int32_t x) { for (int i = 0; i < 4; i++) v[at + i] = (uint8_t)(x >> (8 * i)); };
    std::vector<uint8_t> wad(12 + sheet.size() + 32, 0);
    std::memcpy(wad.data(), "WAD2", 4);
    put32(wad, 4, 1); put32(wad, 8, (int32_t)(12 + sheet.size()));
    std::memcpy(&wad[12], sheet.data(), sheet.size());
    size_t e = 12 + sheet.size();
    put32(wad, e, 12); put32(wad, e + 4, (int32_t)sheet.size()); put32(wad, e + 8, (int32_t)sheet.size());
    wad[e + 12] = 0x44; std::memcpy(&wad[e + 16], "CONCHARS", 8);
    std::vector<uint8_t> pak(12 + wad.size() + 64, 0);
    std::memcpy(pak.data(), "PACK", 4);
    put32(pak, 4, (int32_t)(12 + wad.size())); put32(pak, 8, 64);
    std::memcpy(&pak[12], wad.data(), wad.size());
    std::memcpy(&pak[12 + wad.size()], "gfx.wad", 7);
    put32(pak, 12 + wad.size() + 56, 12); put32(pak, 12 + wad.size() + 60, (int32_t)wad.size());
    for (const std::vector<uint8_t>* f : {&sheet, &wad, &pak}) {
        std::vector<uint8_t> ink;
        int w = 0, h = 0;
        ACHECK(quakeConchars(f->data(), f->size(), ink, w, h, &err) && w == 128 && h == 128);
        ACHECK(ink.size() == sheet.size() && ink[(size_t)(4 * 8 + 4) * 128 + 8 * 8 + 3] == 1 && ink[0] == 0);   // ('H' row 4)
    }
    std::vector<uint8_t> bad = wad;
    std::memcpy(&bad[e + 16], "NOTCHARS", 8);
    std::vector<uint8_t> ink;
    int w = 0, h = 0;
    ACHECK(!quakeConchars(bad.data(), bad.size(), ink, w, h, &err));
}

// The hitsound's tiers: picked by the hit's damage at exact boundaries; one per moment (the heaviest); in every kit
// four distinct sounds, each louder (heard loudness, kHitTierGain) and heavier than the last, none clipping
static float lowShare(const std::vector<float>& x) {   // the share of the energy below ~200 Hz
    const double a = 1.0 - std::exp(-2.0 * 3.14159265 * 200.0 / Audio::kRate);
    double l = 0, el = 0, et = 0;
    for (float v : x) { l += (v - l) * a; el += l * l; et += (double)v * v; }
    return et > 0 ? (float)(el / et) : 0.0f;
}
static float peakOf(const std::vector<float>& x) { float m = 0; for (float v : x) m = std::max(m, std::fabs(v)); return m; }
static void testHitTiers() {
    ACHECK(hitSoundFor(0.0f) == SND_HIT && hitSoundFor(0.04f) == SND_HIT && hitSoundFor(9.99f) == SND_HIT);
    ACHECK(hitSoundFor(10.0f) == SND_HIT_MED && hitSoundFor(24.99f) == SND_HIT_MED);
    ACHECK(hitSoundFor(25.0f) == SND_HIT_HEAVY && hitSoundFor(50.0f) == SND_HIT_HEAVY);
    ACHECK(hitSoundFor(50.01f) == SND_HIT_BRUTAL && hitSoundFor(300.0f) == SND_HIT_BRUTAL);
    // as the client gets them: EV_HIT's damage in hundredths of a point
    ACHECK(hitSoundFor(999 / 100.0f) == SND_HIT && hitSoundFor(1000 / 100.0f) == SND_HIT_MED && hitSoundFor(2499 / 100.0f) == SND_HIT_MED);
    ACHECK(hitSoundFor(2500 / 100.0f) == SND_HIT_HEAVY && hitSoundFor(5000 / 100.0f) == SND_HIT_HEAVY && hitSoundFor(5001 / 100.0f) == SND_HIT_BRUTAL);
    ACHECK(hitTier(SND_HIT) == 0 && hitTier(SND_HIT_BRUTAL) == 3 && hitTier(SND_KILL) == -1 && hitTier(SND_FINISH) == -1);
    // the gate: batched laser ticks (every 50 ms) all play; hits landing together make one sound, a heavier one gets through
    HitSoundGate g;
    ACHECK(g.pass(1.0, SND_HIT) && g.pass(1.05, SND_HIT) && g.pass(1.10, SND_HIT));
    ACHECK(!g.pass(1.11, SND_HIT) && g.pass(1.12, SND_HIT_HEAVY) && !g.pass(1.13, SND_HIT_MED) && !g.pass(1.14, SND_HIT_HEAVY));
    ACHECK(g.pass(1.15, SND_HIT_BRUTAL) && g.pass(1.20, SND_HIT));
    Audio a;
    a.startOffline();
    const int tiers[4] = {SND_HIT, SND_HIT_MED, SND_HIT_HEAVY, SND_HIT_BRUTAL};
    const float ref = Audio::heardLoudness(a.samples(0, SND_HIT)) / kHitTierGain[0];
    for (int pack = 0; pack < Audio::kPacks; pack++) {
        for (int t = 0; t < 4; t++) {
            const auto& x = a.samples(pack, tiers[t]);
            ACHECK(x.size() > 1000 && x.size() < (size_t)Audio::kRate);
            ACHECK(peakOf(x) <= 0.95f);   // headroom: never clipping, even the brutal one
            float l = Audio::heardLoudness(x);
            if (pack != Audio::kPackRetro) ACHECK(std::fabs(l / ref - kHitTierGain[t]) < 0.03f);   // the ladder, in every kit
            if (pack != 0) ACHECK(x != a.samples(0, tiers[t]));   // each kit its own (RETRO: DEFAULT's, chiptuned)
            for (int u = 0; u < t; u++) {
                const auto& y = a.samples(pack, tiers[u]);
                ACHECK(x != y);
                ACHECK(l > Audio::heardLoudness(y) * 1.05f);   // louder than every lighter tier
            }
            if (pack == 0 && t > 0) {   // DEFAULT: each heavier (more low end) and longer than the last
                ACHECK(lowShare(x) > lowShare(a.samples(0, tiers[t - 1])) + 0.05f);
                ACHECK(x.size() > a.samples(0, tiers[t - 1]).size());
            }
        }
        ACHECK(lowShare(a.samples(pack, SND_HIT_BRUTAL)) > lowShare(a.samples(pack, SND_HIT)));   // the brutal one heavier, every kit
    }
    ACHECK(std::string(Audio::soundName(SND_HIT)) == "HIT" && std::string(Audio::soundName(SND_HIT_MED)) == "HIT_MED" &&
           std::string(Audio::soundName(SND_HIT_HEAVY)) == "HIT_HEAVY" && std::string(Audio::soundName(SND_HIT_BRUTAL)) == "HIT_BRUTAL");
    a.stop();
}

// Overrides from the per-user folder: found by path (any case, any accepted extension), and the audio uses them
static void testOverrides() {
    fs::path root = fs::u8path(userDataPath("overrides"));
    std::error_code ec;
    fs::remove_all(root, ec);
    overrides::useUserFolder(true);
    for (auto& l : overrides::list()) ACHECK(l.rfind("shipped:", 0) == 0);   // (none of yours yet)
    // a 0.2 s tone for DEFAULT's HIT (as an upload keeps its name: lower case, .wav)
    std::vector<float> tone(9600);
    for (size_t i = 0; i < tone.size(); i++) tone[i] = 0.8f * std::sin(i * 0.2f) * (1.0f - (float)i / tone.size());
    ACHECK(spit(root / "sounds/default/hit.wav", wavFile(tone, 48000)));
    ACHECK(spit(root / "voices/male/GO.ogg", slurp(testData() / "tone.ogg")));
    // a hitsound tier: MOUTH's brutal hit, a low 0.3 s tone over a DC offset (a rumble it must not clip on)
    std::vector<float> thud(14400);
    for (size_t i = 0; i < thud.size(); i++) thud[i] = 0.3f + 0.6f * std::sin(i * 0.012f) * (1.0f - (float)i / thud.size()) + 0.05f * std::sin(i * 0.4f);
    ACHECK(spit(root / "sounds/mouth/HIT_BRUTAL.wav", wavFile(thud, 48000)));
    ACHECK(spit(root / "models/player/HEAVY.gltf", "this is not glTF"));
    {   // a player model of yours: a giant (the standard model, three times as wide)
        ModelParts parts;
        MeshData mesh;
        playerModelParts(0, 1.0f, referencePalette(MODEL_PLAYER), parts);
        for (ModelPart& q : parts) q.h = q.h * 3.0f;
        partsToMesh(parts, mesh);
        ACHECK(spit(root / "models/player/STANDARD.gltf", writeGltf(mesh, "STANDARD")));
    }
    overrides::useUserFolder(true);   // (re-read the folder)
    ACHECK(overrides::any());
    std::vector<uint8_t> b;
    std::string matched, where;
    ACHECK(overrides::find("sounds/default/HIT", {".ogg", ".wav"}, b, &matched, &where) && where == "user" && matched == "sounds/default/hit.wav");
    ACHECK(!overrides::find("sounds/default/KILL", {".wav"}, b));
    where.clear();   // (only the shipped ones / only yours)
    if (overrides::find("sounds/default/HIT", {".ogg", ".wav"}, b, nullptr, &where, overrides::FROM_SHIPPED)) ACHECK(where == "shipped");
    ACHECK(overrides::find("sounds/default/HIT", {".ogg", ".wav"}, b, nullptr, &where, overrides::FROM_USER) && where == "user");
    ACHECK(!overrides::find("sounds/default/KILL", {".wav"}, b, nullptr, nullptr, overrides::FROM_USER));
    // models: yours are drawn, but ranked draws only the shipped ones (or the box model): a giant or neon enemy model
    // of your own would be an edge that nobody else has
    ModelOverrides mo;
    loadModelOverrides(mo);
    ACHECK(mo.user[MODEL_PLAYER][0] && mo.get(MODEL_PLAYER, 0, false) == mo.user[MODEL_PLAYER][0].get());
    ACHECK(mo.get(MODEL_PLAYER, 0, true) == mo.shipped[MODEL_PLAYER][0].get());
    ACHECK(!mo.user[MODEL_PLAYER][1]);   // (HEAVY: not glTF, left out)
    for (int kind = 0; kind < MODEL_KINDS; kind++)
        for (int i = 0; i < kMaxModelsPerKind; i++) ACHECK(mo.get(kind, i, true) == mo.shipped[kind][i].get());
    Audio plain, mine;
    overrides::useUserFolder(false);
    plain.startOffline();
    overrides::useUserFolder(true);
    mine.startOffline();
    const auto& d0 = plain.samples(0, SND_HIT);
    const auto& d1 = mine.samples(0, SND_HIT);
    ACHECK(d1 != d0 && d1.size() >= 9000 && d1.size() <= 9600);
    ACHECK(mine.samples(Audio::kPackRetro, SND_HIT) != plain.samples(Audio::kPackRetro, SND_HIT));   // RETRO: the new DEFAULT, chiptuned
    ACHECK(mine.samples(Audio::kPackMetal, SND_HIT) == plain.samples(Audio::kPackMetal, SND_HIT));   // a kit's own sound stays
    ACHECK(mine.samples(0, SND_KILL) == plain.samples(0, SND_KILL));   // the rest untouched
    const auto& b1 = mine.samples(Audio::kPackWooden, SND_HIT_BRUTAL);   // (pack 4: MOUTH)
    ACHECK(b1 != plain.samples(Audio::kPackWooden, SND_HIT_BRUTAL) && b1.size() >= 14000 && b1.size() <= 14400);
    ACHECK(peakOf(b1) <= 0.95f);   // levelled as heard, its rumble cut, never clipping
    ACHECK(std::fabs(Audio::heardLoudness(b1) / Audio::heardLoudness(plain.samples(0, SND_HIT_BRUTAL)) - 1.0f) < 0.05f);
    ACHECK(mine.samples(Audio::kPackWooden, SND_HIT_HEAVY) == plain.samples(Audio::kPackWooden, SND_HIT_HEAVY));
    ACHECK(mine.samples(0, SND_HIT_BRUTAL) == plain.samples(0, SND_HIT_BRUTAL));
    ACHECK(mine.samples(0, SND_VOICE_GO) != plain.samples(0, SND_VOICE_GO));
    ACHECK(mine.samples(0, SND_VOICE_ONE) == plain.samples(0, SND_VOICE_ONE));
    fs::remove_all(root, ec);
    overrides::useUserFolder(true);
}

// sound-levels.txt: a listed replacement keeps its own level (the sound editor levels groups by LUFS); only a peak
// over 1.0 is scaled down to 1.0. Unlisted ones are still matched to the sound they replace. Any case, comments.
static void testSoundLevels() {
    printf("audio: overrides listed in sound-levels.txt keep their own level\n");
    fs::path root = fs::u8path(userDataPath("overrides"));
    std::error_code ec;
    fs::remove_all(root, ec);
    auto floatWav = [](const std::vector<float>& x, int rate) {   // 32-bit float: may go over 1.0
        std::string d;
        auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) d += (char)(v >> (8 * i) & 255); };
        auto u16 = [&](uint16_t v) { d += (char)(v & 255); d += (char)(v >> 8); };
        uint32_t bytes = (uint32_t)x.size() * 4;
        d += "RIFF"; u32(36 + bytes); d += "WAVEfmt "; u32(16); u16(3); u16(1); u32((uint32_t)rate); u32((uint32_t)rate * 4); u16(4); u16(32);
        d += "data"; u32(bytes);
        for (float v : x) { uint32_t b; memcpy(&b, &v, 4); u32(b); }
        return d;
    };
    auto tone = [](float amp, size_t n) {
        std::vector<float> t(n);
        for (size_t i = 0; i < n; i++) t[i] = amp * std::sin(i * 0.05f);
        return t;
    };
    const std::vector<float> quiet = tone(0.1f, 14400), loud = tone(2.5f, 14400);   // 0.3 s each
    ACHECK(spit(root / "sounds/default/ROCKET.wav", wavFile(quiet, 48000)));     // listed (in other case)
    ACHECK(spit(root / "sounds/default/SHOTGUN.wav", wavFile(quiet, 48000)));    // not listed: matched as before
    ACHECK(spit(root / "sounds/mouth/HIT_MED.wav", wavFile(quiet, 48000)));      // a hitsound tier, listed: no levelHit
    ACHECK(spit(root / "sounds/heavy_metal/SNIPER.wav", floatWav(loud, 48000))); // listed, peak 2.5: limited to 1.0
    ACHECK(spit(root / "voices/male/GO.wav", wavFile(quiet, 48000)));            // a voice line, listed
    ACHECK(spit(root / "voices/male/ONE.wav", wavFile(quiet, 48000)));           // not listed
    ACHECK(spit(root / "Sound-Levels.txt", "# levelled in the sound editor\r\n  Sounds/Default/rocket \r\n\r\nsounds\\mouth\\hit_med\n"
                                           "SOUNDS/HEAVY_METAL/SNIPER.wav\nvoices/MALE/go\n#voices/male/ONE\n"));
    Audio plain, mine;
    overrides::useUserFolder(false);
    plain.startOffline();
    overrides::useUserFolder(true);
    ACHECK(overrides::soundLevels().find("Sounds/Default/rocket") != std::string::npos);
    mine.startOffline();
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.004f; };
    const auto& rocket = mine.samples(0, SND_ROCKET);
    printf("  peaks: kept ROCKET %.3f, matched SHOTGUN %.3f, kept HIT_MED %.3f, limited SNIPER %.3f, kept GO %.3f, matched ONE %.3f\n",
           peakOf(rocket), peakOf(mine.samples(0, SND_SHOTGUN)), peakOf(mine.samples(Audio::kPackWooden, SND_HIT_MED)),
           peakOf(mine.samples(Audio::kPackMetal, SND_SNIPER)), peakOf(mine.samples(0, SND_VOICE_GO)), peakOf(mine.samples(0, SND_VOICE_ONE)));
    ACHECK(rocket.size() >= 14000 && rocket.size() <= 14400 && near(peakOf(rocket), 0.1f));
    ACHECK(std::fabs(peakOf(mine.samples(0, SND_SHOTGUN)) - 0.1f) > 0.05f);   // (DEFAULT's shotgun is far louder than 0.1)
    ACHECK(near(peakOf(mine.samples(Audio::kPackWooden, SND_HIT_MED)), 0.1f));
    ACHECK(near(peakOf(mine.samples(Audio::kPackMetal, SND_SNIPER)), 1.0f));
    ACHECK(near(peakOf(mine.samples(0, SND_VOICE_GO)), 0.1f));
    ACHECK(std::fabs(peakOf(mine.samples(0, SND_VOICE_ONE)) - 0.1f) > 0.05f);
    // a kept DEFAULT still becomes the sound of the kits that had DEFAULT's, at its own level (and RETRO's, chiptuned)
    for (int p = 1; p < Audio::kPacks; p++) {
        if (p == Audio::kPackRetro) { ACHECK(mine.samples(p, SND_ROCKET) != plain.samples(p, SND_ROCKET)); continue; }
        if (plain.samples(p, SND_ROCKET) == plain.samples(0, SND_ROCKET)) ACHECK(mine.samples(p, SND_ROCKET) == rocket);
        else ACHECK(mine.samples(p, SND_ROCKET) == plain.samples(p, SND_ROCKET));
    }
    fs::remove_all(root, ec);
    overrides::useUserFolder(true);
    ACHECK(overrides::soundLevels().find("rocket") == std::string::npos);
}

// The shipped icon follows the icon override (tools/make_icon.py draws it from the override and records which)
static void testIconSource() {
    const fs::path src = srcDir();
    std::error_code ec;
    for (const char* ext : {".png", ".jpg", ".jpeg"}) {
        fs::path ov = src / (std::string("assets/overrides/images/icon") + ext);
        if (!fs::exists(ov, ec)) continue;
        std::string marker = slurp(src / "assets/quark.icon-source");
        bool ok = marker.find(sha256Hex(slurp(ov))) != std::string::npos;
        if (!ok) printf("  assets/quark.png isn't made from %s: run python3 tools/make_icon.py\n", ov.u8string().c_str());
        ACHECK(ok);
    }
}

// The export: every kind of asset, every file it lists written
static void testExport() {
    fs::path dir = fs::u8path(userDataPath("export_test"));
    ACHECK(exportAssets(dir.u8string().c_str()) == 0);
    std::string man = slurp(dir / "manifest.json");
    size_t n = 0, sounds = 0, voices = 0, models = 0, maps = 0, missing = 0;
    for (size_t at = 0; (at = man.find("\"file\":\"", at)) != std::string::npos; at++) {
        size_t e = man.find('"', at + 8);
        std::string f = man.substr(at + 8, e - at - 8);
        n++;
        sounds += f.rfind("sounds/", 0) == 0;
        voices += f.rfind("voices/", 0) == 0;
        models += f.rfind("models/", 0) == 0;
        maps += f.rfind("maps/", 0) == 0;
        std::error_code ec;
        if (!fs::exists(dir / fs::u8path(f), ec) || fs::file_size(dir / fs::u8path(f), ec) == 0) { missing++; printf("  missing %s\n", f.c_str()); }
    }
    size_t official = 0;
    for (const OfficialMapData* m = kOfficialMapData; m->name; m++) official++;
    ACHECK(sounds == (size_t)Audio::kPacks * SND_VOICE_THREE);
    ACHECK(voices == (size_t)kAnnouncerVoices * (SND_COUNT - SND_VOICE_THREE));
    ACHECK(models == 30 && maps == official && missing == 0);   // (5 players, 11 weapons, 7 projectiles, the crown, 6 props)
    ACHECK(man.find("\"fonts/Rubik.") != std::string::npos && man.find("\"fonts/WorkSans.") != std::string::npos && man.find("\"images/icon.") != std::string::npos);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// 64 sounds at once: the one dropped for a new one is the oldest sound effect, never an announcer line that just
// started. It used to drop voices_.front(), but the mixer's swap-and-pop moves the newest voice into a finished one's
// slot: a short sound ending put the line just played at the front, and the next sound cut it.
static void testVoiceSteal() {
    printf("audio: a full mixer drops the oldest effect, not a fresh announcer line\n");
    Audio a;
    a.startOffline();
    a.setAnnouncer(1);   // (loads the lines: the default voice isn't loaded until the game picks it)
    int line = -1, shortFx = -1, longFx = -1;
    for (int id = SND_VOICE_THREE; id < SND_COUNT; id++) if (a.samples(0, id).size() > (size_t)Audio::kRate / 2) { line = id; break; }
    for (int id = 0; id < SND_VOICE_THREE; id++) {
        size_t n = a.samples(0, id).size();
        if (n < 100) continue;
        if (n < (size_t)Audio::kRate / 5 && (shortFx < 0 || n < a.samples(0, shortFx).size())) shortFx = id;
        if (n > (size_t)Audio::kRate / 2 && (longFx < 0 || n > a.samples(0, longFx).size())) longFx = id;
    }
    ACHECK(line >= 0 && shortFx >= 0 && longFx >= 0);
    if (line < 0 || shortFx < 0 || longFx < 0) { a.stop(); return; }
    const int kVoices = 64;
    a.play(shortFx, 1.0f);                                   // slot 0: ends first
    for (int i = 0; i < kVoices - 2; i++) a.play(longFx, 1.0f);
    a.play(line, 1.0f);                                      // the newest: the last slot
    std::vector<int16_t> out(4096);
    for (size_t done = 0; done < a.samples(0, shortFx).size() + 2048; done += 512) a.mix(out.data(), 512);   // the short one ends
    ACHECK(a.voicesPlaying(shortFx) == 0 && a.voicesPlaying(line) == 1);
    a.play(longFx, 1.0f);                                    // full again...
    a.play(longFx, 1.0f);                                    // ...and one more: an effect makes room
    a.play(longFx, 1.0f);
    printf("  after a burst: line playing %d, effects %d\n", a.voicesPlaying(line), a.voicesPlaying(longFx));
    ACHECK(a.voicesPlaying(line) == 1 && a.voicesPlaying(longFx) == kVoices - 1);
    a.stop();
}

int assetTests() {
    g_afail = 0;
    testModelExport();
    testGltfLoader();
    testBoxModels();
    testDecoders();
    testFontAtlas();
    testUiFonts();
    testHitTiers();
    testVoiceSteal();
    testOverrides();
    testSoundLevels();
    testIconSource();
    testExport();
    return g_afail;
}
