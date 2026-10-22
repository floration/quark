// A tiny glTF 2.0 writer and loader (no dependencies) for the asset export and the owner's model overrides.
// Writer: one mesh, one primitive per material, POSITION / NORMAL / COLOR_0 (float) + uint32 indices, the buffer
// embedded as a base64 data: URI (a single .gltf file).
// Loader: .gltf (JSON; buffers as data: URIs or files found through `resolve`) or .glb; the triangles (indexed or
// not, lists / strips / fans) of every mesh in the default scene with their node transforms (matrix or TRS,
// nested); POSITION, NORMAL (flat normals when missing), COLOR_0 (float / normalised u8 / u16, vec3 / vec4) times
// the material's baseColorFactor. Textures are ignored. Materials are kept by name (tint slots: models.h).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "models.h"

std::string writeGltf(const MeshData& mesh, const std::string& name);
using GltfResolver = std::function<bool(const std::string& uri, std::vector<uint8_t>& out)>;
bool loadGltf(const uint8_t* data, size_t size, const GltfResolver& resolve, MeshData& out, std::string* err = nullptr);

std::string base64Encode(const uint8_t* d, size_t n);
bool base64Decode(const std::string& s, std::vector<uint8_t>& out);
