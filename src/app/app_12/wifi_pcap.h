// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/**
 * @file wifi_pcap.h
 * @brief Small, dependency-free helpers for classic PCAP Wi-Fi captures.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace wifi_pcap {

constexpr size_t kFileHeaderSize = 24;
constexpr size_t kRecordHeaderSize = 16;
constexpr uint32_t kSnaplen = 512;

enum class FrameKind : uint8_t { Beacon, Probe, Disconnect, Other, Invalid };

namespace detail {

inline void putLe32(uint8_t* out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
  out[2] = static_cast<uint8_t>(value >> 16);
  out[3] = static_cast<uint8_t>(value >> 24);
}

}  // namespace detail

/** Classify a complete-enough 802.11 management frame (caller strips FCS). */
inline FrameKind classify(const uint8_t* frame, size_t len) {
  if (frame == nullptr || len < 24) return FrameKind::Invalid;

  // Frame Control is little-endian: version must be 0 and type must be management.
  const uint16_t fc = static_cast<uint16_t>(frame[0]) |
                      (static_cast<uint16_t>(frame[1]) << 8);
  if ((fc & 0x0003u) != 0 || (fc & 0x000cu) != 0) return FrameKind::Invalid;

  const uint8_t subtype = static_cast<uint8_t>((fc >> 4) & 0x0f);
  switch (subtype) {
    case 4:  // Probe request
      return len >= 24 ? FrameKind::Probe : FrameKind::Invalid;
    case 5:  // Probe response
      return len >= 36 ? FrameKind::Probe : FrameKind::Invalid;
    case 8:  // Beacon
      return len >= 36 ? FrameKind::Beacon : FrameKind::Invalid;
    case 10:  // Disassociation
    case 12:  // Deauthentication
      return len >= 26 ? FrameKind::Disconnect : FrameKind::Invalid;
    default:
      return FrameKind::Other;
  }
}

/** Fill a little-endian classic PCAP v2.4 microsecond file header. */
inline void fileHeader(uint8_t (&out)[kFileHeaderSize]) {
  // magic, version 2.4, thiszone, sigfigs, snaplen, LINKTYPE_IEEE802_11 (105)
  const uint8_t header[kFileHeaderSize] = {
      0xd4, 0xc3, 0xb2, 0xa1,  // microsecond-resolution magic
      0x02, 0x00, 0x04, 0x00,  // version 2.4
      0x00, 0x00, 0x00, 0x00,  // GMT offset
      0x00, 0x00, 0x00, 0x00,  // timestamp accuracy
      0x00, 0x02, 0x00, 0x00,  // snaplen 512
      0x69, 0x00, 0x00, 0x00,  // LINKTYPE_IEEE802_11
  };
  for (size_t i = 0; i < kFileHeaderSize; ++i) out[i] = header[i];
}

/** Fill a little-endian classic PCAP packet record header. */
inline void recordHeader(uint8_t (&out)[kRecordHeaderSize], uint64_t timeUs,
                         uint32_t includedLength, uint32_t originalLength) {
  detail::putLe32(out + 0, static_cast<uint32_t>(timeUs / 1000000ULL));
  detail::putLe32(out + 4, static_cast<uint32_t>(timeUs % 1000000ULL));
  detail::putLe32(out + 8, includedLength);
  detail::putLe32(out + 12, originalLength);
}

}  // namespace wifi_pcap
