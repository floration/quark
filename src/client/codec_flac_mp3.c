/* FLAC and MP3 decoding for the asset overrides (docs/ASSETS.md): dr_flac and dr_mp3, vendored unmodified in
   third_party/dr_libs. */
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#include "dr_libs/dr_flac.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_libs/dr_mp3.h"
