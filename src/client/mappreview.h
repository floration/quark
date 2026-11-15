// A map's top-down preview (the map picker's picture; the Discord status's map picture, `quark_tests mappreviews`).
// No graphics: the game and the tests link it.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Map;
// W x H RGBA, rows bottom-up (the renderer's atlas). About a millisecond a map.
std::vector<uint8_t> makeMapPreview(const Map& m, int W, int H);
// An RGBA image (rows bottom-up, as makeMapPreview's) as an RGB PNG, top row first: deflate's stored blocks (no
// compression: tools/png_squeeze.py recompresses the published copies). Empty on a bad size.
std::string encodePreviewPng(const std::vector<uint8_t>& rgbaBottomUp, int W, int H);
