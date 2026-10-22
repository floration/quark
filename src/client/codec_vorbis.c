/* OGG Vorbis decoding for the asset overrides (docs/ASSETS.md): stb_vorbis, vendored unmodified in third_party/stb.
   (Each third-party library builds in a file of its own: their internal macros collide in one.) */
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb/stb_vorbis.c"
