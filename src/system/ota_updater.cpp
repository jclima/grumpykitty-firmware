// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

#include "ota_updater.h"
#include "ota_manifest.h"
#include "power_mgmt.h"
#include "settings_bridge.h"
#include "../ui/ui_wifi_bridge.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_tls.h>
#include <lwip/apps/sntp.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <ctime>
#include <strings.h>

// Arduino 2.0.6 shadows the SDK's esp_crt_bundle.h with its own header.
extern "C" esp_err_t esp_crt_bundle_attach(void* config);

namespace ota_updater {
namespace {
constexpr char BaseUrl[] = "https://jclima.github.io/grumpykitty-firmware/flash/";
// IDF's SNTP receive timeout is 15 s per configured server; allow both servers
// to be tried, plus a small scheduling margin, without restarting an active client.
constexpr uint32_t ClockWaitMs = 35000;
constexpr uint32_t TransferLimitMs = 300000;
constexpr time_t EarliestClock = 1704067200; // 2024-01-01
enum class Job { Check, Download };

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
Status snapshot{State::Idle, 0, 0, "", "Connect to Wi-Fi to check"};
ota_manifest::Manifest offer;
const esp_partition_t* staged = nullptr;
bool busy = false;
std::atomic<bool> cancelRequested{false};

bool powerOkay() {
    return power_is_ready() && (power_vbus_present() || power_battery_pct() >= 30);
}

bool waitClock() {
    if (time(nullptr) >= EarliestClock) return true;
    if (!sntp_enabled()) configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    const uint32_t started = millis();
    while (!cancelRequested && millis() - started < ClockWaitMs) {
        if (time(nullptr) >= EarliestClock) return true;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return false;
}

// The pinned SDK's mbedTLS build omits certificate date checks. Keep the
// SDK bundle's chain and hostname verification, and add validity checks to
// its certificate callback. Only one OTA connection exists at a time.
decltype(mbedtls_ssl_config::f_vrfy) bundleVerifier = nullptr;

int compareUtc(const std::tm& now, const mbedtls_x509_time& boundary) {
    const int current[] = {now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
                           now.tm_hour, now.tm_min, now.tm_sec};
    const int limit[] = {boundary.year, boundary.mon, boundary.day,
                         boundary.hour, boundary.min, boundary.sec};
    for (size_t i = 0; i < 6; ++i) {
        if (current[i] != limit[i]) return current[i] - limit[i];
    }
    return 0;
}

int verifyDates(void* context, mbedtls_x509_crt* certificate, int depth, uint32_t* flags) {
    const int result = bundleVerifier(context, certificate, depth, flags);
    const time_t now = time(nullptr);
    std::tm utc{};
    if (now < EarliestClock || !gmtime_r(&now, &utc)) return MBEDTLS_ERR_X509_FATAL_ERROR;
    if (compareUtc(utc, certificate->valid_from) < 0) *flags |= MBEDTLS_X509_BADCERT_FUTURE;
    if (compareUtc(utc, certificate->valid_to) > 0) *flags |= MBEDTLS_X509_BADCERT_EXPIRED;
    if (result != 0 || *flags != 0) {
        Serial.printf("[OTA] certificate depth=%d verify=%d flags=0x%lx utc=%lld\n",
                      depth, result, (unsigned long)*flags, (long long)now);
    }
    return result;
}

esp_err_t attachTrust(void* config) {
    const auto rc = esp_crt_bundle_attach(config);
    if (rc != ESP_OK || !config) return rc;
    auto* ssl = static_cast<mbedtls_ssl_config*>(config);
    bundleVerifier = ssl->f_vrfy;
    if (!bundleVerifier) return ESP_FAIL;
    mbedtls_ssl_conf_verify(ssl, verifyDates, ssl->p_vrfy);
    return ESP_OK;
}

class Response {
public:
    ~Response() { if (_http) esp_http_client_cleanup(_http); }
    Response() = default;
    Response(const Response&) = delete;
    Response& operator=(const Response&) = delete;

    int64_t open(const char* url) {
        esp_http_client_config_t cfg{};
        cfg.url = url;
        cfg.transport_type = HTTP_TRANSPORT_OVER_SSL;
        cfg.crt_bundle_attach = attachTrust;
        cfg.timeout_ms = 6000;
        cfg.disable_auto_redirect = true;
        cfg.event_handler = &onEvent;
        cfg.user_data = this;
        _http = esp_http_client_init(&cfg);
        if (!_http) {
            _error = "HTTPS setup failed";
            return -1;
        }
        if (esp_http_client_set_header(_http, "Accept-Encoding", "identity") != ESP_OK) {
            _error = "HTTPS header setup failed";
            return -1;
        }
        constexpr uint32_t internalRam = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        const size_t free = heap_caps_get_free_size(internalRam);
        const size_t largest = heap_caps_get_largest_free_block(internalRam);
        const esp_err_t open_rc = esp_http_client_open(_http, 0);
        if (open_rc != ESP_OK) {
            if (_tlsError == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME) _error = "OTA server DNS lookup failed";
            else if (_tlsError == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT) _error = "OTA server connection timed out";
            else if (_tlsCode == -MBEDTLS_ERR_X509_CERT_VERIFY_FAILED || _tlsFlags) _error = "OTA certificate check failed";
            else if (_tlsError == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED) _error = "OTA TLS handshake failed";
            else _error = "Could not connect to OTA server";
            Serial.printf("[OTA] open=%s errno=%d tls=0x%x code=%d flags=0x%x RAM=%u largest=%u\n",
                          esp_err_to_name(open_rc), esp_http_client_get_errno(_http), (unsigned)_tlsError,
                          _tlsCode, (unsigned)_tlsFlags, (unsigned)free, (unsigned)largest);
            return -1;
        }
        const int64_t length = esp_http_client_fetch_headers(_http);
        const int status = esp_http_client_get_status_code(_http);
        const bool encoded = esp_http_client_is_chunked_response(_http) || _encoded;
        if (status == 200 && length > 0 && !encoded) return length;

        if (status == 0) _error = "No response from OTA server";
        else if (status != 200) _error = "OTA server returned HTTP error";
        else if (encoded) _error = "Unsupported OTA response format";
        else _error = "OTA response length missing";
        Serial.printf("[OTA] HTTP status=%d size=%lld encoded=%d\n", status, (long long)length, encoded);
        return -1;
    }

    int read(char* out, int length) { return esp_http_client_read(_http, out, length); }
    bool complete() const { return esp_http_client_is_complete_data_received(_http); }
    const char* error() const { return _error; }

private:
    static esp_err_t onEvent(esp_http_client_event_t* event) {
        if (!event->user_data) return ESP_OK;
        auto& response = *static_cast<Response*>(event->user_data);
        if (event->event_id == HTTP_EVENT_ERROR || event->event_id == HTTP_EVENT_DISCONNECTED) {
            // ESP-IDF 4.4 passes the transport error handle for these events.
            if (event->data && response._tlsError == ESP_OK) {
                response._tlsError = esp_tls_get_and_clear_last_error(
                    static_cast<esp_tls_error_handle_t>(event->data), &response._tlsCode, &response._tlsFlags);
            }
            return ESP_OK;
        }
        if (event->event_id != HTTP_EVENT_ON_HEADER || !event->header_key) return ESP_OK;
        if (strcasecmp(event->header_key, "Transfer-Encoding") == 0 ||
            (strcasecmp(event->header_key, "Content-Encoding") == 0 &&
             (!event->header_value || strcasecmp(event->header_value, "identity") != 0))) {
            response._encoded = true;
        }
        return ESP_OK;
    }

    esp_http_client_handle_t _http = nullptr;
    bool _encoded = false;
    const char* _error = "HTTPS request failed";
    esp_err_t _tlsError = ESP_OK;
    int _tlsCode = 0;
    int _tlsFlags = 0;
};

// An OTA handle is consumed by esp_ota_end() even if image validation fails.
class Image {
public:
    ~Image() { if (_open) esp_ota_abort(_handle); }
    bool begin(const esp_partition_t* slot, uint32_t size) {
        _open = esp_ota_begin(slot, size, &_handle) == ESP_OK;
        return _open;
    }
    bool write(const uint8_t* data, size_t size) { return esp_ota_write(_handle, data, size) == ESP_OK; }
    bool finish() {
        _open = false;
        return esp_ota_end(_handle) == ESP_OK;
    }
private:
    esp_ota_handle_t _handle{};
    bool _open = false;
};

class Digest {
public:
    Digest() { mbedtls_sha256_init(&_context); }
    ~Digest() { mbedtls_sha256_free(&_context); }
    bool start() { return mbedtls_sha256_starts_ret(&_context, 0) == 0; }
    bool update(const uint8_t* data, size_t length) {
        return mbedtls_sha256_update_ret(&_context, data, length) == 0;
    }
    bool matches(const uint8_t* expected) {
        uint8_t sum[32];
        return mbedtls_sha256_finish_ret(&_context, sum) == 0 &&
               memcmp(sum, expected, sizeof(sum)) == 0;
    }
private:
    mbedtls_sha256_context _context;
};

const esp_partition_t* inactiveSlot() {
    const auto* slot0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "app0");
    const auto* slot1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, "app1");
    const auto* data = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, "otadata");
    const auto* running = esp_ota_get_running_partition();
    const auto* boot = esp_ota_get_boot_partition();
    if (!slot0 || !slot1 || !data || !running || !boot ||
        slot0->address != 0x10000 || slot1->address != 0x790000 ||
        slot0->size != ota_manifest::SlotBytes || slot1->size != ota_manifest::SlotBytes ||
        data->address != 0xe000 || data->size != 0x2000 || boot->address != running->address) return nullptr;
    if (running->address == slot0->address) return slot1;
    return running->address == slot1->address ? slot0 : nullptr;
}

struct Result {
    State state;
    const char* message;
    ota_manifest::Manifest manifest{};
    const esp_partition_t* slot = nullptr;
};

Result runCheck() {
    if (!waitClock()) return {State::Failed, "Could not sync clock for HTTPS"};
    if (cancelRequested) return {State::Idle, "Cancelled"};

    char url[sizeof(BaseUrl) + sizeof("ota.json")];
    snprintf(url, sizeof(url), "%sota.json", BaseUrl);
    Response response;
    const int64_t length = response.open(url);
    if (length < 0) return {State::Failed, response.error()};
    if (length > int64_t(ota_manifest::MaxBytes)) return {State::Failed, "OTA feed is too large"};

    char json[ota_manifest::MaxBytes];
    int received = 0;
    const uint32_t started = millis();
    while (received < length && !cancelRequested && millis() - started < 15000) {
        const int n = response.read(json + received, int(length) - received);
        if (n <= 0 || n > length - received) break;
        received += n;
    }
    if (received != length || !response.complete()) return {State::Failed, "Incomplete OTA feed"};

    ota_manifest::Manifest manifest;
    ota_manifest::Version running{};
    if (!ota_manifest::parse(json, received, manifest) || !ota_manifest::parseVersion(GK_VERSION, running))
        return {State::Failed, "Invalid OTA metadata"};
    if (!ota_manifest::newer(manifest.numeric, running))
        return {State::UpToDate, "No newer firmware", manifest};
    return {State::Available, "New firmware is available", manifest};
}

Result runDownload(const ota_manifest::Manifest& manifest) {
    const auto* slot = inactiveSlot();
    if (!slot || manifest.size > slot->size) return {State::Failed, "Use USB to install this update"};

    char url[128];
    snprintf(url, sizeof(url), "%sgrumpykitty-%s-firmware.bin", BaseUrl, manifest.version);
    Response response;
    const int64_t length = response.open(url);
    if (length < 0) return {State::Failed, response.error()};
    if (length != manifest.size) {
        Serial.printf("[OTA] firmware size expected=%u actual=%lld\n", (unsigned)manifest.size, (long long)length);
        return {State::Failed, "Firmware size mismatch"};
    }
    if (cancelRequested) return {State::Idle, "Cancelled"};

    Digest digest;
    Image image;
    if (!digest.start() || !image.begin(slot, manifest.size))
        return {State::Failed, "Could not prepare inactive slot"};

    uint8_t chunk[4096];
    uint32_t received = 0;
    const uint32_t started = millis();
    while (received < manifest.size) {
        if (cancelRequested) return {State::Idle, "Cancelled"};
        if (millis() - started >= TransferLimitMs) return {State::Failed, "Download timed out"};
        const auto need = std::min<uint32_t>(sizeof(chunk), manifest.size - received);
        const int n = response.read(reinterpret_cast<char*>(chunk), int(need));
        if (n <= 0 || unsigned(n) > need) return {State::Failed, "Download interrupted"};
        if (cancelRequested) return {State::Idle, "Cancelled"};
        if (!digest.update(chunk, n) || !image.write(chunk, n)) return {State::Failed, "Firmware write failed"};
        received += n;
        portENTER_CRITICAL(&lock);
        snapshot.written = received;
        portEXIT_CRITICAL(&lock);
    }
    if (cancelRequested) return {State::Idle, "Cancelled"};
    if (!response.complete() || !digest.matches(manifest.digest))
        return {State::Failed, "Firmware SHA-256 check failed"};
    if (!image.finish()) return {State::Failed, "ESP32 rejected the firmware image"};
    return {State::Staged, "Verified. Ready to restart", manifest, slot};
}

void worker(void* arg) {
    const Job job = arg ? Job::Download : Job::Check;
    ota_manifest::Manifest selected;
    portENTER_CRITICAL(&lock);
    selected = offer;
    portEXIT_CRITICAL(&lock);

    const Result result = job == Job::Check ? runCheck() : runDownload(selected);
    portENTER_CRITICAL(&lock);
    if (cancelRequested) {
        snapshot = Status{State::Idle, 0, 0, {}, "Update cancelled"};
        offer = {};
        staged = nullptr;
    } else {
        offer = result.manifest;
        staged = result.slot;
        snapshot = Status{result.state, result.state == State::Staged ? offer.size : 0,
                          offer.size, {}, result.message};
        memcpy(snapshot.version, offer.version, sizeof(snapshot.version));
    }
    busy = false;
    portEXIT_CRITICAL(&lock);
    vTaskDelete(nullptr);
}

bool start(Job job) {
    // Called only from the launcher, which is also the owner of Wi-Fi UI.
    const bool connected = settings_get_wifi_en() && WiFi.status() == WL_CONNECTED;
    const bool radioBusy = ui_wifi_bridge_radio_busy();
    const bool powered = job == Job::Check || powerOkay();
    portENTER_CRITICAL(&lock);
    if (busy || (job == Job::Download && snapshot.state != State::Available)) {
        portEXIT_CRITICAL(&lock);
        return false;
    }
    if (!connected || radioBusy || !powered) {
        snapshot.message = !connected ? "Connect Wi-Fi in Settings" :
                           radioBusy ? "Wi-Fi is busy; try again" : "Plug in USB or charge to 30%";
        portEXIT_CRITICAL(&lock);
        return false;
    }
    cancelRequested = false;
    busy = true;
    staged = nullptr;
    if (job == Job::Check) {
        offer = {};
        snapshot = Status{State::Checking, 0, 0, {}, "Checking for updates"};
    } else {
        snapshot.state = State::Downloading;
        snapshot.message = "Downloading and verifying";
    }
    portEXIT_CRITICAL(&lock);

    if (xTaskCreate(worker, "gk_ota", 12288, job == Job::Download ? reinterpret_cast<void*>(1) : nullptr,
                    1, nullptr) == pdPASS) return true;
    portENTER_CRITICAL(&lock);
    busy = false;
    snapshot.state = State::Failed;
    snapshot.message = "Not enough memory for OTA";
    portEXIT_CRITICAL(&lock);
    return false;
}
} // namespace

Status status() {
    portENTER_CRITICAL(&lock);
    const Status result = snapshot;
    portEXIT_CRITICAL(&lock);
    return result;
}

bool check() { return start(Job::Check); }
bool download() { return start(Job::Download); }

void cancel() {
    portENTER_CRITICAL(&lock);
    cancelRequested = true;
    if (busy) {
        snapshot.message = "Cancelling update";
    } else {
        snapshot = Status{State::Idle, 0, 0, {}, "Connect to Wi-Fi to check"};
        offer = {};
        staged = nullptr;
    }
    portEXIT_CRITICAL(&lock);
}

bool activate() {
    portENTER_CRITICAL(&lock);
    const auto* slot = !busy && snapshot.state == State::Staged ? staged : nullptr;
    portEXIT_CRITICAL(&lock);
    if (!slot || !powerOkay()) return false;
    const auto* expected = inactiveSlot();
    if (!expected || slot->address != expected->address || esp_ota_set_boot_partition(slot) != ESP_OK) {
        portENTER_CRITICAL(&lock);
        staged = nullptr;
        snapshot.state = State::Failed;
        snapshot.message = "Could not select firmware slot";
        portEXIT_CRITICAL(&lock);
        return false;
    }
    return true;
}

void confirmHealthyBoot() {
    esp_ota_img_states_t state;
    const auto* running = esp_ota_get_running_partition();
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        const esp_err_t rc = esp_ota_mark_app_valid_cancel_rollback();
        Serial.printf("[OTA] confirm boot: %s\n", esp_err_to_name(rc));
    }
}
} // namespace ota_updater

// Override Arduino's weak hook so it cannot confirm an image before setup.
extern "C" bool verifyRollbackLater() { return true; }
