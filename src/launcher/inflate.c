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
    // (an entry claiming 0 compressed bytes for a non-empty file: no stream at all, and an empty std::vector's data()
    // is null, which stb would offset: undefined behaviour, found by fuzz_qpak)
    if (!in || inLen <= 0 || !out || outLen <= 0) return -1;
    return stbi_zlib_decode_buffer(out, outLen, in, inLen);
}
