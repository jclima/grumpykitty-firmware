// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

#include "wifi_packet_monitor.h"
#include "wifi_pcap.h"
#include "../app_common/sd_guard.h"
#include "../../system/settings_bridge.h"
#include "../../ui/ui_wifi_bridge.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
using wifi_packet_monitor::Filter;
using wifi_packet_monitor::Stats;
using wifi_pcap::FrameKind;

constexpr size_t MaxFileBytes = 16 * 1024 * 1024;
constexpr size_t QueueSize = 32;
constexpr size_t WriteBatch = 8;
constexpr uint32_t ChannelCheckMs = 250;

struct Packet {
    uint64_t timeUs;
    uint16_t length;
    uint16_t originalLength;
    uint8_t bytes[wifi_pcap::kSnaplen];
};

// Only this block is shared with the Wi-Fi driver task. Closing admission
// under the same lock makes the queue finite before stop drains it.
portMUX_TYPE rxLock = portMUX_INITIALIZER_UNLOCKED;
struct Receiver {
    Packet packets[QueueSize];
    size_t head;
    size_t tail;
    bool accepting;
    bool save;
    Filter filter;
    Stats stats;
    uint64_t clockBaseUs;
    int64_t monoBaseUs;
} rx;

// Radio, SD and session operations run only on the launcher task.
struct RadioState {
    wifi_promiscuous_filter_t filter{};
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    uint8_t channel = 0;
    bool enabledSta = false;
    bool changedChannel = false;
    bool changedFilter = false;
    bool installedCallback = false;
    bool enabledMonitor = false;
    bool wasAssociated = false;
} radio;

FILE* capture = nullptr;
size_t fileBytes = 0;
char partialPath[48] = {};
char finalPath[40] = {};
char publishedPath[40] = {};
const char* statusText = "Ready";
bool active = false;
uint32_t lastChannelCheck = 0;

bool matches(Filter filter, FrameKind kind) {
    return filter == Filter::All ||
           (filter == Filter::Beacons && kind == FrameKind::Beacon) ||
           (filter == Filter::Probes && kind == FrameKind::Probe) ||
           (filter == Filter::Disconnects && kind == FrameKind::Disconnect);
}

void receive(void* buffer, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT || !buffer) return;
    const auto& packet = *static_cast<const wifi_promiscuous_pkt_t*>(buffer);
    if (packet.rx_ctrl.rx_state != 0 || packet.rx_ctrl.sig_len < 4) return;
    // ESP-IDF reports the FCS in sig_len. LINKTYPE_IEEE802_11 here has no FCS.
    const auto length = uint16_t(packet.rx_ctrl.sig_len - 4);
    const auto kind = wifi_pcap::classify(packet.payload, length);
    if (kind == FrameKind::Invalid) return;

    portENTER_CRITICAL(&rxLock);
    if (rx.accepting && packet.rx_ctrl.channel == rx.stats.channel) {
        switch (kind) {
        case FrameKind::Beacon: ++rx.stats.beacons; break;
        case FrameKind::Probe: ++rx.stats.probes; break;
        case FrameKind::Disconnect: ++rx.stats.disconnects; break;
        default: ++rx.stats.other; break;
        }
        if (rx.save && matches(rx.filter, kind)) {
            const size_t next = (rx.head + 1) % QueueSize;
            if (next == rx.tail) {
                ++rx.stats.dropped;
            } else {
                auto& slot = rx.packets[rx.head];
                slot.originalLength = length;
                slot.length = std::min<uint16_t>(length, wifi_pcap::kSnaplen);
                slot.timeUs = rx.clockBaseUs + uint64_t(esp_timer_get_time() - rx.monoBaseUs);
                memcpy(slot.bytes, packet.payload, slot.length);
                rx.head = next;
            }
        }
    }
    portEXIT_CRITICAL(&rxLock);
}

bool pop(Packet& packet) {
    portENTER_CRITICAL(&rxLock);
    const bool ready = rx.tail != rx.head;
    if (ready) {
        packet = rx.packets[rx.tail];
        rx.tail = (rx.tail + 1) % QueueSize;
    }
    portEXIT_CRITICAL(&rxLock);
    return ready;
}

bool driverOk(esp_err_t err, const char* step) {
    if (err == ESP_OK) return true;
    Serial.printf("[packet monitor] %s: %s\n", step, esp_err_to_name(err));
    return false;
}

bool isAssociated() {
    wifi_ap_record_t ap{};
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

bool disableReceiver() {
    portENTER_CRITICAL(&rxLock);
    rx.accepting = false;
    portEXIT_CRITICAL(&rxLock);

    bool ok = true;
    if (radio.enabledMonitor && !driverOk(esp_wifi_set_promiscuous(false), "disable monitor")) ok = false;
    if (radio.installedCallback && !driverOk(esp_wifi_set_promiscuous_rx_cb(nullptr), "detach RX")) ok = false;
    return ok;
}

bool restoreRadio() {
    bool ok = true;
    if (radio.changedFilter && !driverOk(esp_wifi_set_promiscuous_filter(&radio.filter), "restore filter")) ok = false;
    if (radio.enabledSta) {
        if (!WiFi.mode(WIFI_OFF)) ok = false;
    } else if (radio.changedChannel &&
               !driverOk(esp_wifi_set_channel(radio.channel, radio.secondary), "restore channel")) {
        ok = false;
    }
    if (radio.wasAssociated && !isAssociated() &&
        !driverOk(esp_wifi_connect(), "reconnect station")) ok = false;
    ui_wifi_bridge_release_monitor();
    return ok;
}

bool writeBytes(const uint8_t* bytes, size_t length) {
    if (fwrite(bytes, 1, length, capture) != length) return false;
    fileBytes += length;
    return true;
}

bool openCapture() {
    const auto sd = sd_guard::check();
    if (sd != sd_guard::Status::Ok) {
        statusText = sd_guard::message(sd);
        return false;
    }
    if (!SD_MMC.exists("/captures") && !SD_MMC.mkdir("/captures")) {
        statusText = "Cannot create /captures";
        return false;
    }

    const unsigned long stamp = millis();
    for (unsigned i = 0; i < 32; ++i) {
        snprintf(finalPath, sizeof(finalPath), "/captures/wifi-%08lx-%02u.pcap", stamp, i);
        snprintf(partialPath, sizeof(partialPath), "%s.part", finalPath);
        if (SD_MMC.exists(finalPath) || SD_MMC.exists(partialPath)) continue;
        // All card operations run on the launcher task, and USB MSC is
        // unavailable while an app is open. No other writer uses these paths.
        char vfsPath[64];
        snprintf(vfsPath, sizeof(vfsPath), "/sdcard%s", partialPath);
        capture = fopen(vfsPath, "wb");
        if (!capture) {
            statusText = "Cannot open PCAP";
            return false;
        }
        uint8_t header[wifi_pcap::kFileHeaderSize];
        wifi_pcap::fileHeader(header);
        if (writeBytes(header, sizeof(header))) return true;
        statusText = "PCAP header write failed";
        return false;
    }
    statusText = "No free capture filename";
    return false;
}

bool closeCapture(bool publish) {
    if (!capture) return !publish;
    if (publish) {
        const bool flushed = fflush(capture) == 0;
        publish = flushed && fsync(fileno(capture)) == 0;
    }
    const bool closed = fclose(capture) == 0;
    capture = nullptr;
    if (publish && closed && !SD_MMC.exists(finalPath) && SD_MMC.rename(partialPath, finalPath)) {
        memcpy(publishedPath, finalPath, sizeof(publishedPath));
        return true;
    }
    if (!usb_msc_is_active()) SD_MMC.remove(partialPath);
    return false;
}

bool failStart(const char* message) {
    const bool stopped = disableReceiver();
    if (capture) closeCapture(false);
    const bool restored = restoreRadio();
    statusText = stopped && restored ? message : "Wi-Fi restore failed";
    return false;
}

enum class Drain { Ok, Limit, WriteFailed };

Drain drain(size_t limit) {
    if (!capture) return Drain::Ok;
    Packet packet;
    for (size_t i = 0; i < limit && pop(packet); ++i) {
        if (fileBytes + wifi_pcap::kRecordHeaderSize + packet.length > MaxFileBytes) return Drain::Limit;
        uint8_t header[wifi_pcap::kRecordHeaderSize];
        wifi_pcap::recordHeader(header, packet.timeUs, packet.length, packet.originalLength);
        if (!writeBytes(header, sizeof(header)) || !writeBytes(packet.bytes, packet.length))
            return Drain::WriteFailed;
        portENTER_CRITICAL(&rxLock);
        ++rx.stats.saved;
        portEXIT_CRITICAL(&rxLock);
    }
    return Drain::Ok;
}

void finish(Drain result, bool channelMoved = false) {
    const bool stopped = disableReceiver();
    active = false;
    if (capture) {
        if (result == Drain::Ok) result = drain(QueueSize);
        const bool saved = closeCapture(result != Drain::WriteFailed);
        statusText = !saved ? "PCAP write or close failed" :
                      result == Drain::Limit ? "16 MiB limit reached" :
                      channelMoved ? "Channel changed; PCAP saved" : "PCAP saved";
    } else {
        statusText = channelMoved ? "Channel changed; stopped" : "Monitor stopped";
    }
    const bool restored = restoreRadio();
    if (!stopped || !restored) statusText = "Wi-Fi restore failed";
}

} // namespace

namespace wifi_packet_monitor {

bool start(uint8_t requestedChannel, Filter filter, bool save) {
    if (active) return false;
    publishedPath[0] = 0;
    portENTER_CRITICAL(&rxLock);
    rx.stats = {};
    portEXIT_CRITICAL(&rxLock);
    if (!settings_get_wifi_en()) {
        statusText = "Enable Wi-Fi in Settings";
        return false;
    }
    if (!ui_wifi_bridge_try_acquire_monitor()) {
        statusText = "Wi-Fi busy; retry";
        return false;
    }

    radio = {};
    fileBytes = 0;
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return failStart("Wi-Fi scan still running");
    const wifi_mode_t mode = WiFi.getMode();
    if (mode != WIFI_OFF && mode != WIFI_STA) return failStart("Use Wi-Fi station mode");
    if (mode == WIFI_OFF) {
        // Roll back even if Arduino started the interface but reports failure.
        radio.enabledSta = true;
        if (!WiFi.mode(WIFI_STA)) return failStart("Cannot enable Wi-Fi");
    }

    bool alreadyMonitoring = false;
    if (!driverOk(esp_wifi_get_promiscuous(&alreadyMonitoring), "read monitor mode"))
        return failStart("Cannot read radio state");
    if (alreadyMonitoring) return failStart("Another monitor is active");
    if (!driverOk(esp_wifi_get_promiscuous_filter(&radio.filter), "read RX filter") ||
        !driverOk(esp_wifi_get_channel(&radio.channel, &radio.secondary), "read channel") ||
        radio.channel == 0) return failStart("Cannot read radio state");

    radio.wasAssociated = isAssociated();
    const uint8_t channel = requestedChannel ? requestedChannel : radio.channel;
    if (channel != radio.channel) {
        if (radio.wasAssociated) return failStart("Connected: use current channel");
        wifi_country_t country{};
        if (!driverOk(esp_wifi_get_country(&country), "read Wi-Fi region"))
            return failStart("Cannot read Wi-Fi region");
        if (channel < country.schan || channel >= int(country.schan) + country.nchan || channel > 14)
            return failStart("Channel outside Wi-Fi region");
        if (!driverOk(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE), "tune channel"))
            return failStart("Cannot tune channel");
        radio.changedChannel = true;
    }

    if (save && !openCapture()) return failStart(statusText);
    wifi_promiscuous_filter_t filterMask{};
    filterMask.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    if (!driverOk(esp_wifi_set_promiscuous_filter(&filterMask), "set RX filter"))
        return failStart("Cannot set capture filter");
    radio.changedFilter = true;
    if (!driverOk(esp_wifi_set_promiscuous_rx_cb(receive), "attach RX"))
        return failStart("Cannot attach monitor");
    radio.installedCallback = true;
    if (!driverOk(esp_wifi_set_promiscuous(true), "enable monitor"))
        return failStart("Cannot start monitor");
    radio.enabledMonitor = true;

    timeval clock{};
    const bool hasClock = gettimeofday(&clock, nullptr) == 0 && clock.tv_sec >= 946684800;
    const int64_t mono = esp_timer_get_time();
    portENTER_CRITICAL(&rxLock);
    rx.head = rx.tail = 0;
    rx.save = save;
    rx.filter = filter;
    rx.stats.channel = channel;
    rx.monoBaseUs = mono;
    rx.clockBaseUs = hasClock ? uint64_t(clock.tv_sec) * 1000000 + clock.tv_usec : uint64_t(mono);
    rx.accepting = true;
    portEXIT_CRITICAL(&rxLock);
    lastChannelCheck = millis();
    active = true;
    statusText = save ? "Monitoring and saving PCAP" : "Monitoring only";
    return true;
}

void tick() {
    if (!active) return;
    if (capture && usb_msc_is_active()) {
        finish(Drain::WriteFailed);
        return;
    }
    const uint32_t now = millis();
    if (now - lastChannelCheck >= ChannelCheckMs) {
        lastChannelCheck = now;
        uint8_t channel = 0;
        wifi_second_chan_t secondary;
        if (esp_wifi_get_channel(&channel, &secondary) != ESP_OK || channel != rx.stats.channel) {
            finish(Drain::Ok, true);
            return;
        }
    }
    const Drain result = drain(WriteBatch);
    if (result != Drain::Ok) finish(result);
}

void stop() {
    if (active) finish(capture && usb_msc_is_active() ? Drain::WriteFailed : Drain::Ok);
}

bool running() { return active; }

Stats stats() {
    portENTER_CRITICAL(&rxLock);
    const Stats snapshot = rx.stats;
    portEXIT_CRITICAL(&rxLock);
    return snapshot;
}

const char* message() { return statusText; }
const char* savedPath() { return publishedPath; }

} // namespace wifi_packet_monitor
