// Builds the static world mesh and bakes its lighting into a lightmap atlas:
// soft sun shadows, sky occlusion and one bounce of indirect light, traced per texel.
// The result is cached on disk keyed by the map CRC so it's only computed once.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "../game/map.h"

struct WorldVertex {
    float pos[3];
    float normal[3];
    float uv[2];       // lightmap atlas coordinates
    float material;
    float edge[4];     // face-local u, v (meters) and face width, height: for outlines
};

struct BakedWorld {
    std::vector<WorldVertex> verts;
    int atlasW = 0, atlasH = 0;
    std::vector<uint8_t> texels; // RG8: R = direct sun, G = sky + bounce
    bool fromCache = false;
    bool cancelled = false;      // the bake was stopped (cancel): texels incomplete, nothing cached
};

// progress receives 0..1 (called from the baking thread pool's coordinator thread, i.e. the caller's). cancel,
// if given, stops the bake early when it turns true (out.cancelled; nothing is cached). Safe to run off the main
// thread on a private copy of the map (the client bakes a joined map on a worker thread: PERF-3).
void buildWorld(const Map& map, BakedWorld& out, const std::function<void(float)>& progress = nullptr,
                const std::atomic<bool>* cancel = nullptr);
// The GPU's largest texture side (GL_MAX_TEXTURE_SIZE, set by the renderer at start): the lightmap atlas is packed to
// fit it (PERF-13). Until set: 4096, what every GL 3.3 driver we run on offers.
void setLightmapMaxTextureSize(int px);
// VIDEO + AUDIO > MORE 6 (1.19.4): how many bakes the user's cache keeps (oldest go after the next bake), the threads a
// bake runs on (0: all but one core), the cache's bakes and their size, and emptying it (shipped bakes stay)
void setLightCacheLimit(int n);
void setBakeThreads(int n);
int lightCacheStats(uint64_t* bytes);
int clearLightCache();
// Sun visibility (0..1) of a point; used for lighting dynamic objects consistently with the bake.
float sunVisibility(const Map& map, const Vec3& p);

// Shipped bakes: packages carry the official maps' light caches in maps/lightcache/ (found like maps/), read when the
// user's own cache misses and never written, touched or evicted (a miss still bakes into the user's cache).
// quark --bake-maps <dir>: bakes every map embedded in the game (not your own maps) on every core into
// <dir>/<sha256>.qlb, the runtime cache's bytes; a valid file already there is kept, any other .qlb there is removed.
// 0 when every map was written. tools/pack_lightcache.py then zlib-compresses them for packages (.qlbz).
int bakeOfficialMaps(const std::string& dir);
// A map's light cache file name: <sha256 of its text>.qlm in the user's cache/, <sha256>.qlbz shipped (zlib; not .qlm
// also because installed launchers keep every *.qlm through updates as the player's: old bakes would pile up).
std::string lightCacheName(const Map& map, bool shipped = false);
// Writes map's light cache file to path, baked unless a valid one is there already (*kept: it was); false if it
// couldn't be written. The user's cache isn't read or written.
bool bakeLightCacheFile(const Map& map, const std::string& path, bool* kept = nullptr);
// Tests: the folder shipped bakes are read from (empty: findDataPath("maps/lightcache"), the default).
void setShippedLightCacheDir(const std::string& dir);
