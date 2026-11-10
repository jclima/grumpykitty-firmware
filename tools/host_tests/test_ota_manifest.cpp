// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

#include "../../src/system/ota_manifest.h"
#include <cstdio>
#include <string>

using namespace ota_manifest;

int main() {
    unsigned checks = 0, failures = 0;
    auto expect = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
    };

    Version running{}, offered{};
    expect(parseVersion("0.1.4-grumpy", running), "release version");
    expect(parseVersion("0.1.10-grumpy", offered) && newer(offered, running), "numeric order");
    expect(!newer(running, running) && !newer(running, offered), "equal and downgrade");
    for (const char* text : {"0.1.4", "00.1.4-grumpy", "-1.1.4-grumpy", "0.1.+4-grumpy",
                             "0.1.10000-grumpy", "0.1.4-test", "0.1.4-grumpy/..", "0.1.4-grumpy "}) {
        expect(!parseVersion(text, offered), text);
    }

    const std::string hash = "0123456789abcdef" + std::string(48, '0');
    auto feed = [&](const char* schema, const char* device, const char* version,
                    const char* size, const std::string& digest) {
        return std::string("{\"schema\":") + schema + ",\"device\":\"" + device +
               "\",\"version\":\"" + version + "\",\"size\":" + size + ",\"sha256\":\"" + digest + "\"}";
    };
    Manifest result;
    auto read = [&](const std::string& json) { return parse(json.data(), json.size(), result); };
    expect(read(feed("1", "meowkit-s3", "0.1.5-grumpy", "7864320", hash)) &&
           result.size == SlotBytes && result.digest[0] == 0x01 && result.digest[7] == 0xef &&
           newer(result.numeric, running), "exact slot limit and binary digest");
    expect(!read(feed("true", "meowkit-s3", "0.1.5-grumpy", "1", hash)), "boolean schema");
    expect(!read(feed("1", "other-device", "0.1.5-grumpy", "1", hash)), "wrong device");
    expect(!read(feed("1", "meowkit-s3", "0.1.5-grumpy\\u0000/..", "1", hash)), "embedded null in version");
    expect(!read(feed("1", "meowkit-s3", "0.1.5-grumpy", "7864321", hash)), "oversized image");
    expect(!read(feed("1", "meowkit-s3", "0.1.5-grumpy", "0", hash)), "empty image");
    expect(!read(feed("1", "meowkit-s3", "0.1.5-grumpy", "\"1\"", hash)), "string size");
    expect(!read(feed("1", "meowkit-s3", "0.1.5-grumpy", "1", "A" + hash.substr(1))), "bad digest");
    expect(!read(feed("1", "meowkit-s3", "0.1.5-grumpy", "1", hash.substr(1))), "short digest");
    expect(!read(feed("1", "meowkit-s3", "0.1.5-grumpy", "1", hash).substr(1)), "malformed JSON");
    auto extra = feed("1", "meowkit-s3", "0.1.5-grumpy", "1", hash);
    extra.pop_back();
    extra += ",\"url\":\"https://example.invalid/fw.bin\"}";
    expect(!read(extra), "no remote URL override");
    expect(!read(std::string(MaxBytes + 1, ' ')), "bounded feed");

    std::printf("ota_manifest: %u checks, %u failed\n", checks, failures);
    return failures ? 1 : 0;
}
