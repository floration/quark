// Builds the static world mesh and bakes its lighting into a lightmap atlas:
// soft sun shadows, sky occlusion and one bounce of indirect light, traced per texel.
// The result is cached on disk keyed by the map CRC so it's only computed once.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
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
// Sun visibility (0..1) of a point; used for lighting dynamic objects consistently with the bake.
float sunVisibility(const Map& map, const Vec3& p);
