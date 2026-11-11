// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/**
 * @file sk_client.h
 * @brief Boat Dash (app_14) Signal K HTTP client: one worker task, read-only (GET only).
 *
 * The worker discovers the server, polls vessels/self (or per-path), feeds sk::Tracker and
 * publishes status. The UI thread only calls snapshot()/refreshNow()/stop(). Cooperative
 * stop: the worker checks a flag between every step and while reading the body; it is never
 * vTaskDelete()d mid-request. If a blocking step outlives stop()'s bounded wait, the worker
 * finishes on its own and frees the shared state (reference counted).
 */
#pragma once

#include "sk_model.h"

#include <cstdint>

namespace sk {

enum class LinkState : uint8_t {
  Starting,      // worker running, nothing known yet
  Discovering,   // looking for a server
  Connected,     // last poll succeeded
  AuthRequired,  // 401/403 from the configured host: token missing or wrong
  AuthNeedsHost, // 401/403 from a discovered server: token is never sent to it
  AuthNeedsIp,   // 401/403 from a configured .local host: token is never sent to mDNS names
  Unreachable,   // connect/HTTP/parse error (values age normally)
  NoHost,        // nothing configured and nothing discovered
};

struct ClientStatus {
  LinkState link = LinkState::Starting;
  char host[40] = "";   // "ip-or-name:port" of the server in use ("" if none yet)
  char error[56] = "";  // last error detail, never contains the token
  bool everOk = false;
  uint32_t lastOkMs = 0;   // millis() of the last successful poll
  uint32_t stackFreeMin = 0;  // worker stack high-water mark (bytes), updated every ~30 polls
};

/// Human text for the status line ("Auth required: add token" ...).
const char* linkText(LinkState s);

class Client {
 public:
  Client() = default;
  ~Client() { stop(); }
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  /// Copy the config and spawn the worker. False if already running or allocation/task creation failed.
  bool start(const Config& cfg);
  /// Cooperative, bounded (waitMs). Safe to call when not started.
  void stop(uint32_t waitMs = 3500);
  bool running() const { return sh_ != nullptr; }
  /// Poll now instead of waiting for the interval.
  void refreshNow();
  /// Staleness-aware snapshot of the tracker plus the current status.
  void snapshot(uint32_t nowMs, Snapshot& snap, ClientStatus& st);

  struct Shared;

 private:
  Shared* sh_ = nullptr;
};

/// True while a stopped worker still owns shared transport state.
bool workerCleanupPending();

}  // namespace sk
