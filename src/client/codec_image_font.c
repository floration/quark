/* PNG / JPEG decoding and TrueType outlines / distance fields for the asset overrides (docs/ASSETS.md): stb_image
   and stb_truetype, vendored unmodified in third_party/stb. Only override files go through them, once at startup. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb/stb_image.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"
