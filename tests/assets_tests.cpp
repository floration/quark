// Assets: the asset export (`quark_tests exportassets <dir>`: every default asset as a file, for the Asset Studio,
// tools/studio/build.py) and the owner's overrides (docs/ASSETS.md): models <-> glTF, the decoders, the font atlas,
// override lookup and the audio replacing its sounds.
#include <algorithm>
#include <cmath>
#include <cstdio>
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
#include "../src/common/sha256.h"
#include "../src/common/version.h"
#include "../src/game/map.h"
#include "official_map_data.h"   // (generated: every shipped map's text, the way the game embeds it)

namespace fs = std::filesystem;

static int g_afail = 0;
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
    static const char* d[SND_VOICE_THREE] = {"you hit someone (the hitsound) - very short, < 0.2 s", "you killed someone - < 0.6 s",
        "you took damage - < 0.3 s", "a footstep (yours and others', in 3D) - very short, < 0.15 s", "a jump - < 0.2 s",
        "landing from a fall - < 0.3 s", "rifle shot - < 0.3 s (automatic: many per second)", "rocket launcher fired - < 0.7 s",
        "knife swing / throw - < 0.35 s", "sniper shot - < 1 s", "laser (lightning gun), repeats while firing - very short, < 0.1 s",
        "shotgun blast - < 0.6 s", "sticky bomb launched - < 0.4 s", "crossbow bolt fired - < 0.5 s", "bubble gun shot - < 0.3 s",
        "syringe used / thrown - < 0.4 s", "a rocket / sticky explodes (3D) - < 1 s", "a sticky bomb sticks to something - < 0.2 s",
        "a crossbow bolt hits - < 0.3 s", "dash - < 0.4 s", "grappling hook fired - < 0.3 s", "grappling hook catches - < 0.3 s",
        "you died - < 0.7 s", "someone else's killstreak - < 0.6 s", "someone typing in chat (one key click) - very short, < 0.06 s",
        "race: you passed a checkpoint - < 0.35 s", "race: you crossed the finish - < 0.8 s"};
    return id >= 0 && id < SND_VOICE_THREE ? d[id] : "";
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
            const ModelPalette& pal = referencePalette(kind);
            if (kind == MODEL_PLAYER) playerModelParts(i, 1.0f, pal, parts);
            else if (kind == MODEL_WEAPON) viewmodelParts(i, pal, parts);
            else if (kind == MODEL_PROJECTILE) projectileParts(i, pal, 1.0f, 1.0f, 0.0f, parts);
            else crownParts(pal, parts);
            MeshData mesh;
            partsToMesh(parts, mesh);
            std::string path = modelPath(kind, i);
            add({"model/" + path.substr(7), "Models", "model", modelName(kind, i), path + ".gltf", modelDesc(kind), modelKindName(kind)}, writeGltf(mesh, modelName(kind, i)));
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
    const fs::path src = QUARK_SOURCE_DIR;
    if (overrides::find("fonts/WorkSans", {".ttf", ".otf"}, ov, &matched))
        add({"font/worksans", "Fonts", "font", "WORK SANS", "fonts/WorkSans" + matched.substr(matched.rfind('.')),
             "the game's text (menus, HUD, launcher), drawn bold", ""}, std::string(ov.begin(), ov.end()));
    else add({"font/worksans", "Fonts", "font", "WORK SANS", "fonts/WorkSans.ttf",
              "the game's text (menus, HUD, launcher), drawn bold; Work Sans Project Authors, SIL OFL 1.1", ""}, slurp(src / "assets/fonts/WorkSans.ttf"));
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
            const ModelPalette& pal = referencePalette(kind);
            if (kind == MODEL_PLAYER) playerModelParts(i, 1.0f, pal, parts);
            else if (kind == MODEL_WEAPON) viewmodelParts(i, pal, parts);
            else if (kind == MODEL_PROJECTILE) projectileParts(i, pal, 1.0f, 1.0f, 0.0f, parts);
            else crownParts(pal, parts);
            ACHECK(!parts.empty());
            MeshData a, b;
            partsToMesh(parts, a);
            ACHECK(a.triangleCount() == parts.size() * 12);
            std::string g = writeGltf(a, modelName(kind, i));
            std::string err;
            bool ok = loadGltf((const uint8_t*)g.data(), g.size(), nullptr, b, &err);
            if (!ok) printf("  %s: %s\n", modelPath(kind, i).c_str(), err.c_str());
            ACHECK(ok);
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
    ACHECK(models == 5 + 10 + 7 + 1);
    // the default parts are what drawWorld draws: a known box, the standard model's chest, team coloured
    ModelPalette pal;
    pal.c[TINT_TEAM] = {0.2f, 0.4f, 0.8f};
    playerModelParts(0, 1.0f, pal, parts);
    ACHECK(parts.size() == 4 && std::fabs(parts[1].c.y - 1.15f) < 1e-6f && length(parts[1].color - pal.c[TINT_TEAM]) < 1e-6f && parts[3].visOnly);
    ACHECK(tintSlotFromName("Team_Armour.001") == TINT_TEAM && tintSlotFromName("GLOW") == TINT_GLOW && tintSlotFromName("skin") == TINT_NONE);
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
    const fs::path data = QUARK_TEST_DATA;
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
    std::string png = slurp(fs::path(QUARK_SOURCE_DIR) / "assets/quark.png");
    int iw = 0, ih = 0;
    std::vector<uint8_t> rgba;
    ACHECK(decodeImage((const uint8_t*)png.data(), png.size(), iw, ih, rgba) && iw == 256 && ih == 256 && rgba.size() == 256u * 256 * 4);
}

static void testFontAtlas() {
    std::string ttf = slurp(fs::path(QUARK_SOURCE_DIR) / "assets/fonts/WorkSans.ttf");
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
    ACHECK(spit(root / "voices/male/GO.ogg", slurp(fs::path(QUARK_TEST_DATA) / "tone.ogg")));
    ACHECK(spit(root / "models/player/HEAVY.gltf", "this is not glTF"));
    overrides::useUserFolder(true);   // (re-read the folder)
    ACHECK(overrides::any());
    std::vector<uint8_t> b;
    std::string matched, where;
    ACHECK(overrides::find("sounds/default/HIT", {".ogg", ".wav"}, b, &matched, &where) && where == "user" && matched == "sounds/default/hit.wav");
    ACHECK(!overrides::find("sounds/default/KILL", {".wav"}, b));
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
    ACHECK(mine.samples(0, SND_VOICE_GO) != plain.samples(0, SND_VOICE_GO));
    ACHECK(mine.samples(0, SND_VOICE_ONE) == plain.samples(0, SND_VOICE_ONE));
    fs::remove_all(root, ec);
    overrides::useUserFolder(true);
}

// The shipped icon follows the icon override (tools/make_icon.py draws it from the override and records which)
static void testIconSource() {
    const fs::path src = QUARK_SOURCE_DIR;
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
    ACHECK(models == 23 && maps == official && missing == 0);
    ACHECK(man.find("\"fonts/WorkSans.") != std::string::npos && man.find("\"images/icon.") != std::string::npos);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

int assetTests() {
    g_afail = 0;
    testModelExport();
    testGltfLoader();
    testDecoders();
    testFontAtlas();
    testOverrides();
    testIconSource();
    testExport();
    return g_afail;
}
