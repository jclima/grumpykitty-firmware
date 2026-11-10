// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

#pragma once

#include <cstdint>

namespace wifi_packet_monitor {

enum class Filter : uint8_t { All, Beacons, Probes, Disconnects };

struct Stats {
    uint32_t beacons;
    uint32_t probes;
    uint32_t disconnects;
    uint32_t other;
    uint32_t saved;
    uint32_t dropped;
    uint8_t channel;
};

// Call these from the launcher task. The driver callback owns no file handles.
bool start(uint8_t requestedChannel, Filter filter, bool save);
void tick();
void stop();
bool running();
Stats stats();
const char* message();
const char* savedPath();

} // namespace wifi_packet_monitor
