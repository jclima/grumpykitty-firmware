// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

// Host tests for src/app/app_14/sk_model.{h,cpp}. See tools/host_tests/run.sh.
// Vectors are ported from VesselStack tests/test_displays_conversions.py and
// tests/test_displays_severity.py (commit 0336a0b); the Home Assistant (ha_state) tests
// are not ported because this dashboard has no Home Assistant client, only the
// round_value cases they contain.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#include "../../src/app/app_14/sk_defaults.h"
#include "../../src/app/app_14/sk_model.h"

using namespace sk;

static int g_checks = 0, g_failed = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    g_checks++;                                                         \
    if (!(cond)) {                                                      \
      g_failed++;                                                       \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
    }                                                                   \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                                                          \
  do {                                                                                                 \
    g_checks++;                                                                                        \
    const double _a = (a), _b = (b);                                                                   \
    if (!(std::fabs(_a - _b) <= (tol))) {                                                              \
      g_failed++;                                                                                      \
      std::printf("FAIL %s:%d: %s (%.12g) ~ %s (%.12g)\n", __FILE__, __LINE__, #a, _a, #b, _b);        \
    }                                                                                                  \
  } while (0)

#define CHECK_STR(a, b)                                                                              \
  do {                                                                                               \
    g_checks++;                                                                                      \
    const std::string _a = (a), _b = (b);                                                            \
    if (_a != _b) {                                                                                  \
      g_failed++;                                                                                    \
      std::printf("FAIL %s:%d: %s == \"%s\" expected \"%s\"\n", __FILE__, __LINE__, #a, _a.c_str(), _b.c_str()); \
    }                                                                                                \
  } while (0)

static const double kPi = 3.141592653589793;
static const double kInf = std::numeric_limits<double>::infinity();
static const double kNan = std::numeric_limits<double>::quiet_NaN();

// ---------------------------------------------------------------------------
// conversions
// ---------------------------------------------------------------------------

static double conv(const char* name, double v) {
  double out = kNan;
  if (!convert(name, v, out)) return kNan;
  return out;
}

// Returns the rounded value, or NaN for "None".
static double car(const char* name, double v, int p) {
  double out = kNan;
  if (!convertAndRound(name, v, p, out)) return kNan;
  return out;
}

static double rv(double v, int p) {
  double out = kNan;
  if (!roundValue(v, p, out)) return kNan;
  return out;
}

static bool isNegZero(double v) { return v == 0.0 && std::signbit(v); }

static void testConversionsKnown() {
  struct K {
    const char* name;
    double src, expected;
  } known[] = {
      {"none", 3.5, 3.5},
      {"m/s->kn", 1852.0 / 3600, 1.0},
      {"m/s->km/h", 10, 36},
      {"m/s->mph", 1609.344 / 3600, 1.0},
      {"K->C", 273.15, 0.0},
      {"K->F", 273.15, 32.0},
      {"rad->deg", kPi, 180.0},
      {"rad->deg_signed", kPi / 2, 90.0},
      {"rad->deg_360", kPi / 2, 90.0},
      {"m->ft", 0.3048, 1.0},
      {"m->fathom", 1.8288, 1.0},
      {"m->nmi", 1852, 1.0},
      {"ratio->%", 0.5, 50.0},
      {"Pa->hPa", 101325, 1013.25},
      {"Pa->bar", 100000, 1.0},
      {"Pa->psi", 6894.757293168, 1.0},
      {"m3->L", 0.5, 500.0},
      {"m3->gal", 0.003785411784, 1.0},
      {"m3/s->L/h", 0.001, 3600.0},
      {"m3/s->gal/h", 0.003785411784, 3600.0},
      {"Hz->rpm", 50, 3000},
      {"C->Ah", 3600, 1.0},
      {"s->min", 120, 2.0},
      {"s->h", 7200, 2.0},
      {"negate", 12.5, -12.5},
      {"Pa->kPa", 101325, 101.325},  // addition (oil pressure)
  };
  const size_t n = sizeof known / sizeof known[0];
  CHECK(conversionCount() == n);  // table == VesselStack's 25 + Pa->kPa
  for (const K& k : known) {
    bool in_table = false;
    for (size_t i = 0; i < conversionCount(); i++)
      if (!std::strcmp(conversionName(i), k.name)) in_table = true;
    CHECK(in_table);
    CHECK(isKnownConversion(k.name));
    CHECK_NEAR(conv(k.name, k.src), k.expected, 5e-7);  // assertAlmostEqual(places=6)
  }
  CHECK(isKnownConversion("position"));
  CHECK(!isKnownConversion("bogus"));
  CHECK(!isKnownConversion(nullptr));
  double d;
  CHECK(!convert("bogus", 1, d));
  CHECK(!convert("position", 1, d));
}

static void testNegate() {
  CHECK(conv("negate", -3.2) == 3.2);
  CHECK(car("negate", 0.0, 1) == 0.0 && !isNegZero(car("negate", 0.0, 1)));
  CHECK(car("negate", 0.04, 1) == 0.0 && !isNegZero(car("negate", 0.04, 1)));  // -0.04 rounds to zero
  CHECK(car("negate", 12.34, 1) == -12.3);
  CHECK(car("negate", -12.34, 0) == 12);
  CHECK(std::isnan(car("negate", kInf, 1)));
}

static void testSignedWrap() {
  auto f = [](double deg) { return conv("rad->deg_signed", deg * (kPi / 180.0)); };
  CHECK_NEAR(f(180), 180.0, 5e-7);
  CHECK_NEAR(f(-180), 180.0, 5e-7);
  CHECK_NEAR(f(181), -179.0, 5e-7);
  CHECK_NEAR(f(-181), 179.0, 5e-7);
  CHECK_NEAR(f(360), 0.0, 5e-7);
  CHECK_NEAR(f(540), 180.0, 5e-7);
  CHECK_NEAR(f(-90), -90.0, 5e-7);
}

static void test360Wrap() {
  auto f = [](double deg) { return conv("rad->deg_360", deg * (kPi / 180.0)); };
  CHECK_NEAR(f(0), 0.0, 5e-7);
  CHECK_NEAR(f(-1), 359.0, 5e-7);
  CHECK_NEAR(f(360), 0.0, 5e-6);
  CHECK_NEAR(f(725), 5.0, 5e-7);
  CHECK(conv("rad->deg_360", -1e-18) < 360.0);
}

static void testHeadingRoundingWraps() {
  struct R {
    double deg;
    int p;
    double expected;
  } cases[] = {{359.6, 0, 0}, {359.5, 0, 0}, {359.4, 0, 359}, {359.96, 1, 0.0}, {359.94, 1, 359.9}};
  for (const R& c : cases) {
    const double got = car("rad->deg_360", c.deg * (kPi / 180.0), c.p);
    CHECK(got == c.expected);
    if (got != c.expected) std::printf("  (deg=%g p=%d got=%.17g)\n", c.deg, c.p, got);
  }
}

static void testRounding() {
  CHECK(rv(2.5, 0) == 3);
  CHECK(rv(-2.5, 0) == -3);
  CHECK(rv(6.45, 1) == 6.5);
  CHECK(rv(1.23456, 3) == 1.235);
  CHECK(rv(3.2, 0) == 3);
  CHECK(rv(-0.04, 1) == 0.0 && !isNegZero(rv(-0.04, 1)));
  CHECK(car("m/s->kn", 3.2918, 1) == 6.4);
  // from the Home Assistant rounding tests (same round_value)
  CHECK(rv(-0.3, 0) == 0 && !isNegZero(rv(-0.3, 0)));
  CHECK(rv(0.125, 2) == 0.13);
  CHECK(rv(-0.001, 2) == 0.0 && !isNegZero(rv(-0.001, 2)));
  CHECK(rv(21.46, 1) == 21.5);
  CHECK(rv(12.345, 2) == 12.35);
  CHECK(std::isnan(rv(1e308, 1)));
  // shortest decimal form, not the binary expansion: printf("%.2f", 1.005) is "1.00"
  CHECK(rv(1.005, 2) == 1.01);
  CHECK(rv(2.675, 2) == 2.68);
  CHECK(rv(0.285, 2) == 0.29);
  CHECK(rv(0.5, 0) == 1);
  CHECK(rv(0.4999, 0) == 0);
  CHECK(rv(9.995, 2) == 10.0);   // carry through all 9s
  CHECK(rv(99.5, 0) == 100);
  CHECK(rv(0.000001, 2) == 0.0 && !isNegZero(rv(0.000001, 2)));
  CHECK(rv(0.0, 2) == 0.0 && !isNegZero(rv(-0.0, 2)));
  CHECK(rv(0.1 + 0.2, 1) == 0.3);
  CHECK(rv(123456.789, 1) == 123456.8);
}

static void testRoundOutOfRange() {
  const double bad[] = {1e30, 1e300, -1e300, kInf, -kInf, kNan, 1e15 * 1.0001};
  for (double b : bad) {
    CHECK(std::isnan(rv(b, 1)));
    CHECK(std::isnan(rv(b, 0)));
  }
  CHECK(rv(1e15, 1) == 1e15);
  CHECK(rv(-1e15, 0) == -1e15);
  CHECK(std::isnan(car("m/s->kn", 1e308, 1)));
  CHECK(std::isnan(car("rad->deg_signed", 1e308, 1)));
  CHECK(std::isnan(car("bogus", 1, 1)));
  CHECK(std::isnan(rv(1.0, 10)));
  CHECK(std::isnan(rv(1.0, -1)));
}

static void testPosition() {
  char buf[40];
  CHECK(formatPosition(46.20575, -123.7613, buf, sizeof buf));
  CHECK_STR(buf, "46 12.345'N 123 45.678'W");
  CHECK(formatPosition(-33.8568, 151.2153, buf, sizeof buf));
  CHECK_STR(buf, "33 51.408'S 151 12.918'E");
  CHECK(formatPosition(0, 0, buf, sizeof buf));
  CHECK_STR(buf, "0 00.000'N 0 00.000'E");
  CHECK(formatPosition(10.99999999, 20.0, buf, sizeof buf));  // minutes round to 60.000: carry
  CHECK_STR(buf, "11 00.000'N 20 00.000'E");
  CHECK(!formatPosition(91, 0, buf, sizeof buf));
  CHECK(!formatPosition(0, 181, buf, sizeof buf));
  CHECK(!formatPosition(kNan, 0, buf, sizeof buf));
  char tiny[8];
  CHECK(!formatPosition(46.2, 12.3, tiny, sizeof tiny));
}

// ---------------------------------------------------------------------------
// severity
// ---------------------------------------------------------------------------

static Thresholds full() {
  Thresholds t;
  t.hasAlarmBelow = t.hasWarnBelow = t.hasWarnAbove = t.hasAlarmAbove = true;
  t.alarmBelow = 11.0;
  t.warnBelow = 12.0;
  t.warnAbove = 14.5;
  t.alarmAbove = 15.0;
  return t;
}

static Thresholds withH(double h) {
  Thresholds t = full();
  t.hysteresis = h;
  return t;
}

static Thresholds one(const char* which, double bound, double h = 0) {
  Thresholds t;
  t.hysteresis = h;
  if (!std::strcmp(which, "alarm_below")) { t.hasAlarmBelow = true; t.alarmBelow = bound; }
  if (!std::strcmp(which, "warn_below")) { t.hasWarnBelow = true; t.warnBelow = bound; }
  if (!std::strcmp(which, "warn_above")) { t.hasWarnAbove = true; t.warnAbove = bound; }
  if (!std::strcmp(which, "alarm_above")) { t.hasAlarmAbove = true; t.alarmAbove = bound; }
  return t;
}

static const Sev U = Sev::Unknown, O = Sev::Ok, W = Sev::Warn, A = Sev::Alarm;

static void testBounds() {
  const Thresholds f = full();
  CHECK(numericSev(10.9, f, U) == A);
  CHECK(numericSev(11.5, f, U) == W);
  CHECK(numericSev(13.0, f, U) == O);
  CHECK(numericSev(14.8, f, U) == W);
  CHECK(numericSev(15.5, f, U) == A);
  // equality stays on the safe side
  CHECK(numericSev(11.0, f, U) == W);
  CHECK(numericSev(12.0, f, U) == O);
  CHECK(numericSev(14.5, f, U) == O);
  CHECK(numericSev(15.0, f, U) == W);
  // equal alarm and warn bounds
  Thresholds eq;
  eq.hasAlarmBelow = eq.hasWarnBelow = true;
  eq.alarmBelow = eq.warnBelow = 5;
  CHECK(numericSev(4.9, eq, U) == A);
  CHECK(numericSev(5, eq, U) == O);
  // single sided
  CHECK(numericSev(9, one("warn_below", 10), U) == W);
  CHECK(numericSev(1000, one("warn_below", 10), U) == O);
  CHECK(numericSev(9, one("alarm_below", 10), U) == A);
  CHECK(numericSev(11, one("alarm_below", 10), U) == O);
  CHECK(numericSev(51, one("warn_above", 50), U) == W);
  CHECK(numericSev(-1000, one("warn_above", 50), U) == O);
  CHECK(numericSev(51, one("alarm_above", 50), U) == A);
  CHECK(numericSev(50, one("alarm_above", 50), U) == O);
  // unknown for non-finite
  CHECK(numericSev(kNan, f, U) == U);
  CHECK(numericSev(kInf, f, U) == U);
  CHECK(numericSev(-kInf, f, A) == U);
}

static void testHysteresis() {
  const Thresholds t = withH(0.3);
  // low side recovery
  CHECK(numericSev(11.2, t, A) == A);  // inside the hold zone
  CHECK(numericSev(11.3, t, A) == W);  // cleared alarm_below + hysteresis
  CHECK(numericSev(12.2, t, W) == W);
  CHECK(numericSev(12.3, t, W) == O);
  CHECK(numericSev(12.2, t, A) == W);  // alarm -> warn zone -> still warn
  CHECK(numericSev(12.3, t, A) == O);
  // high side recovery
  CHECK(numericSev(14.8, t, A) == A);
  CHECK(numericSev(14.7, t, A) == W);
  CHECK(numericSev(14.3, t, W) == W);
  CHECK(numericSev(14.2, t, W) == O);
  // getting worse is immediate
  CHECK(numericSev(11.9, t, O) == W);
  CHECK(numericSev(10.9, t, O) == A);
  CHECK(numericSev(10.9, t, W) == A);
  CHECK(numericSev(15.1, t, O) == A);
  // no hysteresis without history
  const Sev hist[] = {U, U, O};  // None, "unknown", "ok"
  for (Sev prev : hist) {
    CHECK(numericSev(11.2, t, prev) == W);
    CHECK(numericSev(12.2, t, prev) == O);
  }
  // single bounds
  CHECK(numericSev(49, one("alarm_above", 50, 2), A) == A);
  CHECK(numericSev(48, one("alarm_above", 50, 2), A) == O);
  CHECK(numericSev(10.5, one("warn_below", 10, 1), W) == W);
  CHECK(numericSev(11, one("warn_below", 10, 1), W) == O);
  // decimal boundaries are not float noise: 12.0 + 0.1 must not be 12.100000000000001
  CHECK(numericSev(12.1, one("warn_below", 12.0, 0.1), W) == O);
  // zero hysteresis is plain bounds
  CHECK(numericSev(12.0, withH(0), W) == O);
}

static void testNotifications() {
  CHECK(notificationStateSev("normal") == O);
  CHECK(notificationStateSev("nominal") == O);
  CHECK(notificationStateSev("alert") == W);
  CHECK(notificationStateSev("warn") == W);
  CHECK(notificationStateSev("alarm") == A);
  CHECK(notificationStateSev("emergency") == A);
  CHECK(notificationStateSev("Alarm") == U);  // case sensitive, as in VesselStack
  CHECK(notificationStateSev("") == U);
  CHECK(notificationStateSev("bogus") == U);
  CHECK(notificationStateSev(nullptr) == U);

  JsonDocument d;
  deserializeJson(d, R"({"state":"warn","message":"alarm"})");
  CHECK(notificationSev(d.as<JsonVariantConst>(), true) == W);  // message is never read
  CHECK(notificationSev(d.as<JsonVariantConst>(), false) == W);
  CHECK(notificationSev(JsonVariantConst(), true) == O);        // absent: ok
  CHECK(notificationSev(JsonVariantConst(), false) == U);       // absent: unknown
  deserializeJson(d, "null");
  CHECK(notificationSev(d.as<JsonVariantConst>(), true) == O);
  CHECK(notificationSev(d.as<JsonVariantConst>(), false) == U);
  deserializeJson(d, R"("alarm")");
  CHECK(notificationSev(d.as<JsonVariantConst>(), true) == U);  // not an object
  deserializeJson(d, R"({"state":3})");
  CHECK(notificationSev(d.as<JsonVariantConst>(), true) == U);
  deserializeJson(d, R"({"message":"x"})");
  CHECK(notificationSev(d.as<JsonVariantConst>(), true) == U);
  deserializeJson(d, R"({"state":"emergency"})");
  CHECK(notificationSev(d.as<JsonVariantConst>(), true) == A);
}

// ---------------------------------------------------------------------------
// time
// ---------------------------------------------------------------------------

static void testTime() {
  int64_t t = -1;
  CHECK(parseIso8601("1970-01-01T00:00:00Z", t) && t == 0);
  CHECK(parseIso8601("2000-02-29T12:30:45.123Z", t) && t == 951827445123LL);
  CHECK(parseIso8601("2026-11-02T10:00:00.000Z", t) && t == 1793613600000LL);
  int64_t u;
  CHECK(parseIso8601("2026-11-02T10:00:00Z", u) && u == t);
  CHECK(parseIso8601("2026-11-02T10:00:00", u) && u == t);               // no zone = UTC
  CHECK(parseIso8601("2026-11-02T12:00:00+02:00", u) && u == t);
  CHECK(parseIso8601("2026-11-02T05:30:00-0430", u) && u == t);
  CHECK(parseIso8601("2026-11-02T10:00:00.1Z", u) && u == t + 100);
  CHECK(parseIso8601("2026-11-02T10:00:00.123456Z", u) && u == t + 123);  // truncated to ms
  CHECK(!parseIso8601("", u));
  CHECK(!parseIso8601(nullptr, u));
  CHECK(!parseIso8601("2026-13-02T10:00:00Z", u));
  CHECK(!parseIso8601("2026-02-30T10:00:00Z", u));
  CHECK(!parseIso8601("2025-02-29T10:00:00Z", u));
  CHECK(!parseIso8601("2026-11-02T24:00:00Z", u));
  CHECK(!parseIso8601("2026-11-02T10:60:00Z", u));
  CHECK(!parseIso8601("2026-11-02T10:00:00Zjunk", u));
  CHECK(!parseIso8601("2026-11-02 10:00", u));
  CHECK(!parseIso8601("2026-11-02T10:00:00.Z", u));
  CHECK(!parseIso8601("yesterday", u));

  CHECK(parseHttpDate("Sun, 06 Nov 1994 08:49:37 GMT", u) && u == 784111777000LL);
  CHECK(parseHttpDate("06 Nov 1994 08:49:37 GMT", u) && u == 784111777000LL);  // weekday optional
  CHECK(parseHttpDate("Mon, 02 Nov 2026 10:00:00 GMT", u) && u == t);
  CHECK(parseHttpDate("Mon, 02 Nov 2026 10:00:00 UTC", u) && u == t);
  CHECK(!parseHttpDate("Mon, 02 Nov 2026 10:00:00 PST", u));
  CHECK(!parseHttpDate("Mon, 02 Foo 2026 10:00:00 GMT", u));
  CHECK(!parseHttpDate("Mon, 32 Nov 2026 10:00:00 GMT", u));
  CHECK(!parseHttpDate("Mon, 02 Nov 2026 25:00:00 GMT", u));
  CHECK(!parseHttpDate("2026-11-02T10:00:00Z", u));
  CHECK(!parseHttpDate("", u));
  CHECK(!parseHttpDate(nullptr, u));
}

// ---------------------------------------------------------------------------
// config
// ---------------------------------------------------------------------------

static bool parse(const std::string& json, Config& cfg, std::string& err) {
  return parseConfig(json.data(), json.size(), cfg, err);
}

// A tiny valid config, with `extra` spliced into the instrument "x".
static std::string mini(const std::string& instr = R"("x":{"path":"a.b","label":"X","convert":"none","unit":"V","precision":1,"stale_after_s":10})",
                        const std::string& rest = "") {
  return R"({"schema":1,"instruments":{)" + instr + "}" + rest + "}";
}

static void expectError(const std::string& json, const std::string& needle) {
  Config c;
  std::string err;
  const bool ok = parse(json, c, err);
  g_checks++;
  if (ok || err.find(needle) == std::string::npos) {
    g_failed++;
    std::printf("FAIL config error: wanted \"%s\", got ok=%d err=\"%s\"\n   in: %s\n", needle.c_str(), ok, err.c_str(), json.c_str());
  }
}

static void testConfigDefaults() {
  Config c;
  std::string err;
  const bool ok = parseConfig(kDefaultConfigJson, std::strlen(kDefaultConfigJson), c, err);
  CHECK(ok);
  if (!ok) std::printf("  default config error: %s\n", err.c_str());
  CHECK(c.server.host.empty());
  CHECK(c.server.port == 3000);
  CHECK(c.server.token.empty());
  CHECK(c.server.pollMs == 2000);
  CHECK(c.server.fetch == Fetch::Tree);
  CHECK(c.instruments.size() == 18);
  CHECK(c.notifications.size() == 2);
  CHECK(c.itemCount() == 20);
  CHECK(c.itemCount() <= kMaxItems);
  CHECK(c.pages.size() == 5);
  CHECK_STR(c.pages[0].title, "Nav");
  CHECK(c.pages[0].items.size() == 4 && c.pages[0].items[2] == "awa");
  CHECK(c.pages[4].status && c.pages[4].items.size() == 3);
  CHECK(c.findItem("sog") == 0);
  CHECK(c.findItem("anchor") == 18);
  CHECK(c.findItem("nope") == -1);
  // spot-check values copied from displays.dashboard.example.json
  const Instrument& depth = c.instruments[1];
  CHECK_STR(depth.id, "depth");
  CHECK_STR(depth.path, "environment.depth.belowTransducer");
  CHECK_STR(depth.convert, "m->ft");
  CHECK(depth.hasThresholds && depth.th.hasAlarmBelow && depth.th.alarmBelow == 4 && depth.th.warnBelow == 8);
  CHECK(depth.th.hysteresis == 0.5 && depth.staleAfterS == 10 && depth.precision == 1);
  const Instrument& hv = c.instruments[4];
  CHECK_STR(hv.id, "house_v");
  CHECK(hv.th.alarmBelow == 11.8 && hv.th.warnBelow == 12.2 && hv.th.warnAbove == 14.8 && hv.th.alarmAbove == 15.2);
  CHECK(hv.th.hysteresis == 0.1 && hv.staleAfterS == 30 && hv.precision == 2);
  CHECK(c.instruments[9].path == "tanks.fuel.0.currentLevel" && c.instruments[10].path == "tanks.fuel.1.currentLevel");
  CHECK(c.instruments[9].staleAfterS == 60 && c.instruments[9].th.hysteresis == 2);
  CHECK(c.instruments[12].th.hasWarnAbove && c.instruments[12].th.warnAbove == 75 && c.instruments[12].th.alarmAbove == 90);
  CHECK(c.notifications[1].absentOk);
  // every page item resolves
  for (const Page& p : c.pages)
    for (const std::string& id : p.items) CHECK(c.findItem(id) >= 0);
}

static void testConfigParse() {
  Config c;
  std::string err;
  CHECK(parse(mini(), c, err));
  CHECK(c.server.port == 3000 && c.server.pollMs == 2000 && c.server.fetch == Fetch::Tree);  // server defaults
  CHECK(c.pages.empty() && c.notifications.empty());
  // all server fields
  CHECK(parse(R"({"schema":1,"server":{"host":"boat.local","port":3443,"token":"abc","poll_ms":5000,"fetch":"paths"}})", c, err));
  CHECK_STR(c.server.host, "boat.local");
  CHECK(c.server.port == 3443 && c.server.pollMs == 5000 && c.server.fetch == Fetch::Paths);
  CHECK_STR(c.server.token, "abc");
  // ignored VesselStack keys are accepted
  CHECK(parse(mini(R"("x":{"path":"a.b","label":"X","convert":"none","unit":"V","precision":1,"stale_after_s":10,"icon":"speed","min_interval_ms":500,"min":0,"max":16,"alarm":false})"), c, err));
  // notification absent modes
  CHECK(parse(R"({"schema":1,"notifications":{"n":{"path":"notifications.a.b","label":"N","absent":"unknown"},"m":{"path":"notifications.c.d","label":"M"}}})", c, err));
  CHECK(!c.notifications[0].absentOk && c.notifications[1].absentOk);
  // special page
  CHECK(parse(mini(R"("x":{"path":"a.b","label":"X","convert":"none","unit":"V","precision":1,"stale_after_s":10})",
                   R"(,"pages":[{"title":"S","special":"status","items":["x"]},{"title":"T","items":[]}])"), c, err));
  CHECK(c.pages.size() == 2 && c.pages[0].status && !c.pages[1].status);
  // 32 items ok, 33 not
  std::string many = R"({"schema":1,"instruments":{)";
  for (int i = 0; i < 32; i++) {
    if (i) many += ",";
    many += "\"i" + std::to_string(i) + R"(":{"path":"a.b","label":"X","convert":"none","unit":"V","precision":1,"stale_after_s":10})";
  }
  CHECK(parse(many + "}}", c, err));
  CHECK(c.instruments.size() == 32);
  many += R"(,"i32":{"path":"a.b","label":"X","convert":"none","unit":"V","precision":1,"stale_after_s":10})" + std::string("}}");
  expectError(many, "at most 32");
}

static void testConfigErrors() {
  const std::string base = R"("path":"a.b","label":"X","convert":"none","unit":"V","precision":1,"stale_after_s":10)";
  auto inst = [&](const std::string& patch) {  // replace-or-add a key in the instrument body
    return mini("\"x\":{" + patch + "}");
  };
  expectError("", "JSON error");
  expectError("[1]", "$: config must be an object");
  expectError(R"({"schema":2})", "$.schema: must be 1");
  expectError(R"({})", "$.schema");
  expectError(R"({"schema":1,"bogus":1})", "unknown key \"bogus\"");
  expectError(R"({"schema":1,"server":5})", "$.server: must be an object");
  expectError(R"({"schema":1,"server":{"port":0}})", "$.server.port");
  expectError(R"({"schema":1,"server":{"port":70000}})", "$.server.port");
  expectError(R"({"schema":1,"server":{"port":"3000"}})", "$.server.port");
  expectError(R"({"schema":1,"server":{"poll_ms":100}})", "$.server.poll_ms");
  expectError(R"({"schema":1,"server":{"fetch":"all"}})", "$.server.fetch");
  expectError(R"({"schema":1,"server":{"host":5}})", "$.server.host");
  expectError(R"({"schema":1,"server":{"hostt":"x"}})", "$.server: unknown key");
  expectError(R"({"schema":1,"instruments":[]})", "$.instruments: must be an object");
  expectError(R"({"schema":1,"instruments":{"X":{}}})", "instrument id must match");
  expectError(R"({"schema":1,"instruments":{"x":5}})", "$.instruments.x: must be an object");
  expectError(inst(R"("label":"X","convert":"none","unit":"V","precision":1,"stale_after_s":10)"), "$.instruments.x.path: missing");
  expectError(inst(R"("path":"a","label":"X","convert":"none","unit":"V","precision":1,"stale_after_s":10)"), "dotted SignalK path");
  expectError(inst(R"("path":"1a.b","label":"X","convert":"none","unit":"V","precision":1,"stale_after_s":10)"), "dotted SignalK path");
  expectError(inst(R"("path":"a..b","label":"X","convert":"none","unit":"V","precision":1,"stale_after_s":10)"), "dotted SignalK path");
  expectError(inst(R"("path":"a.b","label":"","convert":"none","unit":"V","precision":1,"stale_after_s":10)"), ".label");
  expectError(inst(R"("path":"a.b","label":"12345678901234567","convert":"none","unit":"V","precision":1,"stale_after_s":10)"), ".label");
  expectError(inst(R"("path":"a.b","label":"X","convert":"furlongs","unit":"V","precision":1,"stale_after_s":10)"), "unknown conversion \"furlongs\"");
  expectError(inst(R"("path":"a.b","label":"X","convert":"none","unit":"123456789","precision":1,"stale_after_s":10)"), ".unit");
  expectError(inst(R"("path":"a.b","label":"X","convert":"none","unit":"V","precision":4,"stale_after_s":10)"), ".precision");
  expectError(inst(R"("path":"a.b","label":"X","convert":"none","unit":"V","precision":1.5,"stale_after_s":10)"), ".precision");
  expectError(inst(R"("path":"a.b","label":"X","convert":"none","unit":"V","precision":1)"), ".stale_after_s");
  expectError(inst(R"("path":"a.b","label":"X","convert":"none","unit":"V","precision":1,"stale_after_s":3601)"), ".stale_after_s");
  expectError(inst(R"("path":"a.b","label":"X","convert":"none","unit":"V","precision":1,"stale_after_s":3)"), "2 x server.poll_ms");
  expectError(inst(base + R"(,"wat":1)"), "$.instruments.x: unknown key \"wat\"");
  // thresholds
  auto th = [&](const std::string& t) { return inst(base + R"(,"thresholds":)" + t); };
  expectError(th("5"), ".thresholds: must be an object");
  expectError(th("{}"), "needs at least one of");
  expectError(th(R"({"hysteresis":1})"), "needs at least one of");
  expectError(th(R"({"warn_below":"a"})"), ".thresholds.warn_below: must be a finite number");
  expectError(th(R"({"alarm_below":9,"warn_below":8})"), "alarm_below must not be greater than warn_below");
  expectError(th(R"({"warn_above":9,"alarm_above":8})"), "warn_above must not be greater than alarm_above");
  expectError(th(R"({"warn_below":9,"warn_above":9})"), "warn_below must be less than warn_above");
  expectError(th(R"({"warn_below":9,"hysteresis":-1})"), "hysteresis: must be a finite number >= 0");
  expectError(th(R"({"alarm_below":4,"warn_below":8,"hysteresis":4})"), "less than the smallest gap");
  expectError(th(R"({"alarm_below":4,"warn_below":8,"hysteresis":1,"nope":1})"), "unknown key \"nope\"");
  Config c;
  std::string err;
  CHECK(parse(th(R"({"alarm_below":4,"warn_below":8,"hysteresis":3.9})"), c, err));
  CHECK(parse(th(R"({"warn_below":8,"hysteresis":100})"), c, err));  // single bound: no gap, any h >= 0
  // notifications
  const std::string nbase = R"("path":"notifications.a.b","label":"N")";
  auto note = [&](const std::string& body) { return R"({"schema":1,"notifications":{"n":{)" + body + "}}}"; };
  expectError(note(R"("path":"a.b","label":"N")"), "starting with \"notifications.\"");
  expectError(note(nbase + R"(,"absent":"maybe")"), ".absent");
  expectError(note(R"("label":"N")"), ".path: missing");
  expectError(note(nbase + R"(,"x":1)"), "unknown key");
  expectError(mini("", "").substr(0, 0) + R"({"schema":1,"instruments":{"n":{)" + base + R"(}},"notifications":{"n":{)" + nbase + "}}}", "already used by an instrument");
  // pages
  auto pages = [&](const std::string& p) { return mini(R"("x":{)" + base + "}", ",\"pages\":" + p); };
  expectError(pages("{}"), "$.pages: must be an array");
  expectError(pages(R"([{"title":"A","items":["nope"]}])"), "$.pages[0].items[0]: unknown item \"nope\"");
  expectError(pages(R"([{"items":["x"]}])"), "$.pages[0].title");
  expectError(pages(R"([{"title":"A"}])"), "$.pages[0].items");
  expectError(pages(R"([{"title":"A","items":["x"],"special":"other"}])"), "special");
  expectError(pages(R"([{"title":"A","items":[1]}])"), "must be a string");
  expectError(pages(R"([{"title":"A","items":["x","x","x","x","x","x","x","x","x"]}])"), "at most 8 items");
  expectError(pages(R"([{"title":"A","items":[]},{"title":"A","items":[]},{"title":"A","items":[]},{"title":"A","items":[]},{"title":"A","items":[]},{"title":"A","items":[]},{"title":"A","items":[]},{"title":"A","items":[]},{"title":"A","items":[]}])"), "at most 8 pages");
}

static void testConfigSyntaxPosition() {
  Config c;
  std::string err;
  const std::string bad = "{\"schema\": 1,\n \"server\": {\"port\": }}";
  CHECK(!parse(bad, c, err));
  CHECK(err.find("JSON error") == 0);
  CHECK(err.find("line 2") != std::string::npos);
  std::printf("  (syntax error message: %s)\n", err.c_str());
  CHECK(!parse("{\"schema\": 1,\n\n\n  \"x\": [1, 2,", c, err));
  CHECK(err.find("line 4") != std::string::npos);
  CHECK(err.find("(byte ") != std::string::npos);
  CHECK(!parse("{\"schema\": 1, \"a\": nul}", c, err));
  CHECK(err.find("line 1, column ") != std::string::npos);
  // a failed parse leaves the output empty
  Config keep;
  keep.pages.push_back(Page());
  CHECK(!parse("{", keep, err));
  CHECK(keep.pages.empty() && keep.instruments.empty());
}

// ---------------------------------------------------------------------------
// filter, extraction, tracker
// ---------------------------------------------------------------------------

static const char* kTree = R"({
  "uuid": "urn:mrn:signalk:uuid:c0d79334-4e25-4245-8892-54e8ccc8021d",
  "name": "Test Boat",
  "mmsi": "123456789",
  "navigation": {
    "speedOverGround": {
      "meta": {"units": "m/s", "description": "Vessel speed over ground", "zones": [{"upper": 1, "state": "alarm"}]},
      "value": 3.2918,
      "$source": "can0.5",
      "timestamp": "2026-11-02T10:00:00.000Z",
      "values": {"can0.5": {"value": 3.2918, "timestamp": "2026-11-02T10:00:00.000Z"}, "gps.GP": {"value": 3.3, "timestamp": "2026-11-02T09:59:59.000Z"}}
    },
    "courseOverGroundTrue": {"value": 6.2, "$source": "can0.5", "timestamp": "2026-11-02T10:00:00.000Z"},
    "position": {"value": {"longitude": -123.7613, "latitude": 46.20575}, "$source": "gps.GP", "timestamp": "2026-11-02T10:00:00.000Z",
                 "meta": {"x": {"y": {"z": 1}}}}
  },
  "environment": {
    "depth": {"belowTransducer": {"value": 2.1, "$source": "can0.9", "timestamp": "2026-11-02T10:00:00.000Z"}},
    "wind": {"angleApparent": {"value": -0.5, "$source": "can0.9", "timestamp": "2026-11-02T10:00:00.000Z"}}
  },
  "electrical": {
    "batteries": {
      "house": {
        "voltage": {"value": 12.6, "$source": "vedirect", "timestamp": "2026-11-02T10:00:00.000Z"},
        "current": {"value": null, "$source": "vedirect", "timestamp": "2026-11-02T10:00:00.000Z"},
        "capacity": {"stateOfCharge": {"value": 0.874, "$source": "vedirect", "timestamp": "2026-11-02T10:00:00.000Z"}}
      },
      "start": {"voltage": {"value": "12.5", "$source": "vedirect", "timestamp": "2026-11-02T10:00:00.000Z"}}
    }
  },
  "tanks": {
    "fuel": {
      "0": {"currentLevel": {"value": 0.62, "$source": "tank", "timestamp": "2026-11-02T10:00:00.000Z"}, "capacity": {"value": 0.2}},
      "1": {"currentLevel": {"value": 0.08, "$source": "tank", "timestamp": "2026-11-02T10:00:00.000Z"}}
    },
    "freshWater": {"0": {"currentLevel": {"value": 0.5, "timestamp": "2026-11-02T09:00:00.000Z"}}}
  },
  "propulsion": {
    "port": {"revolutions": {"value": 25.0, "timestamp": "2026-11-02T10:00:00.000Z"}, "temperature": {"value": 355.15, "timestamp": "2026-11-02T10:00:00.000Z"},
             "oilPressure": {"value": 300000, "timestamp": "2026-11-02T10:00:00.000Z"}}
  },
  "notifications": {
    "navigation": {"anchor": {"value": {"state": "alarm", "method": ["visual", "sound"], "message": "Anchor dragging"}, "timestamp": "2026-11-02T10:00:00.000Z", "$source": "anchoralarm"}},
    "electrical": {"batteries": {"house": {"voltage": {"value": null, "timestamp": "2026-11-02T10:00:00.000Z"}}}}
  },
  "sources": {"can0": {"5": {"label": "x"}}}
})";

static Config defaultConfig() {
  Config c;
  std::string err;
  if (!parseConfig(kDefaultConfigJson, std::strlen(kDefaultConfigJson), c, err)) std::printf("default config: %s\n", err.c_str());
  return c;
}

static double num(const Reading& r) {
  double d = kNan;
  jsonNumber(r.value, d);
  return d;
}
static double num2(JsonVariantConst v) {
  double d = kNan;
  jsonNumber(v, d);
  return d;
}

static void testJsonNumber() {
  JsonDocument d;
  double x = kNan;
  // float32-path literals come back as the decimal that was written
  const char* lits[] = {"11.8", "12.2", "14.8", "15.2", "0.1", "6.45", "12.355", "3.2918", "0.874", "46.20575", "123.7613", "-0.285", "0.62", "300.5", "355.15", "1e-3", "25.0"};
  for (const char* l : lits) {
    deserializeJson(d, l);
    CHECK(jsonNumber(d.as<JsonVariantConst>(), x));
    CHECK(x == std::strtod(l, nullptr));
  }
  deserializeJson(d, "[12, -3, 0, 1234567890123, 0.30000000000000004, 3.141592653589793]");
  JsonArrayConst a = d.as<JsonArrayConst>();
  CHECK(jsonNumber(a[0], x) && x == 12);
  CHECK(jsonNumber(a[1], x) && x == -3);
  CHECK(jsonNumber(a[2], x) && x == 0);
  CHECK(jsonNumber(a[3], x) && x == 1234567890123.0);
  CHECK(jsonNumber(a[4], x) && std::fabs(x - 0.30000000000000004) < 1e-15);  // double path: untouched
  CHECK(jsonNumber(a[5], x) && std::fabs(x - 3.141592653589793) < 1e-15);
  deserializeJson(d, R"(["1", true, null, {}, []])");
  a = d.as<JsonArrayConst>();
  for (JsonVariantConst v : a) CHECK(!jsonNumber(v, x));
  CHECK(!jsonNumber(JsonVariantConst(), x));
}

static void testThroughJson() {
  // VesselStack vectors, but with the numbers arriving through ArduinoJson (float32 path)
  static const char kCfg[] = R"({"schema":1,"instruments":{
    "v":{"path":"a.v","label":"V","convert":"none","unit":"V","precision":1,"stale_after_s":10,
         "thresholds":{"alarm_below":11.8,"warn_below":12.2,"hysteresis":0.1}},
    "w":{"path":"a.w","label":"W","convert":"none","unit":"V","precision":2,"stale_after_s":10},
    "u":{"path":"a.u","label":"U","convert":"none","unit":"V","precision":1,"stale_after_s":10}
  }})";
  Config cfg;
  std::string err;
  CHECK(parseConfig(kCfg, std::strlen(kCfg), cfg, err));
  CHECK(cfg.instruments[0].th.alarmBelow == 11.8 && cfg.instruments[0].th.warnBelow == 12.2 && cfg.instruments[0].th.hysteresis == 0.1);
  Tracker t;
  t.begin(cfg);
  Snapshot s;
  JsonDocument d;
  uint32_t now = 1000;
  auto feed = [&](size_t i, const char* val, const char* ts) {
    const std::string j = std::string("{\"value\":") + val + ",\"timestamp\":\"" + ts + "\"}";
    deserializeJson(d, j);
    t.applyInstrument(i, readNode(d.as<JsonVariantConst>()), now, -1);
    t.snapshot(now, s);
    now += 1000;
  };
  feed(2, "6.45", "1");                                    // precision 1: 6.45 -> 6.5, not 6.4
  CHECK_STR(s.items[2].display, "6.5");
  feed(0, "12.2", "1");                                    // equal to warn_below: ok
  CHECK(s.items[0].sev == Sev::Ok);
  feed(0, "11.8", "2");                                    // equal to alarm_below: warn, not alarm
  CHECK(s.items[0].sev == Sev::Warn);
  feed(0, "11.7", "3");
  CHECK(s.items[0].sev == Sev::Alarm);
  feed(0, "11.8", "4");                                    // hold zone: alarm_below + 0.1 = 11.9
  CHECK(s.items[0].sev == Sev::Alarm);
  feed(0, "11.9", "5");                                    // cleared the hold zone: warn
  CHECK(s.items[0].sev == Sev::Warn);
  feed(0, "12.2", "6");                                    // warn hold zone: 12.2 + 0.1 = 12.3
  CHECK(s.items[0].sev == Sev::Warn);
  feed(0, "12.3", "7");
  CHECK(s.items[0].sev == Sev::Ok);
  feed(1, "12.355", "1");                                  // 2 dp tie: 12.36 (float32 would give 12.35)
  CHECK_STR(s.items[1].display, "12.36");
  feed(1, "0.125", "2");
  CHECK_STR(s.items[1].display, "0.13");
  feed(1, "-0.004", "3");
  CHECK_STR(s.items[1].display, "0.00");                   // never "-0.00"
}

static void testFilterAndExtraction() {
  const Config cfg = defaultConfig();
  JsonDocument filter;
  CHECK(buildFilter(cfg, filter));
  std::string fs;
  serializeJson(filter, fs);
  // structure of the filter
  CHECK(filter["navigation"]["speedOverGround"]["value"].as<bool>());
  CHECK(filter["navigation"]["speedOverGround"]["timestamp"].as<bool>());
  CHECK(filter["tanks"]["fuel"]["0"]["currentLevel"]["value"].as<bool>());
  CHECK(filter["tanks"]["fuel"]["1"]["currentLevel"]["timestamp"].as<bool>());
  CHECK(filter["notifications"]["navigation"]["anchor"]["value"]["state"].as<bool>());
  CHECK(filter["notifications"]["navigation"]["anchor"]["timestamp"].as<bool>());
  CHECK(filter["electrical"]["batteries"]["house"]["capacity"]["stateOfCharge"]["value"].as<bool>());
  CHECK(fs.find("meta") == std::string::npos && fs.find("$source") == std::string::npos);

  JsonDocument doc;
  const DeserializationError de = parseTree(kTree, std::strlen(kTree), filter, doc);
  CHECK(!de);
  if (de) std::printf("  parseTree: %s\n", de.c_str());
  std::string out;
  serializeJson(doc, out);
  // noise is gone, wanted data is there
  CHECK(out.find("$source") == std::string::npos);
  CHECK(out.find("\"meta\"") == std::string::npos);
  CHECK(out.find("\"values\"") == std::string::npos);
  CHECK(out.find("uuid") == std::string::npos && out.find("sources") == std::string::npos);
  CHECK(out.find("Anchor dragging") == std::string::npos && out.find("method") == std::string::npos);
  CHECK(out.find("capacity\":{\"value\":0.2") == std::string::npos);  // tanks.fuel.0.capacity is not configured
  CHECK(out.find("oilPressure") == std::string::npos);
  if (out.size() >= std::strlen(kTree) * 3 / 5) std::printf("  filtered (%zu of %zu bytes): %s\n", out.size(), std::strlen(kTree), out.c_str());
  CHECK(out.size() < std::strlen(kTree) * 3 / 5);

  JsonVariantConst root = doc.as<JsonVariantConst>();
  Reading r = lookup(root, "navigation.speedOverGround");
  CHECK(r.present && num(r) == 3.2918);
  CHECK(r.timestamp && std::string(r.timestamp) == "2026-11-02T10:00:00.000Z");
  r = lookup(root, "tanks.fuel.0.currentLevel");  // numeric segments are object keys
  CHECK(r.present && num(r) == 0.62);
  r = lookup(root, "tanks.fuel.1.currentLevel");
  CHECK(r.present && num(r) == 0.08);
  r = lookup(root, "electrical.batteries.house.capacity.stateOfCharge");
  CHECK(r.present && num(r) == 0.874);
  r = lookup(root, "electrical.batteries.house.current");  // value: null
  CHECK(!r.present);
  r = lookup(root, "propulsion.starboard.revolutions");     // absent branch
  CHECK(!r.present && r.timestamp == nullptr);
  r = lookup(root, "navigation.speedOverGround.value.deeper");
  CHECK(!r.present);
  r = lookup(root, "tanks.fuel.0.currentLevel.value");     // walking into a scalar
  CHECK(!r.present);
  r = lookup(root, "navigation.position");
  CHECK(r.present && num2(r.value["latitude"]) == 46.20575);
  r = lookup(root, "notifications.navigation.anchor");
  CHECK(r.present && std::string(r.value["state"].as<const char*>()) == "alarm");
  r = lookup(root, "notifications.electrical.batteries.house.voltage");
  CHECK(!r.present);  // value null: cleared
  r = lookup(JsonVariantConst(), "a.b");
  CHECK(!r.present);
  // per-path body
  JsonDocument leaf;
  deserializeJson(leaf, R"({"value":1.5,"$source":"x","timestamp":"2026-11-02T10:00:00.000Z","meta":{}})");
  r = readNode(leaf.as<JsonVariantConst>());
  CHECK(r.present && num(r) == 1.5 && r.timestamp);
  deserializeJson(leaf, "5");
  CHECK(!readNode(leaf.as<JsonVariantConst>()).present);

  // a parse error is reported, not crashed on
  JsonDocument bad;
  CHECK(parseTree("{\"navigation\":", 14, filter, bad) != DeserializationError::Ok);
}

static const int64_t kT0 = 1793613600000LL;  // 2026-11-02T10:00:00Z

static Reading mk(JsonDocument& d, const char* json) {
  deserializeJson(d, json);
  return readNode(d.as<JsonVariantConst>());
}

static const SnapItem& item(const Snapshot& s, const Config& c, const char* id) {
  static const SnapItem none;
  const int i = c.findItem(id);
  return i < 0 ? none : s.items[i];
}

static void testTrackerTree() {
  const Config cfg = defaultConfig();
  JsonDocument filter, doc;
  buildFilter(cfg, filter);
  parseTree(kTree, std::strlen(kTree), filter, doc);
  Tracker t;
  t.begin(cfg);
  Snapshot s;
  t.snapshot(0, s);
  CHECK(s.count == cfg.itemCount() && !s.connected);
  CHECK(item(s, cfg, "sog").missing && item(s, cfg, "sog").sev == Sev::Unknown);
  CHECK_STR(item(s, cfg, "sog").display, "--");
  CHECK(item(s, cfg, "anchor").missing && item(s, cfg, "anchor").notification);  // never connected

  const uint32_t now = 100000;
  t.applyTree(doc.as<JsonVariantConst>(), now, kT0 + 1000);  // Date header 1 s after the timestamps
  t.snapshot(now, s);
  CHECK(s.connected);
  const SnapItem& sog = item(s, cfg, "sog");
  CHECK(!sog.missing && !sog.stale && sog.value == 6.4);
  CHECK_STR(sog.display, "6.4");
  CHECK_STR(sog.unit, "kn");
  CHECK(sog.ageS == 1 && !sog.hasThresholds && sog.sev == Sev::Ok);
  CHECK_STR(item(s, cfg, "depth").display, "6.9");  // 2.1 m -> 6.889 ft
  CHECK(item(s, cfg, "depth").sev == Sev::Warn);     // 6.9 ft: below warn 8, above alarm 4
  CHECK_STR(item(s, cfg, "awa").display, "-29");     // -0.5 rad
  CHECK_STR(item(s, cfg, "cog").display, "355");     // 6.2 rad
  CHECK_STR(item(s, cfg, "house_v").display, "12.60");
  CHECK(item(s, cfg, "house_v").sev == Sev::Ok);
  CHECK_STR(item(s, cfg, "batt_soc").display, "87");
  CHECK(item(s, cfg, "batt_a").missing);              // value null
  CHECK(item(s, cfg, "start_v").missing);             // value is a string: not a reading
  CHECK_STR(item(s, cfg, "fuel0").display, "62");
  CHECK_STR(item(s, cfg, "fuel1").display, "8");
  CHECK(item(s, cfg, "fuel1").sev == Sev::Alarm);     // 8% < 10
  CHECK_STR(item(s, cfg, "eng_p_rpm").display, "1500");
  CHECK_STR(item(s, cfg, "eng_p_temp").display, "82");
  CHECK(item(s, cfg, "eng_s_rpm").missing);
  CHECK_STR(item(s, cfg, "position").display, "46 12.345'N 123 45.678'W");
  CHECK(item(s, cfg, "position").value == 0);
  CHECK(item(s, cfg, "anchor").notification && item(s, cfg, "anchor").sev == Sev::Alarm);
  CHECK_STR(item(s, cfg, "anchor").display, "ALARM");
  CHECK(!item(s, cfg, "anchor").missing && !item(s, cfg, "anchor").stale);
  CHECK(item(s, cfg, "low_battery").sev == Sev::Ok);  // absent: ok
  // water: timestamp 09:00, Date 10:00:01 -> 3601 s old on first sight, limit 60 -> stale at once
  CHECK(item(s, cfg, "water").stale && item(s, cfg, "water").sev == Sev::Unknown && item(s, cfg, "water").ageS == 3601);

  // disconnect: notifications forgotten and stale, instruments keep their own clocks
  t.setConnected(false);
  t.snapshot(now + 1000, s);
  CHECK(!s.connected);
  CHECK(item(s, cfg, "anchor").stale && item(s, cfg, "anchor").sev == Sev::Unknown && !item(s, cfg, "anchor").missing);
  CHECK(!item(s, cfg, "sog").stale);
  // reconnect with the notification gone: absent: ok
  JsonDocument empty;
  deserializeJson(empty, "{}");
  t.applyTree(empty.as<JsonVariantConst>(), now + 2000, -1);
  t.snapshot(now + 2000, s);
  CHECK(item(s, cfg, "anchor").sev == Sev::Ok && item(s, cfg, "low_battery").sev == Sev::Ok && !item(s, cfg, "anchor").stale);
}

static void testTrackerStaleness() {
  Config cfg;
  std::string err;
  CHECK(parse(R"({"schema":1,"instruments":{
    "v":{"path":"a.v","label":"V","convert":"none","unit":"V","precision":2,"stale_after_s":10,
         "thresholds":{"alarm_below":11.8,"warn_below":12.2,"hysteresis":0.1}},
    "p":{"path":"a.p","label":"P","convert":"none","unit":"","precision":1,"stale_after_s":10}
  }})", cfg, err));
  Tracker t;
  t.begin(cfg);
  Snapshot s;
  JsonDocument d;

  // first sight, no Date header: age counts from first sight
  t.applyInstrument(0, mk(d, R"({"value":11.7,"timestamp":"T1"})"), 1000, -1);
  t.snapshot(1000, s);
  CHECK(!s.items[0].stale && s.items[0].sev == Sev::Alarm && s.items[0].ageS == 0);
  t.snapshot(11000, s);                                   // exactly the limit: strict >
  CHECK(!s.items[0].stale && s.items[0].ageS == 10);
  t.snapshot(11001, s);
  CHECK(s.items[0].stale && s.items[0].sev == Sev::Unknown && !s.items[0].missing);
  CHECK_STR(s.items[0].display, "11.70");                 // last value is kept for display

  // same timestamp again: a frozen path stays stale
  t.applyInstrument(0, mk(d, R"({"value":11.7,"timestamp":"T1"})"), 12000, -1);
  t.snapshot(12000, s);
  CHECK(s.items[0].stale && s.items[0].sev == Sev::Unknown);

  // new timestamp: fresh; the hysteresis memory was reset by the stale gap
  // (11.85 is inside the alarm hold zone 11.8..11.9, but with no history it is plain warn)
  t.applyInstrument(0, mk(d, R"({"value":11.85,"timestamp":"T2"})"), 13000, -1);
  t.snapshot(13000, s);
  CHECK(!s.items[0].stale && s.items[0].sev == Sev::Warn);

  // hysteresis memory persists while fresh: alarm, then 11.85 holds alarm
  t.applyInstrument(0, mk(d, R"({"value":11.7,"timestamp":"T3"})"), 14000, -1);
  t.applyInstrument(0, mk(d, R"({"value":11.85,"timestamp":"T4"})"), 15000, -1);
  t.snapshot(15000, s);
  CHECK(s.items[0].sev == Sev::Alarm);
  t.applyInstrument(0, mk(d, R"({"value":11.9,"timestamp":"T5"})"), 16000, -1);
  t.snapshot(16000, s);
  CHECK(s.items[0].sev == Sev::Warn);

  // the memory is also reset when the stale period is only discovered by the next update
  t.applyInstrument(0, mk(d, R"({"value":11.7,"timestamp":"T6"})"), 17000, -1);        // alarm
  t.applyInstrument(0, mk(d, R"({"value":11.85,"timestamp":"T7"})"), 17000 + 20000, -1);  // 20 s later, never snapshotted
  t.snapshot(17000 + 20000, s);
  CHECK(s.items[0].sev == Sev::Warn && !s.items[0].stale);

  // first sight with a Date header
  Tracker t2;
  t2.begin(cfg);
  t2.applyInstrument(0, mk(d, R"({"value":12.5,"timestamp":"2026-11-02T10:00:00.000Z"})"), 5000, kT0 + 20000);  // 20 s old
  t2.snapshot(5000, s);
  CHECK(s.items[0].stale && s.items[0].sev == Sev::Unknown && s.items[0].ageS == 20);
  t2.applyInstrument(1, mk(d, R"({"value":1.0,"timestamp":"2026-11-02T10:00:00.000Z"})"), 5000, kT0 + 5000);   // 5 s old
  t2.snapshot(5000, s);
  CHECK(!s.items[1].stale && s.items[1].ageS == 5);
  t2.snapshot(5000 + 5000, s);                            // 10 s: still fresh
  CHECK(!s.items[1].stale && s.items[1].ageS == 10);
  t2.snapshot(5000 + 5001, s);                            // 10.001 s
  CHECK(s.items[1].stale);
  // a later timestamp change resets the offset (the Date is only used on first sight)
  t2.applyInstrument(1, mk(d, R"({"value":1.0,"timestamp":"2026-11-02T10:00:07.000Z"})"), 20000, kT0 + 9000);
  t2.snapshot(20000, s);
  CHECK(!s.items[1].stale && s.items[1].ageS == 0);
  // Date older than the timestamp (clock skew): age 0, not stale
  Tracker t3;
  t3.begin(cfg);
  t3.applyInstrument(1, mk(d, R"({"value":1.0,"timestamp":"2026-11-02T10:00:00.000Z"})"), 5000, kT0 - 30000);
  t3.snapshot(5000, s);
  CHECK(!s.items[1].stale && s.items[1].ageS == 0);
  // unparseable timestamp or Date: falls back to first-sight time
  Tracker t4;
  t4.begin(cfg);
  t4.applyInstrument(1, mk(d, R"({"value":1.0,"timestamp":"garbage"})"), 5000, kT0);
  t4.snapshot(5000, s);
  CHECK(!s.items[1].stale && s.items[1].ageS == 0);

  // no timestamp at all: a changed value counts as a change
  Tracker t5;
  t5.begin(cfg);
  t5.applyInstrument(1, mk(d, R"({"value":1.0})"), 1000, -1);
  t5.applyInstrument(1, mk(d, R"({"value":1.0})"), 9000, -1);
  t5.snapshot(11500, s);
  CHECK(s.items[1].stale);                               // unchanged: still 10.5 s old
  t5.applyInstrument(1, mk(d, R"({"value":1.1})"), 11500, -1);
  t5.snapshot(11500, s);
  CHECK(!s.items[1].stale && s.items[1].ageS == 0);

  // millis() wraps
  Tracker t6;
  t6.begin(cfg);
  t6.applyInstrument(1, mk(d, R"({"value":1.0,"timestamp":"W"})"), 0xFFFFFF00u, -1);
  t6.snapshot(0x00000100u + 5000, s);                    // 0x200 ms later + 5 s
  CHECK(!s.items[1].stale && s.items[1].ageS == 5);
  t6.snapshot(0x00000100u + 10000, s);
  CHECK(s.items[1].stale);

  // unusable readings are ignored (the item stays missing / keeps its last value)
  Tracker t7;
  t7.begin(cfg);
  t7.applyInstrument(1, mk(d, R"({"value":"1.0","timestamp":"A"})"), 1000, -1);
  t7.applyInstrument(1, mk(d, R"({"value":true,"timestamp":"A"})"), 1000, -1);
  t7.applyInstrument(1, mk(d, R"({"value":1e30,"timestamp":"A"})"), 1000, -1);
  t7.applyInstrument(1, mk(d, R"({"value":null,"timestamp":"A"})"), 1000, -1);
  t7.applyInstrument(7, mk(d, R"({"value":1.0})"), 1000, -1);  // out of range index
  t7.snapshot(1000, s);
  CHECK(s.items[1].missing && s.count == 2);
  t7.applyInstrument(1, mk(d, R"({"value":2.0,"timestamp":"B"})"), 1000, -1);
  t7.applyInstrument(1, mk(d, R"({"value":"x","timestamp":"C"})"), 2000, -1);
  t7.snapshot(2000, s);
  CHECK(!s.items[1].missing && s.items[1].value == 2.0 && s.items[1].ageS == 1);  // unchanged by the bad reading
}

static void testTrackerPathsMode() {
  Config cfg;
  std::string err;
  CHECK(parse(R"({"schema":1,"instruments":{"d":{"path":"environment.depth.belowTransducer","label":"D","convert":"m->ft","unit":"ft","precision":1,"stale_after_s":10,
        "thresholds":{"alarm_below":4,"warn_below":8,"hysteresis":0.5}}},
        "notifications":{"a":{"path":"notifications.navigation.anchor","label":"A","absent":"unknown"}}})", cfg, err));
  Tracker t;
  t.begin(cfg);
  JsonDocument d;
  t.applyInstrument(0, mk(d, R"({"value":1.0,"timestamp":"T"})"), 0, -1);
  t.applyNotification(0, Reading());                  // 404 => not raised, absent: unknown
  t.setConnected(true);
  Snapshot s;
  t.snapshot(0, s);
  CHECK(s.items[0].sev == Sev::Alarm && s.items[0].hasThresholds);
  CHECK(s.items[1].sev == Sev::Unknown && !s.items[1].stale && !s.items[1].missing);
  CHECK(s.items[1].notification);
  t.applyNotification(0, mk(d, R"({"value":{"state":"warn"},"timestamp":"T"})"));
  t.snapshot(0, s);
  CHECK(s.items[1].sev == Sev::Warn);
  CHECK_STR(s.items[1].display, "WARN");
}

static void testShouldSendToken() {
  Config cfg;
  Endpoint ep;
  cfg.server.token = "SECRET";
  CHECK(!shouldSendToken(ep, cfg));  // default endpoint is not configured
  ep.fromDiscovery = true;
  CHECK(!shouldSendToken(ep, cfg));  // discovered
  ep.fromDiscovery = false;
  CHECK(!shouldSendToken(ep, cfg));  // cached
  ep.configured = true;
  CHECK(shouldSendToken(ep, cfg));   // explicit server.host
  cfg.server.token.clear();
  CHECK(!shouldSendToken(ep, cfg));  // configured but no token
}

int main() {
  testConversionsKnown();
  testNegate();
  testSignedWrap();
  test360Wrap();
  testHeadingRoundingWraps();
  testRounding();
  testRoundOutOfRange();
  testPosition();
  testBounds();
  testHysteresis();
  testNotifications();
  testTime();
  testConfigDefaults();
  testConfigParse();
  testConfigErrors();
  testConfigSyntaxPosition();
  testJsonNumber();
  testThroughJson();
  testFilterAndExtraction();
  testTrackerTree();
  testTrackerStaleness();
  testTrackerPathsMode();
  testShouldSendToken();
  std::printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
