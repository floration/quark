// zlib (RFC 1950) decoding for compressed packages: inflate.c.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// Decodes `in` into `out`; the number of bytes written, or -1 when the stream is bad or doesn't fit.
int qpakInflate(const char* in, int inLen, char* out, int outLen);
#ifdef __cplusplus
}
#endif
