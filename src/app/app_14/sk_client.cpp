// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/** Boat Dash Signal K client: discovery + polling worker. GET only. Never logs the token. */
#include "sk_client.h"

#include "../../system/persist.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <mdns.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>
#include <string>

namespace sk {

namespace {

constexpr uint32_t kStackBytes = 12288;
constexpr uint32_t kConnectTimeoutMs = 2000;
constexpr uint32_t kIoTimeoutMs = 3000;
constexpr uint32_t kMdnsQueryMs = 2000;
constexpr uint32_t kRediscoverMs = 8000;
constexpr uint32_t kMaxBodyIdleMs = kIoTimeoutMs;
constexpr char kCacheKey[] = PKEY_SK_LAST_IP;
constexpr char kMdnsName[] = "grumpykitty";
constexpr char kFallbackHost[] = "vesselstack";

/// ArduinoJson allocator that prefers PSRAM (the unfiltered tree can be large; the filtered doc is small).
class PsramAllocator : public ArduinoJson::Allocator {
 public:
  void* allocate(size_t n) override {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
  }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override {
    void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return q ? q : realloc(p, n);
  }
};

/// Buffered, stop-aware reader over the HTTP body for ArduinoJson (it only needs read/readBytes).
class BodyStream : public Stream {
 public:
  BodyStream(WiFiClient* c, const std::atomic<bool>& stop) : c_(c), stop_(stop) {}
  bool aborted() const { return aborted_; }
  bool timedOut() const { return timedOut_; }

  int available() override { return int(len_ - pos_) + c_->available(); }
  int read() override {
    if (pos_ >= len_ && !fill()) return -1;
    return buf_[pos_++];
  }
  int peek() override {
    if (pos_ >= len_ && !fill()) return -1;
    return buf_[pos_];
  }
  size_t readBytes(char* out, size_t n) override {
    size_t got = 0;
    while (got < n) {
      if (pos_ >= len_ && !fill()) break;
      const size_t k = std::min(n - got, len_ - pos_);
      memcpy(out + got, buf_ + pos_, k);
      pos_ += k;
      got += k;
    }
    return got;
  }
  size_t write(uint8_t) override { return 0; }
  void flush() override {}

 private:
  // Blocks (in 2 ms steps) until at least one byte, EOF, timeout or stop.
  bool fill() {
    pos_ = len_ = 0;
    const uint32_t t0 = millis();
    for (;;) {
      if (stop_.load()) { aborted_ = true; return false; }
      const int av = c_->available();
      if (av > 0) {
        const int n = c_->read(buf_, std::min<size_t>(size_t(av), sizeof buf_));
        if (n > 0) { len_ = size_t(n); return true; }
      } else if (!c_->connected()) {
        return false;  // clean EOF
      }
      if (millis() - t0 > kMaxBodyIdleMs) { timedOut_ = true; return false; }
      delay(2);
    }
  }

  WiFiClient* c_;
  const std::atomic<bool>& stop_;
  uint8_t buf_[512];
  size_t pos_ = 0, len_ = 0;
  bool aborted_ = false, timedOut_ = false;
};

enum class Fetch1 : uint8_t { Ok, Stopped, Auth, NotFound, Failed };

}  // namespace

struct Client::Shared {
  Config cfg;
  Tracker tracker;
  ClientStatus st;
  SemaphoreHandle_t mutex = nullptr;
  SemaphoreHandle_t done = nullptr;
  std::atomic<bool> stop{false};
  std::atomic<bool> kick{false};
  std::atomic<int> refs{2};  // UI side + worker side
  TaskHandle_t task = nullptr;

  Shared() {
    mutex = xSemaphoreCreateMutex();
    done = xSemaphoreCreateBinary();
  }
  ~Shared() {
    if (mutex) vSemaphoreDelete(mutex);
    if (done) vSemaphoreDelete(done);
  }
  bool ok() const { return mutex && done; }
};

const char* linkText(LinkState s) {
  switch (s) {
    case LinkState::Starting: return "Starting...";
    case LinkState::Discovering: return "Looking for server...";
    case LinkState::Connected: return "Connected";
    case LinkState::AuthRequired: return "Auth required: add token";
    case LinkState::AuthNeedsHost: return "Auth needs fixed host: set server.host";
    case LinkState::AuthNeedsIp: return "Token needs IP host (not .local)";
    case LinkState::Unreachable: return "Server unreachable";
    case LinkState::NoHost: return "Set host in /grumpykitty/signalk.json";
  }
  return "";
}

namespace {

using Shared = Client::Shared;

struct Lock {
  explicit Lock(Shared& s) : m(s.mutex) { xSemaphoreTake(m, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m); }
  SemaphoreHandle_t m;
};

void setStatus(Shared& sh, LinkState link, const char* err, const char* host = nullptr) {
  Lock l(sh);
  sh.st.link = link;
  if (err) snprintf(sh.st.error, sizeof sh.st.error, "%s", err);
  if (host) snprintf(sh.st.host, sizeof sh.st.host, "%s", host);
}

// Sleep up to ms, waking early on stop or kick. True if the full time elapsed uninterrupted.
bool sleepSlices(Shared& sh, uint32_t ms) {
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (sh.stop.load()) return false;
    if (sh.kick.exchange(false)) return false;
    delay(25);
  }
  return true;
}

std::string hostPort(const Endpoint& ep) { return ep.host + ":" + std::to_string(ep.port); }

std::string baseUrl(const Endpoint& ep) { return "http://" + hostPort(ep); }

// One GET. On 200, `onBody` is called with the response stream. Returns Ok only if onBody returned true.
template <typename BodyFn>
Fetch1 httpGet(Shared& sh, const Endpoint& ep, const std::string& url, int& code, int64_t& dateMs,
               char* err, size_t errLen, BodyFn onBody) {
  code = 0;
  dateMs = -1;
  HTTPClient http;
  http.useHTTP10(true);
  http.setReuse(false);
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kIoTimeoutMs);
  static const char* kHeaders[] = {"Date"};
  http.collectHeaders(kHeaders, 1);
  if (!http.begin(String(url.c_str()))) {
    snprintf(err, errLen, "bad URL");
    return Fetch1::Failed;
  }
  http.addHeader("Accept", "application/json");
  if (shouldSendToken(ep, sh.cfg)) {  // configured host only; never discovered/cached endpoints
    String auth("Bearer ");
    auth += sh.cfg.server.token.c_str();
    http.addHeader("Authorization", auth);
  }
  if (sh.stop.load()) { http.end(); return Fetch1::Stopped; }
  code = http.GET();
  Fetch1 r = Fetch1::Failed;
  if (code == 200) {
    const String d = http.header("Date");
    if (d.length()) parseHttpDate(d.c_str(), dateMs);
    BodyStream body(http.getStreamPtr(), sh.stop);
    if (onBody(body, err, errLen)) r = Fetch1::Ok;
    else if (body.aborted()) r = Fetch1::Stopped;
    else if (body.timedOut() && !err[0]) snprintf(err, errLen, "read timeout");
  } else if (code == 401 || code == 403) {
    r = Fetch1::Auth;
    snprintf(err, errLen, "HTTP %d", code);
  } else if (code == 404) {
    r = Fetch1::NotFound;
    snprintf(err, errLen, "HTTP 404");
  } else if (code > 0) {
    snprintf(err, errLen, "HTTP %d", code);
  } else {
    snprintf(err, errLen, "%s", HTTPClient::errorToString(code).c_str());
  }
  http.end();
  return r;
}

// GET /signalk and require endpoints.v1.
bool validate(Shared& sh, const Endpoint& ep) {
  char err[56] = "";
  int code;
  int64_t dateMs;
  JsonDocument filter;
  filter["endpoints"]["v1"] = true;
  const Fetch1 r = httpGet(sh, ep, baseUrl(ep) + "/signalk", code, dateMs, err, sizeof err,
                           [&](Stream& s, char*, size_t) {
                             JsonDocument doc;
                             if (deserializeJson(doc, s, DeserializationOption::Filter(filter),
                                                 DeserializationOption::NestingLimit(kNestingLimit))) return false;
                             return !doc["endpoints"]["v1"].isNull();
                           });
  return r == Fetch1::Ok;
}

bool parseCache(const char* s, Endpoint& ep) {
  const char* colon = strchr(s, ':');
  if (!colon || colon == s) return false;
  const int port = atoi(colon + 1);
  if (port < 1 || port > 65535) return false;
  ep.host.assign(s, size_t(colon - s));
  ep.port = port;
  return true;
}

void cacheEndpoint(const Endpoint& ep) {
  char have[40] = "";
  persist_get_str(kCacheKey, have, sizeof have, "");
  const std::string now = hostPort(ep);
  if (now.size() < sizeof have && now != have) persist_set_str(kCacheKey, now.c_str());
}

enum class Found : uint8_t { Yes, No, Stopped };

// Step (c)+(d): mDNS. Returns after at most ~2x kMdnsQueryMs plus validation.
Found discoverMdns(Shared& sh, Endpoint& out) {
  if (!MDNS.begin(kMdnsName)) return Found::No;
  Found res = Found::No;
  mdns_result_t* results = nullptr;
  if (mdns_query_ptr("_signalk-http", "_tcp", kMdnsQueryMs, 4, &results) == ESP_OK && results) {
    int tried = 0;
    for (mdns_result_t* r = results; r && tried < 3 && res == Found::No; r = r->next) {
      for (mdns_ip_addr_t* a = r->addr; a && tried < 3; a = a->next) {
        if (sh.stop.load()) { res = Found::Stopped; break; }
        if (a->addr.type != ESP_IPADDR_TYPE_V4) continue;
        Endpoint ep;
        ep.host = IPAddress(a->addr.u_addr.ip4.addr).toString().c_str();
        ep.port = r->port ? r->port : sh.cfg.server.port;
        ep.fromDiscovery = true;
        ++tried;
        if (validate(sh, ep)) { out = ep; res = Found::Yes; break; }
      }
    }
    mdns_query_results_free(results);
  }
  if (res == Found::No && sh.stop.load()) res = Found::Stopped;
  if (res == Found::No) {
    const IPAddress ip = MDNS.queryHost(kFallbackHost, kMdnsQueryMs);
    if (sh.stop.load()) res = Found::Stopped;
    else if (ip != IPAddress(0, 0, 0, 0)) {
      Endpoint ep;
      ep.host = ip.toString().c_str();
      ep.port = sh.cfg.server.port;
      ep.fromDiscovery = true;
      if (validate(sh, ep)) { out = ep; res = Found::Yes; }
    }
  }
  MDNS.end();
  return res;
}

// Discovery order: (a) config host, (b) cached ip:port, (c) _signalk-http._tcp, (d) vesselstack.local.
// An explicit host is authoritative: if it does not answer we do not wander to another server.
Found resolve(Shared& sh, Endpoint& out) {
  const Server& sv = sh.cfg.server;
  if (!sv.host.empty()) {
    Endpoint ep;
    ep.host = sv.host;
    ep.port = sv.port;
    ep.configured = true;  // the only place an endpoint becomes configured
    setStatus(sh, LinkState::Discovering, "", hostPort(ep).c_str());
    if (validate(sh, ep)) { out = ep; return Found::Yes; }
    return sh.stop.load() ? Found::Stopped : Found::No;
  }
  setStatus(sh, LinkState::Discovering, "", "");
  char cached[40] = "";
  persist_get_str(kCacheKey, cached, sizeof cached, "");
  Endpoint ep;
  if (cached[0] && parseCache(cached, ep)) {
    ep.fromDiscovery = false;  // already cached
    ep.configured = false;     // cached endpoints never get the token
    if (validate(sh, ep)) { out = ep; return Found::Yes; }
  }
  if (sh.stop.load()) return Found::Stopped;
  const Found f = discoverMdns(sh, out);
  if (f == Found::Yes) cacheEndpoint(out);
  return f;
}

std::string pathToUrlTail(const std::string& dotted) {
  std::string s = dotted;
  std::replace(s.begin(), s.end(), '.', '/');
  return s;
}

struct PollOutcome {
  Fetch1 r = Fetch1::Failed;
  char err[56] = "";
};

// Full-tree poll.
PollOutcome pollTree(Shared& sh, const Endpoint& ep, const JsonDocument& filter, PsramAllocator& alloc) {
  PollOutcome o;
  int code;
  int64_t dateMs;
  JsonDocument doc(&alloc);
  o.r = httpGet(sh, ep, baseUrl(ep) + "/signalk/v1/api/vessels/self", code, dateMs, o.err, sizeof o.err,
                [&](Stream& s, char* err, size_t errLen) {
                  const DeserializationError de =
                      deserializeJson(doc, s, DeserializationOption::Filter(filter),
                                      DeserializationOption::NestingLimit(kNestingLimit));
                  if (de) {
                    snprintf(err, errLen, "parse: %s", de.c_str());
                    return false;
                  }
                  return true;
                });
  if (o.r == Fetch1::Ok) {
    Lock l(sh);
    sh.tracker.applyTree(doc.as<JsonVariantConst>(), millis(), dateMs);
  }
  return o;
}

// fetch=per_path: one GET per configured path; 404 = absent.
PollOutcome pollPaths(Shared& sh, const Endpoint& ep) {
  PollOutcome o;
  JsonDocument instFilter, noteFilter;
  instFilter["value"] = true;
  instFilter["timestamp"] = true;
  noteFilter["value"]["state"] = true;
  noteFilter["timestamp"] = true;
  const std::string base = baseUrl(ep) + "/signalk/v1/api/vessels/self/";
  const size_t nInst = sh.cfg.instruments.size(), nNote = sh.cfg.notifications.size();
  for (size_t i = 0; i < nInst + nNote; i++) {
    if (sh.stop.load()) { o.r = Fetch1::Stopped; return o; }
    const bool isInst = i < nInst;
    const std::string& path = isInst ? sh.cfg.instruments[i].path : sh.cfg.notifications[i - nInst].path;
    int code;
    int64_t dateMs;
    JsonDocument doc;
    char err[56] = "";
    const Fetch1 r = httpGet(sh, ep, base + pathToUrlTail(path), code, dateMs, err, sizeof err,
                             [&](Stream& s, char* e, size_t eLen) {
                               const DeserializationError de =
                                   deserializeJson(doc, s, DeserializationOption::Filter(isInst ? instFilter : noteFilter),
                                                   DeserializationOption::NestingLimit(kNestingLimit));
                               if (de) { snprintf(e, eLen, "parse: %s", de.c_str()); return false; }
                               return true;
                             });
    if (r != Fetch1::Ok && r != Fetch1::NotFound) {
      o.r = r;
      memcpy(o.err, err, sizeof o.err);
      return o;
    }
    const Reading rd = (r == Fetch1::Ok) ? readNode(doc.as<JsonVariantConst>()) : Reading();
    Lock l(sh);
    if (isInst) sh.tracker.applyInstrument(i, rd, millis(), dateMs);
    else sh.tracker.applyNotification(i - nInst, rd);
  }
  Lock l(sh);
  sh.tracker.setConnected(true);
  o.r = Fetch1::Ok;
  return o;
}

void run(Shared& sh) {
  PsramAllocator alloc;
  JsonDocument filter;
  if (!buildFilter(sh.cfg, filter)) {
    setStatus(sh, LinkState::Unreachable, "config too large");
    return;
  }
  if (!sh.cfg.server.token.empty() && sh.cfg.server.host.empty())
    log_w("[SK] token set but server.host is empty: token not used (set server.host to send it)");
  if (!sh.cfg.server.token.empty() && isMdnsHost(sh.cfg.server.host))
    log_w("[SK] server.host is a .local (mDNS) name: token will NOT be sent (use an IP or unicast DNS name)");
  const uint32_t pollMs = uint32_t(std::max(sh.cfg.server.pollMs, 1000));
  Endpoint ep;
  bool resolved = false;
  int fails = 0, polls = 0;

  while (!sh.stop.load()) {
    if (WiFi.status() != WL_CONNECTED) {
      {
        Lock l(sh);
        sh.tracker.setConnected(false);
      }
      setStatus(sh, LinkState::Unreachable, "WiFi offline");
      sleepSlices(sh, 500);
      continue;
    }
    if (!resolved) {
      const Found f = resolve(sh, ep);
      if (f == Found::Stopped) break;
      if (f == Found::No) {
        if (sh.cfg.server.host.empty()) setStatus(sh, LinkState::NoHost, "no server found");
        else setStatus(sh, LinkState::Unreachable, "host did not answer /signalk");
        sleepSlices(sh, kRediscoverMs);
        continue;
      }
      resolved = true;
      fails = 0;
      setStatus(sh, LinkState::Starting, "", hostPort(ep).c_str());
    }

    const uint32_t t0 = millis();
    const PollOutcome o = (sh.cfg.server.fetch == Fetch::Paths) ? pollPaths(sh, ep) : pollTree(sh, ep, filter, alloc);
    if (o.r == Fetch1::Stopped) break;
    if (o.r == Fetch1::Ok) {
      fails = 0;
      Lock l(sh);
      sh.st.link = LinkState::Connected;
      sh.st.error[0] = 0;
      sh.st.everOk = true;
      sh.st.lastOkMs = millis();
    } else {
      {
        Lock l(sh);
        sh.tracker.setConnected(false);
      }
      const LinkState authState =
          !ep.configured ? LinkState::AuthNeedsHost
          : (!sh.cfg.server.token.empty() && !shouldSendToken(ep, sh.cfg)) ? LinkState::AuthNeedsIp
                                                                           : LinkState::AuthRequired;
      setStatus(sh, o.r == Fetch1::Auth ? authState : LinkState::Unreachable, o.err);
      // A discovered server that keeps failing may have moved: rediscover. An explicit host just retries.
      if (++fails >= 3 && sh.cfg.server.host.empty()) { resolved = false; fails = 0; }
    }
    if (++polls % 30 == 1) {
      const uint32_t free = uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
      log_d("[SK] stack free min %u B", (unsigned)free);
      Lock l(sh);
      sh.st.stackFreeMin = free;
    }
    const uint32_t spent = millis() - t0;
    if (!sleepSlices(sh, spent >= pollMs ? 0 : pollMs - spent) && sh.stop.load()) break;
  }
  const uint32_t free = uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
  log_i("[SK] worker exit, stack free min %u B", (unsigned)free);
}

void taskEntry(void* arg) {
  Shared* sh = static_cast<Shared*>(arg);
  run(*sh);
  xSemaphoreGive(sh->done);  // last touch of anything the UI side may free
  if (sh->refs.fetch_sub(1) == 1) delete sh;
  vTaskDelete(nullptr);
}

}  // namespace

bool Client::start(const Config& cfg) {
  if (sh_) return false;
  Shared* sh = new (std::nothrow) Shared();
  if (!sh) return false;
  if (!sh->ok()) { delete sh; return false; }
  sh->cfg = cfg;
  sh->tracker.begin(sh->cfg);  // after the copy: the Tracker keeps a pointer into the heap Shared
  if (xTaskCreatePinnedToCore(taskEntry, "sk_client", kStackBytes, sh, 1, &sh->task, 0) != pdPASS) {
    delete sh;
    return false;
  }
  sh_ = sh;
  return true;
}

void Client::stop(uint32_t waitMs) {
  if (!sh_) return;
  Shared* sh = sh_;
  sh_ = nullptr;
  sh->stop = true;
  if (xSemaphoreTake(sh->done, pdMS_TO_TICKS(waitMs)) != pdTRUE)
    log_w("[SK] worker still in a request; it will finish and free itself");
  if (sh->refs.fetch_sub(1) == 1) delete sh;
}

void Client::refreshNow() {
  if (sh_) sh_->kick = true;
}

void Client::snapshot(uint32_t nowMs, Snapshot& snap, ClientStatus& st) {
  if (!sh_) {
    snap = Snapshot();
    st = ClientStatus();
    return;
  }
  Lock l(*sh_);
  sh_->tracker.snapshot(nowMs, snap);
  st = sh_->st;
}

}  // namespace sk
