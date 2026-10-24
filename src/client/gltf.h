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

// boxes: also written as the mesh's extras.quark (a box model file: docs/ASSETS.md "Box models")
std::string writeGltf(const MeshData& mesh, const std::string& name, const ModelParts* boxes = nullptr);
using GltfResolver = std::function<bool(const std::string& uri, std::vector<uint8_t>& out)>;
// boxes (optional): filled from the first mesh's extras.quark when it is a box model (version 2), else left empty;
// a box list that fails the checks (parseBoxModel) fails the load
bool loadGltf(const uint8_t* data, size_t size, const GltfResolver& resolve, MeshData& out, std::string* err = nullptr, ModelParts* boxes = nullptr);
// extras.quark of a box model as JSON ({"version":2,"boxes":[...]}), and back (with the checks: finite numbers, at
// most kMaxOverrideBoxes boxes, sizes and colours in range, known slots); false + err on anything wrong
std::string boxModelJson(const ModelParts& boxes);
bool parseBoxModel(const std::string& json, ModelParts& out, std::string* err = nullptr);

// Every model's overrides (models.h ModelOverrides; docs/ASSETS.md): each model's file from the shipped overrides and
// from yours, loaded and checked (a bad or too big file is logged and left out). Nothing to do when there are none.
void loadModelOverrides(ModelOverrides& out);

std::string base64Encode(const uint8_t* d, size_t n);
bool base64Decode(const std::string& s, std::vector<uint8_t>& out);
