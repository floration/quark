// zlib streams for compressed packages (.qpakz, qpak.h): stb_image's decoder, compiled here with its symbols static
// (STB_IMAGE_STATIC), so it can't clash with the game's own copy (src/client/codec_image_font.c) when both are linked.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG   // (the zlib decoder is part of the PNG one; no other format is built)
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wunused-function"   // (the image API is unused here: only the zlib decoder is)
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "../../third_party/stb/stb_image.h"
#include "inflate.h"

int qpakInflate(const char* in, int inLen, char* out, int outLen) {
    return stbi_zlib_decode_buffer(out, outLen, in, inLen);
}
