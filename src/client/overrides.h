// The owner's asset overrides (docs/ASSETS.md). A file at an asset's export path replaces that default asset:
//   - shipped: assets/overrides/<path> in the repository, embedded in the game at build time
//     (tools/embed_overrides.py; maps go through tools/make_maps.py / embed_maps.py instead)
//   - yours:   <user data folder>/overrides/<path> (userDataPath("overrides")), read at startup; wins over shipped
// Paths are the export's ("sounds/default/ROCKET.wav", "models/player/HEAVY.gltf", "fonts/WorkSans.ttf", ...),
// matched without regard to case, and any of the extensions the loader accepts may stand in for the export's own
// (an upload keeps its extension: ROCKET.ogg replaces ROCKET.wav).
// Nothing is read when there are no overrides: any() is false and every loader returns at once.
#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace overrides {
bool any();                       // is there any override at all (shipped or yours)?
// Where find / readRelative look: yours then the shipped ones (what the game uses), or only one of the two (ranked
// draws only the shipped models: everyone has those)
enum From { FROM_ANY, FROM_SHIPPED, FROM_USER };
// The override for an asset: base = its path without the extension ("sounds/default/ROCKET"), tried with each of
// exts (".wav", ...). Fills out with the file's bytes; where = "user" or "shipped" and the matched path.
bool find(const std::string& base, std::initializer_list<const char*> exts, std::vector<uint8_t>& out,
          std::string* matched = nullptr, std::string* where = nullptr, From from = FROM_ANY);
// Reads a file next to an override (a .gltf's separate buffer): path relative to the overrides root.
bool readRelative(const std::string& path, std::vector<uint8_t>& out, From from = FROM_ANY);
// sound-levels.txt at the overrides root: the shipped file's text, then yours (your lines add to the shipped ones);
// empty without either. Each line names a sound / voice override that keeps its own level (audio.cpp).
std::string soundLevels();
// The overrides directly in a folder ("voices/male/quips") with one of exts: their names, lower case without the
// extension, each once (yours and the shipped ones), sorted. find(dir + "/" + name, exts) reads one.
std::vector<std::string> listFolder(const std::string& dir, std::initializer_list<const char*> exts);
std::vector<std::string> list();  // every override (lower-case paths, "shipped:" / "user:" prefixed); for logs and tests
void useUserFolder(bool on);      // off: shipped overrides only (the asset export); also re-reads your folder (tests)
}

// Decoders for override files (third_party/: stb_vorbis, dr_flac, dr_mp3, stb_image, stb_truetype).
// Sound: WAV (PCM 8/16/24/32, float), OGG Vorbis, FLAC, MP3 -> mono floats at its own rate.
bool decodeSound(const uint8_t* data, size_t size, std::vector<float>& mono, int& rate, std::string* err = nullptr);
// Image: PNG / JPEG -> 8-bit RGBA, rows top to bottom.
bool decodeImage(const uint8_t* data, size_t size, int& w, int& h, std::vector<uint8_t>& rgba);
