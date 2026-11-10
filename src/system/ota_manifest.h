// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima
#pragma once

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ota_manifest {

constexpr uint32_t SlotBytes = 0x780000;
constexpr size_t MaxBytes = 1024;

struct Version {
    unsigned major, minor, patch;
};

inline bool parseVersion(const char* text, Version& version) {
    if (!text) return false;
    unsigned values[3]{};
    for (unsigned i = 0; i < 3; ++i) {
        if (*text < '0' || *text > '9') return false;
        if (*text == '0' && text[1] >= '0' && text[1] <= '9') return false;
        unsigned digits = 0;
        while (*text >= '0' && *text <= '9') {
            if (++digits > 4) return false;
            values[i] = values[i] * 10 + unsigned(*text++ - '0');
        }
        if (i < 2 && *text++ != '.') return false;
    }
    if (strcmp(text, "-grumpy") != 0) return false;
    version = {values[0], values[1], values[2]};
    return true;
}

inline bool newer(const Version& offered, const Version& running) {
    if (offered.major != running.major) return offered.major > running.major;
    if (offered.minor != running.minor) return offered.minor > running.minor;
    return offered.patch > running.patch;
}

struct Manifest {
    char version[32]{};
    Version numeric{};
    uint32_t size = 0;
    uint8_t digest[32]{};
};

inline int nibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

inline bool parse(const char* json, size_t length, Manifest& output) {
    if (!json || length == 0 || length > MaxBytes) return false;
    JsonDocument doc;
    if (deserializeJson(doc, json, length, DeserializationOption::NestingLimit(2)) ||
        !doc.is<JsonObject>() || doc.size() != 5 || !doc["schema"].is<unsigned>() ||
        doc["schema"].as<unsigned>() != 1 || !doc["size"].is<uint32_t>()) return false;

    const auto device = doc["device"].as<JsonString>();
    const auto version = doc["version"].as<JsonString>();
    const auto hash = doc["sha256"].as<JsonString>();
    constexpr char target[] = "meowkit-s3";
    Manifest result;
    result.size = doc["size"].as<uint32_t>();
    if (device.isNull() || device.size() != sizeof(target) - 1 ||
        memcmp(device.c_str(), target, sizeof(target) - 1) != 0 ||
        version.isNull() || version.size() >= sizeof(result.version) ||
        strlen(version.c_str()) != version.size() ||
        !parseVersion(version.c_str(), result.numeric) ||
        hash.isNull() || hash.size() != 64 || result.size == 0 || result.size > SlotBytes) {
        return false;
    }
    for (size_t i = 0; i < sizeof(result.digest); ++i) {
        const int high = nibble(hash.c_str()[2 * i]);
        const int low = nibble(hash.c_str()[2 * i + 1]);
        if (high < 0 || low < 0) return false;
        result.digest[i] = uint8_t(high * 16 + low);
    }
    memcpy(result.version, version.c_str(), version.size());
    output = result;
    return true;
}
} // namespace ota_manifest
