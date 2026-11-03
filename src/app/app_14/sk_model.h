// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/**
 * @file sk_model.h
 * @brief Boat Dash (app_14) data core: Signal K conversions, severity, config, staleness.
 *
 * Pure C++17 plus ArduinoJson: no Arduino/ESP headers, no clock, no I/O, so it
 * host-compiles (see tools/host_tests). Time is injected as a millis() value.
 *
 * Semantics are a port of the VesselStack display bridge (https://github.com/jclima/VesselStack,
 * commit 0336a0b): displays/conversions.py, displays/severity.py, displays/topics.py
 * (freshness), displays/schema.py (config validation). Deliberate differences are
 * marked "differs from VesselStack" below.
 */
#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sk {

constexpr size_t kMaxItems = 32;        // instruments + notifications in one config
constexpr size_t kMaxPages = 8;
constexpr size_t kMaxPageItems = 8;
constexpr double kMaxAbsValue = 1e15;   // larger converted readings are invalid
constexpr size_t kNestingLimit = 20;    // JSON nesting limit for vessels/self (filtered-out noise counts too)

// ---------------------------------------------------------------------------
// Severity
// ---------------------------------------------------------------------------

enum class Sev : uint8_t { Unknown = 0, Ok, Warn, Alarm };
const char* sevName(Sev s);  // "unknown" "ok" "warn" "alarm"

// ---------------------------------------------------------------------------
// Conversions (displays/conversions.py)
// ---------------------------------------------------------------------------

/// Number of numeric conversions in the table (excludes "position").
size_t conversionCount();
const char* conversionName(size_t index);
/// True for every numeric conversion name and for "position".
bool isKnownConversion(const char* name);
/// Apply a numeric conversion. False for an unknown name, "position", or a failed conversion.
bool convert(const char* name, double value, double& out);
/// Round half away from zero on the 15-significant-digit decimal form ("%.15g"; Python uses repr(),
/// which can keep up to 17 digits); never -0.0.
/// False for non-finite values, |value| > kMaxAbsValue, or precision outside 0..9.
bool roundValue(double value, int precision, double& out);
/// convert + round; a "rad->deg_360" result that rounds up to 360 wraps to 0.
bool convertAndRound(const char* name, double value, int precision, double& out);
/// "46 12.345'N 123 45.678'W" (degrees, decimal minutes). False for non-finite or out of range input.
bool formatPosition(double lat, double lon, char* out, size_t outSize);

// ---------------------------------------------------------------------------
// Thresholds and severity (displays/severity.py)
// ---------------------------------------------------------------------------

struct Thresholds {
  bool hasAlarmBelow = false, hasWarnBelow = false, hasWarnAbove = false, hasAlarmAbove = false;
  double alarmBelow = 0, warnBelow = 0, warnAbove = 0, alarmAbove = 0;
  double hysteresis = 0;
  bool any() const { return hasAlarmBelow || hasWarnBelow || hasWarnAbove || hasAlarmAbove; }
};

/// Strict bounds (a value equal to a bound stays on the safe side); easing to a less
/// severe level also needs to clear the bound by `hysteresis`. `prev` = Unknown for none.
/// A non-finite value is Unknown.
Sev numericSev(double value, const Thresholds& th, Sev prev);
/// normal/nominal -> Ok, alert/warn -> Warn, alarm/emergency -> Alarm, else Unknown.
Sev notificationStateSev(const char* state);
/// `value` is the `value` of a notifications.* node. Null/missing follows `absentOk`
/// (Ok or Unknown); anything that is not an object with a string `state` is Unknown.
Sev notificationSev(JsonVariantConst value, bool absentOk);

// ---------------------------------------------------------------------------
// JSON numbers
// ---------------------------------------------------------------------------

/// Read a JSON number as a double that matches what Python's float() would give for the text.
/// ArduinoJson 7 parses short literals (mantissa <= 2^24) into a float32, so 11.8 arrives as
/// 11.8000001907 and 6.45 as 6.4499998. For such a value (float-exact) the shortest decimal
/// (1..7 significant digits) within 4 float ulps is recovered. Integers and wider literals
/// are returned as parsed. False if `v` is not a number.
/// Residual limitation: a literal of 7+ significant digits that fits the float path cannot
/// be recovered with certainty (the information is lost at parse time).
bool jsonNumber(JsonVariantConst v, double& out);

// ---------------------------------------------------------------------------
// Time parsing (staleness on first sight)
// ---------------------------------------------------------------------------

/// "2026-11-02T10:00:00[.fff][Z|+hh:mm|-hhmm]" -> epoch milliseconds (no zone = UTC).
bool parseIso8601(const char* text, int64_t& epochMs);
/// RFC 1123 / IMF-fixdate "Sun, 06 Nov 1994 08:49:37 GMT" (weekday optional) -> epoch milliseconds.
bool parseHttpDate(const char* text, int64_t& epochMs);

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

enum class Fetch : uint8_t { Tree, Paths };

struct Server {
  std::string host;            // empty: discover
  int port = 3000;
  std::string token;
  int pollMs = 2000;
  Fetch fetch = Fetch::Tree;
};

/// A Signal K server address. `configured` is true ONLY for the endpoint built from the config file's
/// server.host/port; discovered (mDNS) and cached (NVS) endpoints are never configured.
struct Endpoint {
  std::string host;
  int port = 3000;
  bool fromDiscovery = false;  // cache it on success
  bool configured = false;     // came from server.host: the only endpoint that may receive the token
};

struct Instrument {
  std::string id, path, label, convert, unit;
  int precision = 0;
  int staleAfterS = 10;
  bool hasThresholds = false;
  Thresholds th;
};

struct Notification {
  std::string id, path, label;
  bool absentOk = true;  // schema default "ok"
};

struct Page {
  std::string title;
  std::vector<std::string> items;
  bool status = false;  // "special":"status": the app draws its status page
};

struct Config {
  Server server;
  std::vector<Instrument> instruments;
  std::vector<Notification> notifications;
  std::vector<Page> pages;

  /// Snapshot index space: instruments first, then notifications.
  size_t itemCount() const { return instruments.size() + notifications.size(); }
  /// Snapshot index of an id, or -1.
  int findItem(const std::string& id) const;
};

/// Parse and validate. On failure returns false and `err` holds a message with a position:
/// "JSON error: ... at line L, column C (byte N)" or "$.instruments.depth.precision: ...".
/// Only the first problem is reported. Unknown keys are errors (as in VesselStack) except the
/// VesselStack keys this firmware ignores: icon, min_interval_ms, min, max, alarm, layout.
bool parseConfig(const char* json, size_t len, Config& out, std::string& err);

// ---------------------------------------------------------------------------
// Filter and extraction (vessels/self tree)
// ---------------------------------------------------------------------------

/// Filter document for deserializeJson(DeserializationOption::Filter): keeps `value` and
/// `timestamp` of every configured instrument path (`value.state` + `timestamp` for notifications).
bool buildFilter(const Config& cfg, JsonDocument& filter);
/// Parse a vessels/self body through the filter (nesting limit kNestingLimit so noise does not abort).
DeserializationError parseTree(const char* json, size_t len, const JsonDocument& filter, JsonDocument& out);

struct Reading {
  bool present = false;               // node exists and its value is not null
  JsonVariantConst value;
  const char* timestamp = nullptr;    // points into the document
};
/// A node that is already the leaf ({"value":..,"timestamp":..}), e.g. a per-path GET body.
Reading readNode(JsonVariantConst node);
/// Walk a dotted path (numeric segments are object keys) then readNode().
Reading lookup(JsonVariantConst root, const std::string& dottedPath);

// ---------------------------------------------------------------------------
// Snapshot and staleness tracker
// ---------------------------------------------------------------------------

struct SnapItem {
  double value = 0;          // converted + rounded (0 for position / notifications)
  char display[28] = "--";   // formatted value, position string, or notification label
  char unit[9] = "";
  Sev sev = Sev::Unknown;
  bool stale = false;        // had data once, now too old (or disconnected, for notifications)
  bool missing = true;       // never received
  bool notification = false;
  bool hasThresholds = false;  // false: a fresh sev of Ok means "no thresholds", not "green"
  uint32_t ageS = 0;
};

struct Snapshot {
  size_t count = 0;
  bool connected = false;
  SnapItem items[kMaxItems];
};

/// Clock-free freshness. The Config must outlive the Tracker and not change size.
///
/// Instruments: an item "changes" when its timestamp string differs from the last one (when
/// the node has no timestamp: when the formatted value differs). It is stale when
/// ageOffset + (now - changedAt) > stale_after_s * 1000. On first sight, when both the HTTP
/// Date and the timestamp parse, ageOffset = Date - timestamp, so a frozen path is stale at once.
/// Stale or missing -> severity Unknown and the hysteresis memory is reset.
/// Notifications (differs from VesselStack's push model): no per-item timeout; they follow
/// the connection, like VesselStack's EventEngine. A successful full fetch is a complete
/// picture, so there is no "settle" window.
class Tracker {
 public:
  void begin(const Config& cfg);
  /// `dateMs` = parsed HTTP Date header, or a negative value when unknown.
  void applyInstrument(size_t index, const Reading& r, uint32_t nowMs, int64_t dateMs);
  void applyNotification(size_t index, const Reading& r);
  /// One successful full-tree poll: every item, then connected = true.
  void applyTree(JsonVariantConst root, uint32_t nowMs, int64_t dateMs);
  /// Fetch failed / recovered. Disconnected notifications become stale+Unknown and are forgotten.
  void setConnected(bool connected);
  /// Refreshes staleness (and resets memory of stale items), then fills `out`.
  void snapshot(uint32_t nowMs, Snapshot& out);

 private:
  struct State {
    bool seen = false;
    bool hasTs = false;
    char ts[40] = "";
    uint32_t changedAt = 0;
    uint32_t ageOffsetMs = 0;
    double value = 0;
    char text[28] = "";
    Sev sev = Sev::Unknown;
  };
  uint64_t ageMs(const State& s, uint32_t nowMs) const;

  const Config* cfg_ = nullptr;
  State inst_[kMaxItems];
  Sev noteSev_[kMaxItems] = {};
  bool noteSeen_[kMaxItems] = {};
  bool connected_ = false;
  bool everConnected_ = false;
};

/// True for an mDNS name: ends in ".local" (case-insensitive, optional trailing dot) with a
/// non-empty label before it. "local", "notlocal" and "pi.lan" are not mDNS names.
inline bool isMdnsHost(const std::string& host) {
  std::string h = host;
  if (!h.empty() && h.back() == '.') h.pop_back();
  static const char kSuffix[] = ".local";
  const size_t n = sizeof(kSuffix) - 1;
  if (h.size() <= n) return false;
  for (size_t i = 0; i < n; i++) {
    char c = h[h.size() - n + i];
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    if (c != kSuffix[i]) return false;
  }
  return true;
}

/// Security: the bearer token is sent only to the explicitly configured server.host (never to a
/// discovered or cached endpoint, since mDNS can be spoofed on the LAN), only if a token is set,
/// and never to a ".local" (mDNS) name, whose resolution is spoofable. Plain HTTP stays cleartext.
inline bool shouldSendToken(const Endpoint& ep, const Config& cfg) {
  return ep.configured && !cfg.server.token.empty() && !isMdnsHost(ep.host) &&
         !isMdnsHost(cfg.server.host);
}

}  // namespace sk
