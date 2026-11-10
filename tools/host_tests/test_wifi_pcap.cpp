// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../src/app/app_12/wifi_pcap.h"

using namespace wifi_pcap;

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    ++g_checks;                                                            \
    if (!(cond)) {                                                         \
      ++g_failed;                                                          \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
    }                                                                      \
  } while (0)

static void testHeaders() {
  uint8_t file[kFileHeaderSize] = {};
  fileHeader(file);
  const uint8_t expectedFile[kFileHeaderSize] = {
      0xd4, 0xc3, 0xb2, 0xa1, 0x02, 0x00, 0x04, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x02, 0x00, 0x00, 0x69, 0x00, 0x00, 0x00,
  };
  CHECK(std::memcmp(file, expectedFile, sizeof file) == 0);

  uint8_t record[kRecordHeaderSize] = {};
  recordHeader(record, 3723004001ULL, 0x01020304u, 0xaabbccddu);
  const uint8_t expectedRecord[kRecordHeaderSize] = {
      0x8b, 0x0e, 0x00, 0x00,  // 3723 seconds
      0xa1, 0x0f, 0x00, 0x00,  // 4001 microseconds
      0x04, 0x03, 0x02, 0x01,
      0xdd, 0xcc, 0xbb, 0xaa,
  };
  CHECK(std::memcmp(record, expectedRecord, sizeof record) == 0);
}

static void frame(uint8_t* out, size_t len, uint8_t subtype) {
  std::memset(out, 0, len);
  out[0] = static_cast<uint8_t>(subtype << 4);
}

static void testClassification() {
  uint8_t data[40];
  frame(data, sizeof data, 8);
  CHECK(classify(data, 36) == FrameKind::Beacon);
  CHECK(classify(data, 35) == FrameKind::Invalid);
  frame(data, sizeof data, 4);
  CHECK(classify(data, 24) == FrameKind::Probe);
  frame(data, sizeof data, 5);
  CHECK(classify(data, 36) == FrameKind::Probe);
  CHECK(classify(data, 24) == FrameKind::Invalid);
  frame(data, sizeof data, 10);
  CHECK(classify(data, 26) == FrameKind::Disconnect);
  frame(data, sizeof data, 12);
  CHECK(classify(data, 25) == FrameKind::Invalid);
  CHECK(classify(data, 26) == FrameKind::Disconnect);
  frame(data, sizeof data, 0);
  CHECK(classify(data, 24) == FrameKind::Other);
  CHECK(classify(data, 23) == FrameKind::Invalid);
  CHECK(classify(nullptr, 24) == FrameKind::Invalid);

  // Data/control frames and non-zero protocol versions are invalid, not Other.
  frame(data, sizeof data, 8);
  data[0] = 0x08;  // type 2 (data)
  CHECK(classify(data, 36) == FrameKind::Invalid);
  data[0] = 0x01;  // management type, version 1
  CHECK(classify(data, 24) == FrameKind::Invalid);
}

int main() {
  testHeaders();
  testClassification();
  std::printf("wifi_pcap: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
