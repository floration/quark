// Sound and image decoding for the asset overrides (overrides.h). WAV is read here; the rest by the vendored
// single-file libraries compiled in codecs.c.
#include "overrides.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"
#include "dr_libs/dr_flac.h"
#include "dr_libs/dr_mp3.h"
#include "stb/stb_image.h"

static bool decodeWav(const uint8_t* d, size_t size, std::vector<float>& out, int& rate) {
    auto u16 = [&](size_t o) { return (uint32_t)(d[o] | d[o + 1] << 8); };
    auto u32 = [&](size_t o) { return (uint32_t)(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (uint32_t)d[o + 3] << 24); };
    if (size < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return false;
    int fmt = 0, ch = 0, bits = 0; size_t data = 0, dataLen = 0;
    for (size_t o = 12; o + 8 <= size;) {
        uint32_t len = u32(o + 4);
        if (!memcmp(d + o, "fmt ", 4) && o + 24 <= size) {
            fmt = (int)u16(o + 8); ch = (int)u16(o + 10); rate = (int)u32(o + 12); bits = (int)u16(o + 22);
            if (fmt == 0xFFFE && o + 34 <= size) fmt = (int)u16(o + 32);   // WAVE_FORMAT_EXTENSIBLE: the sub-format
        } else if (!memcmp(d + o, "data", 4)) { data = o + 8; dataLen = std::min<size_t>(len, size - data); break; }
        if ((size_t)len > size) break;
        o += 8 + (size_t)len + (len & 1);
    }
    if (!data || ch < 1 || ch > 8 || rate < 4000 || rate > 192000 || !((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32))) return false;
    size_t bps = (size_t)bits / 8, frames = dataLen / (bps * ch);
    out.assign(frames, 0.0f);
    for (size_t i = 0; i < frames; i++) {
        float sum = 0;
        for (int c = 0; c < ch; c++) {
            const uint8_t* p = d + data + (i * ch + c) * bps;
            float v = 0;
            if (fmt == 3) { float x; memcpy(&x, p, 4); v = x; }
            else if (bits == 8) v = (p[0] - 128) / 128.0f;
            else if (bits == 16) v = (int16_t)(p[0] | p[1] << 8) / 32768.0f;
            else if (bits == 24) v = (int32_t)((uint32_t)(p[0] << 8 | p[1] << 16 | (uint32_t)p[2] << 24)) / 2147483648.0f;
            else v = (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24) / 2147483648.0f;
            sum += v;
        }
        out[i] = sum / ch;
    }
    return true;
}

static void toMono(const float* x, size_t frames, int ch, std::vector<float>& out) {
    out.assign(frames, 0.0f);
    for (size_t i = 0; i < frames; i++) {
        float s = 0;
        for (int c = 0; c < ch; c++) s += x[i * ch + c];
        out[i] = s / ch;
    }
}

bool decodeSound(const uint8_t* d, size_t size, std::vector<float>& mono, int& rate, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (size > 64u << 20) return fail("over 64 MB");
    if (size >= 12 && !memcmp(d, "RIFF", 4)) return decodeWav(d, size, mono, rate) ? true : fail("unsupported WAV (PCM 8 / 16 / 24 / 32-bit or float)");
    if (size >= 4 && !memcmp(d, "OggS", 4)) {
        int ch = 0, sr = 0;
        short* pcm = nullptr;
        int frames = stb_vorbis_decode_memory(d, (int)size, &ch, &sr, &pcm);
        if (frames <= 0 || ch < 1 || !pcm) { free(pcm); return fail("unreadable OGG (Vorbis only)"); }
        mono.assign((size_t)frames, 0.0f);
        for (int i = 0; i < frames; i++) { int s = 0; for (int c = 0; c < ch; c++) s += pcm[(size_t)i * ch + c]; mono[i] = s / (32768.0f * ch); }
        free(pcm);
        rate = sr;
        return rate >= 4000 && rate <= 192000 ? true : fail("odd sample rate");
    }
    if (size >= 4 && !memcmp(d, "fLaC", 4)) {
        unsigned int ch = 0, sr = 0; drflac_uint64 frames = 0;
        float* pcm = drflac_open_memory_and_read_pcm_frames_f32(d, size, &ch, &sr, &frames, nullptr);
        if (!pcm || !ch || !frames) { drflac_free(pcm, nullptr); return fail("unreadable FLAC"); }
        toMono(pcm, (size_t)frames, (int)ch, mono);
        drflac_free(pcm, nullptr);
        rate = (int)sr;
        return rate >= 4000 && rate <= 192000 ? true : fail("odd sample rate");
    }
    {   // MP3 (ID3 tag or a frame sync first)
        drmp3_config cfg{};
        drmp3_uint64 frames = 0;
        float* pcm = drmp3_open_memory_and_read_pcm_frames_f32(d, size, &cfg, &frames, nullptr);
        if (pcm && cfg.channels && frames) {
            toMono(pcm, (size_t)frames, (int)cfg.channels, mono);
            drmp3_free(pcm, nullptr);
            rate = (int)cfg.sampleRate;
            return rate >= 4000 && rate <= 192000 ? true : fail("odd sample rate");
        }
        drmp3_free(pcm, nullptr);
    }
    return fail("not a WAV, OGG, FLAC or MP3 file");
}

bool decodeImage(const uint8_t* data, size_t size, int& w, int& h, std::vector<uint8_t>& rgba) {
    int n = 0;
    if (size > (size_t)1 << 30) return false;
    unsigned char* p = stbi_load_from_memory(data, (int)size, &w, &h, &n, 4);
    if (!p) return false;
    rgba.assign(p, p + (size_t)w * h * 4);
    stbi_image_free(p);
    return true;
}
