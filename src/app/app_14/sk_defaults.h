// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/**
 * @file sk_defaults.h
 * @brief Boat Dash (app_14) built-in default configuration (JSON, parsed by sk::parseConfig).
 *
 * Sources (VesselStack https://github.com/jclima/VesselStack @ 0336a0b):
 *  - server: host "" = discovery, port 3000 (Signal K default), poll 2000 ms: this firmware, not VesselStack.
 *  - instruments sog, depth, house_v, start_v, fuel0, water, waste, batt_soc, batt_a, solar_a:
 *      displays/displays.dashboard.example.json "instruments" (paths, convert, unit, precision,
 *      stale_after_s, thresholds, hysteresis copied; icon/min/max/min_interval_ms dropped).
 *  - fuel1: same as fuel0 with tanks.fuel.1 (the example has one fuel tank; the 2nd is new).
 *  - awa, cog, position, eng_*: not in the VesselStack example. Paths are from the Signal K
 *      spec (environment.wind.angleApparent, navigation.courseOverGroundTrue,
 *      navigation.position, propulsion.<id>.revolutions (Hz), propulsion.<id>.temperature (K));
 *      conversions are from displays/conversions.py; stale limits are new.
 *      Oil pressure (propulsion.*.oilPressure, Pa->kPa) is supported by the model but not
 *      shown by default: the engines page is a full 2x2.
 *  - notifications anchor, low_battery: displays.dashboard.example.json "notifications"
 *      (low_battery has "absent": "ok").
 *  - pages: Nav/Tanks mirror the example's page titles; Power/Engines/Status are new.
 *      "special":"status" marks the app-drawn status page (notifications, position, server, data age).
 * Page layout in this firmware is always a 2x2 tile grid.
 */
#pragma once

namespace sk {

inline constexpr char kDefaultConfigJson[] = R"JSON({
  "schema": 1,
  "server": {"host": "", "port": 3000, "token": "", "poll_ms": 2000, "fetch": "tree"},
  "instruments": {
    "sog": {"path": "navigation.speedOverGround", "label": "SOG", "convert": "m/s->kn", "unit": "kn", "precision": 1, "stale_after_s": 10},
    "depth": {"path": "environment.depth.belowTransducer", "label": "Depth", "convert": "m->ft", "unit": "ft", "precision": 1, "stale_after_s": 10, "thresholds": {"alarm_below": 4, "warn_below": 8, "hysteresis": 0.5}},
    "awa": {"path": "environment.wind.angleApparent", "label": "AWA", "convert": "rad->deg_signed", "unit": "deg", "precision": 0, "stale_after_s": 10},
    "cog": {"path": "navigation.courseOverGroundTrue", "label": "COG", "convert": "rad->deg_360", "unit": "deg", "precision": 0, "stale_after_s": 10},
    "house_v": {"path": "electrical.batteries.house.voltage", "label": "House", "convert": "none", "unit": "V", "precision": 2, "stale_after_s": 30, "thresholds": {"alarm_below": 11.8, "warn_below": 12.2, "warn_above": 14.8, "alarm_above": 15.2, "hysteresis": 0.1}},
    "start_v": {"path": "electrical.batteries.start.voltage", "label": "Start", "convert": "none", "unit": "V", "precision": 2, "stale_after_s": 30, "thresholds": {"alarm_below": 11.5, "warn_below": 12.0, "hysteresis": 0.1}},
    "batt_soc": {"path": "electrical.batteries.house.capacity.stateOfCharge", "label": "SoC", "convert": "ratio->%", "unit": "%", "precision": 0, "stale_after_s": 60, "thresholds": {"alarm_below": 20, "warn_below": 30, "hysteresis": 2}},
    "batt_a": {"path": "electrical.batteries.house.current", "label": "Current", "convert": "none", "unit": "A", "precision": 1, "stale_after_s": 30},
    "solar_a": {"path": "electrical.solar.main.current", "label": "Solar", "convert": "none", "unit": "A", "precision": 1, "stale_after_s": 30},
    "fuel0": {"path": "tanks.fuel.0.currentLevel", "label": "Fuel 1", "convert": "ratio->%", "unit": "%", "precision": 0, "stale_after_s": 60, "thresholds": {"alarm_below": 10, "warn_below": 25, "hysteresis": 2}},
    "fuel1": {"path": "tanks.fuel.1.currentLevel", "label": "Fuel 2", "convert": "ratio->%", "unit": "%", "precision": 0, "stale_after_s": 60, "thresholds": {"alarm_below": 10, "warn_below": 25, "hysteresis": 2}},
    "water": {"path": "tanks.freshWater.0.currentLevel", "label": "Water", "convert": "ratio->%", "unit": "%", "precision": 0, "stale_after_s": 60, "thresholds": {"warn_below": 20, "hysteresis": 2}},
    "waste": {"path": "tanks.blackWater.0.currentLevel", "label": "Waste", "convert": "ratio->%", "unit": "%", "precision": 0, "stale_after_s": 60, "thresholds": {"warn_above": 75, "alarm_above": 90, "hysteresis": 2}},
    "eng_p_rpm": {"path": "propulsion.port.revolutions", "label": "Port RPM", "convert": "Hz->rpm", "unit": "rpm", "precision": 0, "stale_after_s": 10},
    "eng_s_rpm": {"path": "propulsion.starboard.revolutions", "label": "Stbd RPM", "convert": "Hz->rpm", "unit": "rpm", "precision": 0, "stale_after_s": 10},
    "eng_p_temp": {"path": "propulsion.port.temperature", "label": "Port temp", "convert": "K->C", "unit": "C", "precision": 0, "stale_after_s": 30},
    "eng_s_temp": {"path": "propulsion.starboard.temperature", "label": "Stbd temp", "convert": "K->C", "unit": "C", "precision": 0, "stale_after_s": 30},
    "position": {"path": "navigation.position", "label": "Position", "convert": "position", "unit": "", "precision": 3, "stale_after_s": 30}
  },
  "notifications": {
    "anchor": {"path": "notifications.navigation.anchor", "label": "Anchor"},
    "low_battery": {"path": "notifications.electrical.batteries.house.voltage", "label": "Low battery", "absent": "ok"}
  },
  "pages": [
    {"title": "Nav", "items": ["sog", "depth", "awa", "cog"]},
    {"title": "Power", "items": ["house_v", "batt_soc", "batt_a", "solar_a"]},
    {"title": "Tanks", "items": ["fuel0", "fuel1", "water", "waste"]},
    {"title": "Engines", "items": ["eng_p_rpm", "eng_s_rpm", "eng_p_temp", "eng_s_temp"]},
    {"title": "Status", "special": "status", "items": ["anchor", "low_battery", "position"]}
  ]
})JSON";

}  // namespace sk
