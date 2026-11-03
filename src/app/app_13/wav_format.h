// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/**
 * @file wav_format.h
 * @brief Minimal PCM WAV header writer and bounds-checked parser.
 *
 * Pure C++ (no Arduino/ESP dependencies) so it can be host-compiled.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wavfmt {

constexpr size_t kHeaderSize = 44;

inline void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline void put32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
inline uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t get32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** Write a canonical 44-byte PCM header. Also used to patch sizes (rewrite the
 *  whole header with the current data size). dataBytes is clamped so the RIFF
 *  size cannot overflow 32 bits. */
inline void writeHeader(uint8_t out[kHeaderSize], uint32_t rate, uint16_t channels,
                        uint16_t bits, uint32_t dataBytes)
{
    if (dataBytes > 0xFFFFFFFFu - 36u) dataBytes = 0xFFFFFFFFu - 36u;
    const uint16_t block = (uint16_t)(channels * (bits / 8));
    memcpy(out, "RIFF", 4);
    put32(out + 4, 36u + dataBytes);
    memcpy(out + 8, "WAVE", 4);
    memcpy(out + 12, "fmt ", 4);
    put32(out + 16, 16);
    put16(out + 20, 1);               /* PCM */
    put16(out + 22, channels);
    put32(out + 24, rate);
    put32(out + 28, rate * block);
    put16(out + 32, block);
    put16(out + 34, bits);
    memcpy(out + 36, "data", 4);
    put32(out + 40, dataBytes);
}

struct Info {
    uint32_t rate;
    uint16_t channels;
    uint16_t bits;
    uint32_t dataOffset;   /* byte offset of first sample in the file */
    uint32_t dataBytes;    /* clamped to the file size, whole frames only */
};

/** Parse the start of a file. `buf`/`len` is the first `len` bytes (any length;
 *  every access is bounds checked), `fileSize` the full file size. Accepts only
 *  PCM, 16-bit, 1-2 channels, 8000-48000 Hz, with the data chunk starting
 *  inside `buf`. Returns false on anything else. */
inline bool parse(const uint8_t* buf, size_t len, uint64_t fileSize, Info& info)
{
    if (!buf || len < 12 || fileSize < 12) return false;
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) return false;

    bool haveFmt = false;
    uint16_t ch = 0, bits = 0;
    uint32_t rate = 0;
    size_t pos = 12;
    while (pos + 8 <= len) {
        const uint8_t* c = buf + pos;
        const uint32_t sz = get32(c + 4);
        const size_t body = pos + 8;
        if (memcmp(c, "fmt ", 4) == 0) {
            if (sz < 16 || body + 16 > len) return false;
            const uint16_t tag = get16(buf + body);
            ch = get16(buf + body + 2);
            rate = get32(buf + body + 4);
            const uint16_t block = get16(buf + body + 12);
            bits = get16(buf + body + 14);
            if (tag != 1 || bits != 16 || ch < 1 || ch > 2) return false;
            if (rate < 8000 || rate > 48000) return false;
            if (block != (uint16_t)(ch * 2)) return false;
            haveFmt = true;
        } else if (memcmp(c, "data", 4) == 0) {
            if (!haveFmt) return false;
            if ((uint64_t)body > fileSize) return false;
            uint64_t avail = fileSize - body;
            /* Size 0 / 0xFFFFFFFF = unpatched or streaming header: data runs to EOF. */
            uint64_t n = (sz == 0 || sz == 0xFFFFFFFFu || sz > avail) ? avail : sz;
            n -= n % (uint64_t)(ch * 2);
            info.rate = rate;
            info.channels = ch;
            info.bits = bits;
            info.dataOffset = (uint32_t)body;
            info.dataBytes = (uint32_t)n;
            return n > 0;
        }
        /* Next chunk: chunks are word aligned. Guard against overflow. */
        const uint64_t next = (uint64_t)body + sz + (sz & 1u);
        if (next > len || next <= pos) return false;
        pos = (size_t)next;
    }
    return false;
}

} // namespace wavfmt
