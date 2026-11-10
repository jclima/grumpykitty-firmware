#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 JC Lima
"""Mock Signal K server for developing Boat Dash (app_14). Read-only, stdlib only.

    python3 -I tools/mock_signalk.py --port 3000 [--bind 0.0.0.0] [--require-token T]
                                      [--scenario ok|warn|alarm]

Endpoints (GET only):
  /signalk                              discovery document (endpoints.v1), always open
  /signalk/v1/api/vessels/self          the full vessel tree
  /signalk/v1/api/vessels/self/<a>/<b>  one node or subtree, 404 if unknown
With --require-token, the /signalk/v1/api/ endpoints answer 401 unless the request carries
"Authorization: Bearer <token>" (like a Signal K server with security on; discovery stays open).

Every data node carries value and timestamp plus noise a real server adds: $source, a
"values" map with one entry per source, and "meta". Timestamps advance with the clock, except
for one frozen path (FROZEN_PATH) whose timestamp stays at server start, so the dashboard
shows it going stale. Every response carries a Date header (BaseHTTPRequestHandler adds it, RFC 1123 GMT).

Scenario: ok (all healthy), warn, alarm. Switch with ?scenario=NAME on any request, or send
SIGUSR1 to the process to cycle ok -> warn -> alarm -> ok.
"""
import argparse
import hmac
import json
import math
import os
import signal
import sys
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

SCENARIOS = ("ok", "warn", "alarm")
FROZEN_PATH = "electrical.solar.main.current"   # timestamp never advances
START = time.time()
STATE = {"scenario": "ok"}

# path -> (source, scenario -> SI value, wiggle amplitude). Values are SI like a real server.
# Tank/battery numbers are chosen to land in the default Boat Dash thresholds
# (see src/app/app_14/sk_defaults.h).
DATA = {
    "navigation.speedOverGround":            ("gps.GP",   {"ok": 2.57, "warn": 2.57, "alarm": 2.57}, 0.15),
    "navigation.courseOverGroundTrue":       ("gps.GP",   {"ok": 1.571, "warn": 1.571, "alarm": 1.571}, 0.02),
    "environment.depth.belowTransducer":     ("can0.12",  {"ok": 4.5, "warn": 2.0, "alarm": 1.0}, 0.05),
    "environment.wind.angleApparent":        ("can0.12",  {"ok": 0.52, "warn": 0.52, "alarm": 0.52}, 0.05),
    "electrical.batteries.house.voltage":    ("vedirect", {"ok": 12.90, "warn": 12.0, "alarm": 11.5}, 0.02),
    "electrical.batteries.start.voltage":    ("vedirect", {"ok": 12.70, "warn": 11.9, "alarm": 11.2}, 0.01),
    "electrical.batteries.house.capacity.stateOfCharge": ("vedirect", {"ok": 0.85, "warn": 0.28, "alarm": 0.15}, 0.0),
    "electrical.batteries.house.current":    ("vedirect", {"ok": 5.2, "warn": -12.5, "alarm": -30.0}, 0.3),
    FROZEN_PATH:                             ("vedirect", {"ok": 3.1, "warn": 3.1, "alarm": 3.1}, 0.0),
    "tanks.fuel.0.currentLevel":             ("tank.0",   {"ok": 0.75, "warn": 0.20, "alarm": 0.05}, 0.0),
    "tanks.fuel.1.currentLevel":             ("tank.1",   {"ok": 0.70, "warn": 0.22, "alarm": 0.08}, 0.0),
    "tanks.freshWater.0.currentLevel":       ("tank.2",   {"ok": 0.60, "warn": 0.15, "alarm": 0.05}, 0.0),
    "tanks.blackWater.0.currentLevel":       ("tank.3",   {"ok": 0.20, "warn": 0.80, "alarm": 0.95}, 0.0),
    "propulsion.port.revolutions":           ("can0.20",  {"ok": 25.0, "warn": 25.0, "alarm": 25.0}, 0.3),
    "propulsion.starboard.revolutions":      ("can0.21",  {"ok": 25.0, "warn": 25.0, "alarm": 25.0}, 0.3),
    "propulsion.port.temperature":           ("can0.20",  {"ok": 355.15, "warn": 355.15, "alarm": 355.15}, 0.2),
    "propulsion.starboard.temperature":      ("can0.21",  {"ok": 353.15, "warn": 353.15, "alarm": 353.15}, 0.2),
    "propulsion.port.oilPressure":           ("can0.20",  {"ok": 300000, "warn": 300000, "alarm": 300000}, 1000),
    "propulsion.starboard.oilPressure":      ("can0.21",  {"ok": 295000, "warn": 295000, "alarm": 295000}, 1000),
}
UNITS = {"navigation.speedOverGround": "m/s", "environment.depth.belowTransducer": "m",
         "electrical.batteries.house.voltage": "V", "tanks.fuel.0.currentLevel": "ratio",
         "propulsion.port.revolutions": "Hz", "propulsion.port.temperature": "K"}

POSITION = {"latitude": 46.20575, "longitude": -123.7613}   # moves a little over time

# Notification path -> scenario -> value (None = not present in the tree; "null" = present, cleared)
NOTIFICATIONS = {
    "notifications.navigation.anchor": {"ok": None, "warn": ("alert", "Anchor moving"),
                                         "alarm": ("alarm", "Anchor dragging")},
    "notifications.electrical.batteries.house.voltage": {"ok": "null", "warn": ("warn", "House battery low"),
                                                          "alarm": ("alarm", "House battery critical")},
}


def iso(t):
    return datetime.fromtimestamp(t, timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.") + "%03dZ" % int(t * 1000 % 1000)


def wiggle(path, amp, now):
    return amp * math.sin((now - START) / 7.0 + (sum(map(ord, path)) % 13)) if amp else 0.0


def put(tree, path, node):
    cur = tree
    parts = path.split(".")
    for part in parts[:-1]:
        cur = cur.setdefault(part, {})
    cur[parts[-1]] = node


def data_node(path, source, value, ts):
    node = {
        "meta": {"description": "mock " + path, "units": UNITS.get(path, ""), "displayName": path.rsplit(".", 1)[-1]},
        "value": value,
        "$source": source,
        "timestamp": ts,
        "values": {source: {"value": value, "timestamp": ts},
                   "mock.backup": {"value": value, "timestamp": iso(START - 60)}},
    }
    return node


def build_tree(scenario, now):
    tree = {"uuid": "urn:mrn:signalk:uuid:00000000-0000-4000-8000-000000000001", "name": "Mock Vessel",
            "mmsi": "000000000", "sources": {"gps": {"GP": {"talker": "GP"}}}}
    for path, (source, by_scenario, amp) in DATA.items():
        value = by_scenario[scenario]
        if value is not None:
            value = round(value + wiggle(path, amp, now), 4)
        ts = iso(START) if path == FROZEN_PATH else iso(now)
        put(tree, path, data_node(path, source, value, ts))
    pos = {"latitude": POSITION["latitude"] + 0.00001 * math.sin((now - START) / 30.0),
           "longitude": POSITION["longitude"] + 0.00001 * math.cos((now - START) / 30.0)}
    put(tree, "navigation.position", data_node("navigation.position", "gps.GP", pos, iso(now)))
    for path, by_scenario in NOTIFICATIONS.items():
        entry = by_scenario[scenario]
        if entry is None:
            continue
        value = None if entry == "null" else {"state": entry[0], "method": ["visual", "sound"], "message": entry[1]}
        put(tree, path, {"value": value, "timestamp": iso(now), "$source": "mock.alarms"})
    return tree


def lookup(tree, path_parts):
    cur = tree
    for part in path_parts:
        if not isinstance(cur, dict) or part not in cur:
            return None
        cur = cur[part]
    return cur


class Handler(BaseHTTPRequestHandler):
    protect_discovery = False
    server_version = "MockSignalK/1"
    protocol_version = "HTTP/1.1"
    require_token = None
    auth_status = 401
    verbose = False

    def log_message(self, fmt, *args):   # never logs headers, so never the token
        if self.verbose:
            sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def send_json(self, status, obj, extra=None):
        body = json.dumps(obj, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        url = urlsplit(self.path)
        query = parse_qs(url.query)
        wanted = query.get("scenario", [None])[0]
        if wanted is not None:
            if wanted not in SCENARIOS:
                return self.send_json(400, {"error": "scenario must be one of " + ", ".join(SCENARIOS)})
            STATE["scenario"] = wanted
        path = url.path.rstrip("/") or "/"
        host = self.headers.get("Host") or "localhost"

        if path == "/signalk":
            if self.protect_discovery:
                got = self.headers.get("Authorization", "")
                scheme, _, token = got.partition(" ")
                if self.require_token is None or scheme.lower() != "bearer" or not hmac.compare_digest(token.strip().encode(), self.require_token.encode()):
                    return self.send_json(self.auth_status, {"error": "unauthorized"}, {"WWW-Authenticate": 'Bearer realm="mock-signalk"'})
            return self.send_json(200, {
                "endpoints": {"v1": {"version": "1.46.2",
                                     "signalk-http": "http://%s/signalk/v1/api/" % host,
                                     "signalk-ws": "ws://%s/signalk/v1/stream" % host}},
                "server": {"id": "mock-signalk", "version": "0.1"}})

        if path == "/signalk/v1/api" or path.startswith("/signalk/v1/api/"):
            if self.require_token is not None:
                got = self.headers.get("Authorization", "")
                scheme, _, token = got.partition(" ")
                if scheme.lower() != "bearer" or not hmac.compare_digest(token.strip().encode(), self.require_token.encode()):
                    return self.send_json(self.auth_status, {"error": "unauthorized"}, {"WWW-Authenticate": 'Bearer realm="mock-signalk"'})
            parts = [p for p in path[len("/signalk/v1/api"):].split("/") if p]
            if parts[:2] != ["vessels", "self"]:
                return self.send_json(404, {"error": "not found"})
            tree = build_tree(STATE["scenario"], time.time())
            node = lookup(tree, parts[2:])
            if node is None:
                return self.send_json(404, {"error": "not found"})
            return self.send_json(200, node)

        return self.send_json(404, {"error": "not found"})


def main():
    ap = argparse.ArgumentParser(description="Mock Signal K server (read-only) for Boat Dash development")
    ap.add_argument("--port", type=int, default=3000)
    ap.add_argument("--bind", default="127.0.0.1", help="address to bind (use 0.0.0.0 to reach it from the device)")
    ap.add_argument("--require-token", metavar="T", default=None, help="require 'Authorization: Bearer T' on the API")
    ap.add_argument("--auth-status", type=int, choices=(401, 403), default=401,
                    help="status to return for missing or wrong token (default: 401)")
    ap.add_argument("--protect-discovery", action="store_true",
                    help="require the bearer token for /signalk discovery too")
    ap.add_argument("--scenario", choices=SCENARIOS, default="ok")
    ap.add_argument("-v", "--verbose", action="store_true", help="log requests (never headers)")
    args = ap.parse_args()

    STATE["scenario"] = args.scenario
    Handler.require_token = args.require_token
    Handler.auth_status = args.auth_status
    Handler.protect_discovery = args.protect_discovery
    Handler.verbose = args.verbose

    def cycle(_signo, _frame):
        STATE["scenario"] = SCENARIOS[(SCENARIOS.index(STATE["scenario"]) + 1) % len(SCENARIOS)]
        sys.stderr.write("scenario -> %s\n" % STATE["scenario"])

    signal.signal(signal.SIGUSR1, cycle)

    httpd = ThreadingHTTPServer((args.bind, args.port), Handler)
    httpd.daemon_threads = True
    sys.stderr.write("mock Signal K on http://%s:%d/signalk  scenario=%s%s (pid %d)\n" % (
        args.bind, args.port, args.scenario, "  token required" if args.require_token else "", os.getpid()))
    try:
        httpd.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()


if __name__ == "__main__":
    main()
