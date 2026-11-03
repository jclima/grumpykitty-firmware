// SPDX-License-Identifier: MIT
// Copyright (c) 2026 JC Lima

/**
 * @file sk_model.cpp
 * @brief Boat Dash (app_14) data core. See sk_model.h. Pure C++17: no Arduino/ESP headers.
 */
#include "sk_model.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace sk {

namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kGal = 0.003785411784;  // US gallon in m3
}  // namespace

// ===========================================================================
// Conversions (displays/conversions.py)
// ===========================================================================

namespace {

double signedDeg(double rad) {
  double deg = std::fmod(rad * (180.0 / kPi), 360.0);
  if (deg > 180.0) deg -= 360.0;
  else if (deg <= -180.0) deg += 360.0;
  return deg;
}

double deg360(double rad) {
  double deg = std::fmod(rad * (180.0 / kPi), 360.0);
  if (deg < 0) deg += 360.0;  // python's float %: result takes the sign of the divisor
  return deg >= 360.0 ? 0.0 : deg;
}

struct Conv {
  const char* name;
  double (*fn)(double);
};

// Same order as CONVERSIONS in conversions.py, then the additions (Pa->kPa).
const Conv kConversions[] = {
    {"none", [](double v) { return v; }},
    {"m/s->kn", [](double v) { return v * 3600.0 / 1852.0; }},
    {"m/s->km/h", [](double v) { return v * 3.6; }},
    {"m/s->mph", [](double v) { return v * 3600.0 / 1609.344; }},
    {"K->C", [](double v) { return v - 273.15; }},
    {"K->F", [](double v) { return (v - 273.15) * 9.0 / 5.0 + 32.0; }},
    {"rad->deg", [](double v) { return v * (180.0 / kPi); }},
    {"rad->deg_signed", signedDeg},
    {"rad->deg_360", deg360},
    {"m->ft", [](double v) { return v / 0.3048; }},
    {"m->fathom", [](double v) { return v / 1.8288; }},
    {"m->nmi", [](double v) { return v / 1852.0; }},
    {"ratio->%", [](double v) { return v * 100.0; }},
    {"Pa->hPa", [](double v) { return v / 100.0; }},
    {"Pa->kPa", [](double v) { return v / 1000.0; }},  // addition: oil pressure
    {"Pa->bar", [](double v) { return v / 100000.0; }},
    {"Pa->psi", [](double v) { return v / 6894.757293168; }},
    {"m3->L", [](double v) { return v * 1000.0; }},
    {"m3->gal", [](double v) { return v / kGal; }},
    {"m3/s->L/h", [](double v) { return v * 1000.0 * 3600.0; }},
    {"m3/s->gal/h", [](double v) { return v / kGal * 3600.0; }},
    {"Hz->rpm", [](double v) { return v * 60.0; }},
    {"C->Ah", [](double v) { return v / 3600.0; }},
    {"s->min", [](double v) { return v / 60.0; }},
    {"s->h", [](double v) { return v / 3600.0; }},
    {"negate", [](double v) { return -v; }},  // reversed shunt
};
constexpr size_t kConvCount = sizeof(kConversions) / sizeof(kConversions[0]);

const Conv* findConv(const char* name) {
  if (!name) return nullptr;
  for (size_t i = 0; i < kConvCount; i++)
    if (std::strcmp(kConversions[i].name, name) == 0) return &kConversions[i];
  return nullptr;
}

}  // namespace

size_t conversionCount() { return kConvCount; }
const char* conversionName(size_t index) { return index < kConvCount ? kConversions[index].name : nullptr; }
bool isKnownConversion(const char* name) { return findConv(name) != nullptr || (name && std::strcmp(name, "position") == 0); }

bool convert(const char* name, double value, double& out) {
  const Conv* c = findConv(name);
  if (!c) return false;
  out = c->fn(value);
  return true;
}

bool roundValue(double value, int precision, double& out) {
  if (!std::isfinite(value) || std::fabs(value) > kMaxAbsValue) return false;
  if (precision < 0 || precision > 9) return false;
  if (value == 0.0) {
    out = 0.0;
    return true;
  }
  // 15 significant digits (what "%.15g" gives): drops binary noise such as the last-ulp error of
  // ArduinoJson's number parser, so 6.45 stays a tie. Python's repr() would keep up to 17 digits.
  char buf[48];
  std::snprintf(buf, sizeof buf, "%.14e", value);
  const char* p = buf;
  const bool neg = (*p == '-');
  if (neg) p++;
  std::string digs;
  for (; *p && *p != 'e'; p++)
    if (*p >= '0' && *p <= '9') digs += *p;
  const int exp10 = (*p == 'e') ? std::atoi(p + 1) : 0;
  while (digs.size() > 1 && digs.back() == '0') digs.pop_back();

  // value = 0.DIGS * 10^(exp10+1); keep `keep` digits to land on `precision` decimals.
  const int keep = exp10 + 1 + precision;
  std::string mant;
  if (keep < 0) {
    mant = "0";
  } else if (keep == 0) {
    mant = (digs[0] >= '5') ? "1" : "0";
  } else if (static_cast<size_t>(keep) >= digs.size()) {
    mant = digs + std::string(static_cast<size_t>(keep) - digs.size(), '0');
  } else {
    mant = digs.substr(0, static_cast<size_t>(keep));
    if (digs[static_cast<size_t>(keep)] >= '5') {  // half (or more) rounds away from zero
      size_t i = mant.size();
      while (i > 0) {
        if (mant[i - 1] == '9') {
          mant[i - 1] = '0';
          i--;
        } else {
          mant[i - 1]++;
          break;
        }
      }
      if (i == 0) mant.insert(mant.begin(), '1');
    }
  }
  const std::string text = mant + "e-" + std::to_string(precision);
  const double r = std::strtod(text.c_str(), nullptr);
  out = (r == 0.0) ? 0.0 : (neg ? -r : r);
  return true;
}

bool convertAndRound(const char* name, double value, int precision, double& out) {
  double c;
  if (!convert(name, value, c)) return false;
  double r;
  if (!roundValue(c, precision, r)) return false;
  if (std::strcmp(name, "rad->deg_360") == 0 && r >= 360.0) r = 0.0;  // 359.5.. rounds up to 360: wrap
  out = r;
  return true;
}

bool formatPosition(double lat, double lon, char* out, size_t outSize) {
  if (!out || outSize == 0) return false;
  out[0] = '\0';
  if (!std::isfinite(lat) || !std::isfinite(lon) || std::fabs(lat) > 90.0 || std::fabs(lon) > 180.0) return false;
  auto part = [](double v, char* dst, size_t n, bool isLat) {
    const char hemi = isLat ? (v < 0 ? 'S' : 'N') : (v < 0 ? 'W' : 'E');
    const double a = std::fabs(v);
    int deg = static_cast<int>(std::floor(a));
    double min = (a - deg) * 60.0;
    double r;
    if (!roundValue(min, 3, r)) r = 0;
    if (r >= 60.0) {
      deg++;
      r = 0.0;
    }
    std::snprintf(dst, n, "%d %06.3f'%c", deg, r, hemi);
  };
  char a[20], b[20];
  part(lat, a, sizeof a, true);
  part(lon, b, sizeof b, false);
  const int n = std::snprintf(out, outSize, "%s %s", a, b);
  return n > 0 && static_cast<size_t>(n) < outSize;
}

// ===========================================================================
// Severity (displays/severity.py)
// ===========================================================================

const char* sevName(Sev s) {
  switch (s) {
    case Sev::Ok: return "ok";
    case Sev::Warn: return "warn";
    case Sev::Alarm: return "alarm";
    default: return "unknown";
  }
}

namespace {
// Python round(x, 9): correctly rounded decimal of the exact binary value.
double round9(double x) {
  if (!std::isfinite(x) || std::fabs(x) > kMaxAbsValue) return x;
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.9f", x);
  return std::strtod(buf, nullptr);
}
}  // namespace

Sev numericSev(double v, const Thresholds& th, Sev prev) {
  if (!std::isfinite(v)) return Sev::Unknown;
  const double h = std::isfinite(th.hysteresis) ? th.hysteresis : 0.0;
  auto below = [&](bool has, double bound, double margin) { return has && v < round9(bound + margin); };
  auto above = [&](bool has, double bound, double margin) { return has && v > round9(bound - margin); };
  auto alarm = [&](double m) { return below(th.hasAlarmBelow, th.alarmBelow, m) || above(th.hasAlarmAbove, th.alarmAbove, m); };
  auto warn = [&](double m) { return below(th.hasWarnBelow, th.warnBelow, m) || above(th.hasWarnAbove, th.warnAbove, m); };

  if (alarm(0)) return Sev::Alarm;
  if (prev == Sev::Alarm && alarm(h)) return Sev::Alarm;
  if (warn(0)) return Sev::Warn;
  if ((prev == Sev::Warn || prev == Sev::Alarm) && warn(h)) return Sev::Warn;
  return Sev::Ok;
}

Sev notificationStateSev(const char* state) {
  if (!state) return Sev::Unknown;
  if (!std::strcmp(state, "normal") || !std::strcmp(state, "nominal")) return Sev::Ok;
  if (!std::strcmp(state, "alert") || !std::strcmp(state, "warn")) return Sev::Warn;
  if (!std::strcmp(state, "alarm") || !std::strcmp(state, "emergency")) return Sev::Alarm;
  return Sev::Unknown;
}

Sev notificationSev(JsonVariantConst value, bool absentOk) {
  if (value.isNull()) return absentOk ? Sev::Ok : Sev::Unknown;
  if (!value.is<JsonObjectConst>()) return Sev::Unknown;
  JsonVariantConst state = value["state"];
  if (!state.is<const char*>()) return Sev::Unknown;
  return notificationStateSev(state.as<const char*>());
}

// ===========================================================================
// JSON numbers
// ===========================================================================

bool jsonNumber(JsonVariantConst v, double& out) {
  if (!v.is<double>()) return false;  // also true for integers
  if (v.is<long long>()) {            // an integer literal: exact
    out = v.as<double>();
    return true;
  }
  const double d = v.as<double>();
  out = d;
  if (!std::isfinite(d) || d == 0.0) return true;
  const float f = static_cast<float>(d);
  if (static_cast<double>(f) != d) return true;  // double path: parsed at full precision
  // float path: find the shortest decimal that is within a few float ulps of the stored value
  const double tol = 4.0 * std::fabs(static_cast<double>(std::nextafter(f, std::numeric_limits<float>::infinity())) - d);
  char buf[32];
  for (int digits = 1; digits <= 7; digits++) {
    std::snprintf(buf, sizeof buf, "%.*g", digits, d);
    const double r = std::strtod(buf, nullptr);
    if (std::fabs(r - d) <= tol) {
      out = r;
      return true;
    }
  }
  return true;
}

// ===========================================================================
// Time parsing
// ===========================================================================

namespace {

int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

bool validDate(int y, int m, int d) {
  if (y < 1970 || y > 9999 || m < 1 || m > 12 || d < 1) return false;
  static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int max = dim[m - 1];
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) max = 29;
  return d <= max;
}

bool readDigits(const char*& p, int count, int& out) {
  out = 0;
  for (int i = 0; i < count; i++) {
    if (*p < '0' || *p > '9') return false;
    out = out * 10 + (*p - '0');
    p++;
  }
  return true;
}

}  // namespace

bool parseIso8601(const char* text, int64_t& epochMs) {
  if (!text) return false;
  const char* p = text;
  int Y, M, D, h, mi, s;
  if (!readDigits(p, 4, Y) || *p++ != '-' || !readDigits(p, 2, M) || *p++ != '-' || !readDigits(p, 2, D)) return false;
  if (*p != 'T' && *p != 't' && *p != ' ') return false;
  p++;
  if (!readDigits(p, 2, h) || *p++ != ':' || !readDigits(p, 2, mi) || *p++ != ':' || !readDigits(p, 2, s)) return false;
  if (!validDate(Y, M, D) || h > 23 || mi > 59 || s > 59) return false;
  int ms = 0;
  if (*p == '.') {
    p++;
    if (*p < '0' || *p > '9') return false;
    int scale = 100;
    for (; *p >= '0' && *p <= '9'; p++) {
      ms += (*p - '0') * scale;  // digits past the third are truncated
      scale /= 10;
    }
  }
  int64_t offsetS = 0;
  if (*p == 'Z' || *p == 'z') {
    p++;
  } else if (*p == '+' || *p == '-') {
    const int sign = (*p == '-') ? -1 : 1;
    p++;
    int oh, om;
    if (!readDigits(p, 2, oh)) return false;
    if (*p == ':') p++;
    if (!readDigits(p, 2, om) || oh > 23 || om > 59) return false;
    offsetS = sign * (oh * 3600 + om * 60);
  }
  if (*p != '\0') return false;
  const int64_t days = daysFromCivil(Y, static_cast<unsigned>(M), static_cast<unsigned>(D));
  epochMs = ((days * 86400 + h * 3600 + mi * 60 + s) - offsetS) * 1000 + ms;
  return true;
}

bool parseHttpDate(const char* text, int64_t& epochMs) {
  if (!text) return false;
  const char* p = text;
  while (*p == ' ') p++;
  if (const char* comma = std::strchr(p, ',')) p = comma + 1;  // optional weekday
  while (*p == ' ') p++;
  int D, Y, h, mi, s;
  if (!readDigits(p, 2, D)) return false;
  if (*p++ != ' ') return false;
  static const char* const mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  int M = 0;
  for (int i = 0; i < 12; i++)
    if (std::strncmp(p, mon[i], 3) == 0) M = i + 1;
  if (!M) return false;
  p += 3;
  if (*p++ != ' ' || !readDigits(p, 4, Y) || *p++ != ' ') return false;
  if (!readDigits(p, 2, h) || *p++ != ':' || !readDigits(p, 2, mi) || *p++ != ':' || !readDigits(p, 2, s)) return false;
  if (*p++ != ' ') return false;
  if (std::strcmp(p, "GMT") != 0 && std::strcmp(p, "UTC") != 0 && std::strcmp(p, "UT") != 0 && std::strcmp(p, "Z") != 0)
    return false;
  if (!validDate(Y, M, D) || h > 23 || mi > 59 || s > 59) return false;
  epochMs = (daysFromCivil(Y, static_cast<unsigned>(M), static_cast<unsigned>(D)) * 86400 + h * 3600 + mi * 60 + s) * 1000;
  return true;
}

// ===========================================================================
// Config (displays/schema.py validation, trimmed to this firmware)
// ===========================================================================

int Config::findItem(const std::string& id) const {
  for (size_t i = 0; i < instruments.size(); i++)
    if (instruments[i].id == id) return static_cast<int>(i);
  for (size_t i = 0; i < notifications.size(); i++)
    if (notifications[i].id == id) return static_cast<int>(instruments.size() + i);
  return -1;
}

namespace {

struct CountingReader {
  const char* data;
  size_t len;
  size_t pos = 0;
  int read() { return pos < len ? static_cast<unsigned char>(data[pos++]) : -1; }
  size_t readBytes(char* buf, size_t n) {
    const size_t k = std::min(n, len - pos);
    std::memcpy(buf, data + pos, k);
    pos += k;
    return k;
  }
};

struct Errors {
  std::string first;
  bool bad() const { return !first.empty(); }
  void add(const std::string& where, const std::string& msg) {
    if (first.empty()) first = where + ": " + msg;
  }
};

bool validId(const std::string& s) {
  if (s.empty() || s.size() > 32 || s[0] < 'a' || s[0] > 'z') return false;
  for (char c : s)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  return true;
}

// ^[A-Za-z][A-Za-z0-9]*(\.[A-Za-z0-9_-]+)+$
bool validPath(const std::string& s) {
  if (s.empty() || s.size() > 96) return false;
  size_t i = 0;
  if (!std::isalpha(static_cast<unsigned char>(s[0]))) return false;
  while (i < s.size() && std::isalnum(static_cast<unsigned char>(s[i]))) i++;
  int segs = 0;
  while (i < s.size()) {
    if (s[i] != '.') return false;
    i++;
    size_t start = i;
    while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '-')) i++;
    if (i == start) return false;
    segs++;
  }
  return segs >= 1;
}

bool printableText(const std::string& s, size_t lo, size_t hi) {
  size_t chars = 0;
  for (unsigned char c : s) {
    if (c < 0x20 || c == 0x7f) return false;
    if ((c & 0xC0) != 0x80) chars++;
  }
  return chars >= lo && chars <= hi;
}

bool finiteNumber(JsonVariantConst v) {
  double d;
  return jsonNumber(v, d) && std::isfinite(d);
}

bool isInt(JsonVariantConst v) { return v.is<int>(); }

template <size_t N>
void checkKeys(JsonObjectConst o, const char* const (&allowed)[N], const std::string& where, Errors& e) {
  for (JsonPairConst kv : o) {
    bool ok = false;
    for (size_t i = 0; i < N; i++)
      if (kv.key() == allowed[i]) ok = true;
    if (!ok) e.add(where, std::string("unknown key \"") + kv.key().c_str() + "\"");
  }
}

bool needString(JsonObjectConst o, const char* key, const std::string& where, Errors& e, std::string& out) {
  JsonVariantConst v = o[key];
  if (!v.is<const char*>()) {
    e.add(where + "." + key, o[key].isNull() ? "missing" : "must be a string");
    return false;
  }
  out = v.as<const char*>();
  return true;
}

void parseThresholds(JsonVariantConst v, const std::string& where, Thresholds& th, Errors& e) {
  static const char* const keys[] = {"alarm_below", "warn_below", "warn_above", "alarm_above", "hysteresis"};
  if (!v.is<JsonObjectConst>()) {
    e.add(where, "must be an object");
    return;
  }
  JsonObjectConst o = v.as<JsonObjectConst>();
  checkKeys(o, keys, where, e);
  struct B {
    const char* name;
    bool low;
    bool* has;
    double* val;
  } bounds[] = {{"alarm_below", true, &th.hasAlarmBelow, &th.alarmBelow},
                {"warn_below", true, &th.hasWarnBelow, &th.warnBelow},
                {"warn_above", false, &th.hasWarnAbove, &th.warnAbove},
                {"alarm_above", false, &th.hasAlarmAbove, &th.alarmAbove}};
  bool allNumbers = true;
  for (B& b : bounds) {
    if (o[b.name].isNull()) continue;
    if (!finiteNumber(o[b.name])) {
      e.add(where + "." + b.name, "must be a finite number");
      allNumbers = false;
      continue;
    }
    *b.has = true;
    jsonNumber(o[b.name], *b.val);
  }
  if (!th.any() && allNumbers) e.add(where, "needs at least one of alarm_below, warn_below, warn_above, alarm_above");
  if (!allNumbers) return;
  // ordering over the bounds that are present, in severity order
  std::vector<const B*> present;
  for (const B& b : bounds)
    if (*b.has) present.push_back(&b);
  double minGap = 0;
  bool haveGap = false;
  for (size_t i = 0; i + 1 < present.size(); i++) {
    const B& a = *present[i];
    const B& b = *present[i + 1];
    if (a.low == b.low) {
      if (*a.val > *b.val) e.add(where, std::string(a.name) + " must not be greater than " + b.name);
    } else if (*a.val >= *b.val) {
      e.add(where, std::string(a.name) + " must be less than " + b.name);
    }
    const double gap = *b.val - *a.val;
    if (!haveGap || gap < minGap) minGap = gap;
    haveGap = true;
  }
  if (!o["hysteresis"].isNull()) {
    JsonVariantConst h = o["hysteresis"];
    double hv = 0;
    if (!finiteNumber(h) || (jsonNumber(h, hv), hv < 0)) {
      e.add(where + ".hysteresis", "must be a finite number >= 0");
    } else {
      th.hysteresis = hv;
      if (haveGap && th.hysteresis >= minGap)
        e.add(where + ".hysteresis", "must be less than the smallest gap between bounds");
    }
  }
}

}  // namespace

bool parseConfig(const char* json, size_t len, Config& out, std::string& err) {
  out = Config();
  err.clear();
  JsonDocument doc;
  CountingReader reader{json, len};
  DeserializationError de = deserializeJson(doc, reader, DeserializationOption::NestingLimit(12));
  if (de) {
    size_t pos = std::min(reader.pos, len);
    int line = 1, col = 1;
    for (size_t i = 0; i + 1 < pos; i++) {  // the parser has read one byte past the offender
      if (json[i] == '\n') {
        line++;
        col = 1;
      } else {
        col++;
      }
    }
    err = std::string("JSON error: ") + de.c_str() + " at line " + std::to_string(line) + ", column " +
          std::to_string(col) + " (byte " + std::to_string(pos) + ")";
    return false;
  }

  Errors e;
  if (!doc.is<JsonObject>()) {
    err = "$: config must be an object";
    return false;
  }
  JsonObjectConst root = doc.as<JsonObjectConst>();
  static const char* const topKeys[] = {"schema", "server", "instruments", "notifications", "pages"};
  checkKeys(root, topKeys, "$", e);
  if (!isInt(root["schema"]) || root["schema"].as<int>() != 1) e.add("$.schema", "must be 1");

  Config cfg;
  // ---- server
  if (!root["server"].isNull()) {
    JsonVariantConst sv = root["server"];
    if (!sv.is<JsonObjectConst>()) {
      e.add("$.server", "must be an object");
    } else {
      JsonObjectConst so = sv.as<JsonObjectConst>();
      static const char* const keys[] = {"host", "port", "token", "poll_ms", "fetch"};
      checkKeys(so, keys, "$.server", e);
      if (!so["host"].isNull()) {
        if (!so["host"].is<const char*>() || strlen(so["host"].as<const char*>()) > 253)
          e.add("$.server.host", "must be a string of at most 253 characters (empty: discover)");
        else if (!isValidServerHost(so["host"].as<const char*>()))
          e.add("$.server.host",
                "server.host must be a hostname or IPv4 address, without scheme, port or path; "
                "use server.port for the port");
        else
          cfg.server.host = so["host"].as<const char*>();
      }
      if (!so["port"].isNull()) {
        if (!isInt(so["port"]) || so["port"].as<int>() < 1 || so["port"].as<int>() > 65535)
          e.add("$.server.port", "must be an integer 1..65535");
        else
          cfg.server.port = so["port"].as<int>();
      }
      if (!so["token"].isNull()) {
        if (!so["token"].is<const char*>() || strlen(so["token"].as<const char*>()) > 1024)
          e.add("$.server.token", "must be a string of at most 1024 characters");
        else
          cfg.server.token = so["token"].as<const char*>();
      }
      if (!so["poll_ms"].isNull()) {
        if (!isInt(so["poll_ms"]) || so["poll_ms"].as<int>() < 500 || so["poll_ms"].as<int>() > 60000)
          e.add("$.server.poll_ms", "must be an integer 500..60000");
        else
          cfg.server.pollMs = so["poll_ms"].as<int>();
      }
      if (!so["fetch"].isNull()) {
        const char* f = so["fetch"].is<const char*>() ? so["fetch"].as<const char*>() : "";
        if (!std::strcmp(f, "tree")) cfg.server.fetch = Fetch::Tree;
        else if (!std::strcmp(f, "paths")) cfg.server.fetch = Fetch::Paths;
        else e.add("$.server.fetch", "must be \"tree\" or \"paths\"");
      }
    }
  }

  // ---- instruments
  if (!root["instruments"].isNull()) {
    JsonVariantConst iv = root["instruments"];
    if (!iv.is<JsonObjectConst>()) {
      e.add("$.instruments", "must be an object");
    } else {
      static const char* const keys[] = {"path", "label", "convert", "unit", "precision", "stale_after_s", "thresholds",
                                         "icon", "min_interval_ms", "min", "max", "alarm"};
      for (JsonPairConst kv : iv.as<JsonObjectConst>()) {
        Instrument in;
        in.id = kv.key().c_str();
        const std::string where = "$.instruments." + in.id;
        if (!validId(in.id)) e.add(where, "instrument id must match ^[a-z][a-z0-9_]{0,31}$");
        if (!kv.value().is<JsonObjectConst>()) {
          e.add(where, "must be an object");
          continue;
        }
        JsonObjectConst o = kv.value().as<JsonObjectConst>();
        checkKeys(o, keys, where, e);
        if (needString(o, "path", where, e, in.path) && !validPath(in.path)) e.add(where + ".path", "must be a dotted SignalK path");
        if (needString(o, "label", where, e, in.label) && !printableText(in.label, 1, 16))
          e.add(where + ".label", "must be 1..16 printable characters");
        if (needString(o, "convert", where, e, in.convert) && !isKnownConversion(in.convert.c_str()))
          e.add(where + ".convert", "unknown conversion \"" + in.convert + "\"");
        if (needString(o, "unit", where, e, in.unit) && !(in.unit.size() <= 8 && printableText(in.unit, 0, 8)))
          e.add(where + ".unit", "must be a printable string of at most 8 characters");
        if (!isInt(o["precision"]) || o["precision"].as<int>() < 0 || o["precision"].as<int>() > 3)
          e.add(where + ".precision", "must be an integer 0..3");
        else
          in.precision = o["precision"].as<int>();
        if (!isInt(o["stale_after_s"]) || o["stale_after_s"].as<int>() < 1 || o["stale_after_s"].as<int>() > 3600)
          e.add(where + ".stale_after_s", "must be an integer 1..3600");
        else
          in.staleAfterS = o["stale_after_s"].as<int>();
        if (!o["thresholds"].isNull()) {
          in.hasThresholds = true;
          parseThresholds(o["thresholds"], where + ".thresholds", in.th, e);
        }
        cfg.instruments.push_back(in);
      }
    }
  }

  // ---- notifications
  if (!root["notifications"].isNull()) {
    JsonVariantConst nv = root["notifications"];
    if (!nv.is<JsonObjectConst>()) {
      e.add("$.notifications", "must be an object");
    } else {
      static const char* const keys[] = {"path", "label", "absent", "alarm", "icon"};
      for (JsonPairConst kv : nv.as<JsonObjectConst>()) {
        Notification n;
        n.id = kv.key().c_str();
        const std::string where = "$.notifications." + n.id;
        if (!validId(n.id)) e.add(where, "notification id must match ^[a-z][a-z0-9_]{0,31}$");
        for (const Instrument& in : cfg.instruments)
          if (in.id == n.id) e.add(where, "id is already used by an instrument; ids must be unique");
        if (!kv.value().is<JsonObjectConst>()) {
          e.add(where, "must be an object");
          continue;
        }
        JsonObjectConst o = kv.value().as<JsonObjectConst>();
        checkKeys(o, keys, where, e);
        if (needString(o, "path", where, e, n.path) && !(validPath(n.path) && n.path.rfind("notifications.", 0) == 0))
          e.add(where + ".path", "must be a dotted SignalK path starting with \"notifications.\"");
        if (needString(o, "label", where, e, n.label) && !printableText(n.label, 1, 16))
          e.add(where + ".label", "must be 1..16 printable characters");
        if (!o["absent"].isNull()) {
          const char* a = o["absent"].is<const char*>() ? o["absent"].as<const char*>() : "";
          if (!std::strcmp(a, "ok")) n.absentOk = true;
          else if (!std::strcmp(a, "unknown")) n.absentOk = false;
          else e.add(where + ".absent", "must be one of ok, unknown");
        }
        cfg.notifications.push_back(n);
      }
    }
  }
  if (cfg.itemCount() > kMaxItems) e.add("$", "at most " + std::to_string(kMaxItems) + " instruments + notifications");
  for (const Instrument& in : cfg.instruments)  // a stale limit shorter than two polls is permanently stale
    if (in.staleAfterS * 1000 < 2 * cfg.server.pollMs)
      e.add("$.instruments." + in.id + ".stale_after_s", "must be at least 2 x server.poll_ms");

  // ---- pages
  if (!root["pages"].isNull()) {
    JsonVariantConst pv = root["pages"];
    if (!pv.is<JsonArrayConst>()) {
      e.add("$.pages", "must be an array");
    } else {
      static const char* const keys[] = {"title", "items", "special", "layout"};
      if (pv.as<JsonArrayConst>().size() > kMaxPages) e.add("$.pages", "at most " + std::to_string(kMaxPages) + " pages");
      size_t idx = 0;
      for (JsonVariantConst p : pv.as<JsonArrayConst>()) {
        const std::string where = "$.pages[" + std::to_string(idx++) + "]";
        if (!p.is<JsonObjectConst>()) {
          e.add(where, "must be an object");
          continue;
        }
        JsonObjectConst o = p.as<JsonObjectConst>();
        checkKeys(o, keys, where, e);
        Page page;
        if (needString(o, "title", where, e, page.title) && !printableText(page.title, 1, 16))
          e.add(where + ".title", "must be 1..16 printable characters");
        if (!o["special"].isNull()) {
          if (o["special"].is<const char*>() && !std::strcmp(o["special"].as<const char*>(), "status")) page.status = true;
          else e.add(where + ".special", "must be \"status\"");
        }
        if (!o["items"].is<JsonArrayConst>()) {
          e.add(where + ".items", "must be an array of item ids");
        } else {
          if (o["items"].as<JsonArrayConst>().size() > kMaxPageItems)
            e.add(where + ".items", "at most " + std::to_string(kMaxPageItems) + " items");
          size_t k = 0;
          for (JsonVariantConst it : o["items"].as<JsonArrayConst>()) {
            const std::string iw = where + ".items[" + std::to_string(k++) + "]";
            if (!it.is<const char*>()) {
              e.add(iw, "must be a string");
              continue;
            }
            page.items.push_back(it.as<const char*>());
            if (cfg.findItem(page.items.back()) < 0) e.add(iw, "unknown item \"" + page.items.back() + "\"");
          }
        }
        cfg.pages.push_back(page);
      }
    }
  }

  if (e.bad()) {
    err = e.first;
    return false;
  }
  out = std::move(cfg);
  return true;
}

// ===========================================================================
// Filter and extraction
// ===========================================================================

namespace {

// Walk/create nested objects for a dotted path below `root` (keys are copied).
// Note: assigning to a JsonVariant writes through it, so the cursor is a JsonObject rebuilt each step.
JsonObject walkCreate(JsonObject root, const std::string& path) {
  JsonObject cur = root;
  size_t start = 0;
  while (start <= path.size()) {
    size_t dot = path.find('.', start);
    if (dot == std::string::npos) dot = path.size();
    const std::string seg = path.substr(start, dot - start);
    const JsonString key(seg.c_str(), false);  // copied into the document
    if (cur[key].is<JsonObject>()) cur = cur[key].as<JsonObject>();
    else cur = cur[key].to<JsonObject>();
    start = dot + 1;
  }
  return cur;
}

}  // namespace

bool buildFilter(const Config& cfg, JsonDocument& filter) {
  filter.clear();
  JsonObject root = filter.to<JsonObject>();
  for (const Instrument& in : cfg.instruments) {
    JsonObject node = walkCreate(root, in.path);
    node["value"] = true;
    node["timestamp"] = true;
  }
  for (const Notification& n : cfg.notifications) {
    JsonObject node = walkCreate(root, n.path);
    node["value"].to<JsonObject>()["state"] = true;
    node["timestamp"] = true;
  }
  return !filter.overflowed();
}

DeserializationError parseTree(const char* json, size_t len, const JsonDocument& filter, JsonDocument& out) {
  return deserializeJson(out, json, len, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(kNestingLimit));
}

Reading readNode(JsonVariantConst node) {
  Reading r;
  if (!node.is<JsonObjectConst>()) return r;
  r.value = node["value"];
  r.present = !r.value.isNull();
  JsonVariantConst ts = node["timestamp"];
  if (ts.is<const char*>()) r.timestamp = ts.as<const char*>();
  return r;
}

Reading lookup(JsonVariantConst root, const std::string& dottedPath) {
  JsonVariantConst node = root;
  size_t start = 0;
  while (start <= dottedPath.size()) {
    size_t dot = dottedPath.find('.', start);
    if (dot == std::string::npos) dot = dottedPath.size();
    if (!node.is<JsonObjectConst>()) return Reading();
    char seg[64];
    const size_t n = dot - start;
    if (n == 0 || n >= sizeof seg) return Reading();
    std::memcpy(seg, dottedPath.data() + start, n);
    seg[n] = '\0';
    node = node[static_cast<const char*>(seg)];  // numeric segments ("0") are plain object keys
    start = dot + 1;
  }
  return readNode(node);
}

// ===========================================================================
// Tracker
// ===========================================================================

namespace {
const char* noteLabel(Sev s) {
  switch (s) {
    case Sev::Ok: return "OK";
    case Sev::Warn: return "WARN";
    case Sev::Alarm: return "ALARM";
    default: return "--";
  }
}
void copyStr(char* dst, size_t n, const char* src) {
  std::strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}
}  // namespace

void Tracker::begin(const Config& cfg) {
  cfg_ = &cfg;
  for (State& s : inst_) s = State();
  for (size_t i = 0; i < kMaxItems; i++) {
    noteSev_[i] = Sev::Unknown;
    noteSeen_[i] = false;
  }
  connected_ = false;
  everConnected_ = false;
}

uint64_t Tracker::ageMs(const State& s, uint32_t nowMs) const {
  return static_cast<uint64_t>(s.ageOffsetMs) + static_cast<uint32_t>(nowMs - s.changedAt);
}

void Tracker::applyInstrument(size_t index, const Reading& r, uint32_t nowMs, int64_t dateMs) {
  if (!cfg_ || index >= cfg_->instruments.size() || !r.present) return;
  const Instrument& in = cfg_->instruments[index];
  State& st = inst_[index];

  double value = 0;
  char text[sizeof st.text];
  if (in.convert == "position") {
    JsonVariantConst v = r.value;
    double lat, lon;
    if (!v.is<JsonObjectConst>() || !jsonNumber(v["latitude"], lat) || !jsonNumber(v["longitude"], lon)) return;
    if (!formatPosition(lat, lon, text, sizeof text)) return;
  } else {
    double raw;
    if (!jsonNumber(r.value, raw)) return;  // strings, bools, objects are not readings
    if (!std::isfinite(raw) || !convertAndRound(in.convert.c_str(), raw, in.precision, value)) return;
    std::snprintf(text, sizeof text, "%.*f", in.precision, value);
  }

  const uint64_t limitMs = static_cast<uint64_t>(in.staleAfterS) * 1000;
  const bool first = !st.seen;
  const bool wasStale = st.seen && ageMs(st, nowMs) > limitMs;

  bool changed;
  if (first) changed = true;
  else if (r.timestamp) changed = !st.hasTs || std::strncmp(st.ts, r.timestamp, sizeof st.ts - 1) != 0;
  else changed = st.hasTs || std::strcmp(st.text, text) != 0;

  if (changed) {
    uint32_t offset = 0;
    int64_t tsMs;
    if (first && dateMs >= 0 && r.timestamp && parseIso8601(r.timestamp, tsMs) && dateMs > tsMs) {
      const int64_t diff = dateMs - tsMs;
      offset = diff > 0x7FFFFFFF ? 0x7FFFFFFFu : static_cast<uint32_t>(diff);
    }
    st.changedAt = nowMs;
    st.ageOffsetMs = offset;
    st.hasTs = r.timestamp != nullptr;
    copyStr(st.ts, sizeof st.ts, r.timestamp ? r.timestamp : "");
  }
  st.value = value;
  copyStr(st.text, sizeof st.text, text);
  st.seen = true;

  const bool stale = ageMs(st, nowMs) > limitMs;
  if (stale) {
    st.sev = Sev::Unknown;  // memory reset
  } else if (in.hasThresholds) {
    st.sev = numericSev(value, in.th, (first || wasStale) ? Sev::Unknown : st.sev);
  } else {
    st.sev = Sev::Ok;
  }
}

void Tracker::applyNotification(size_t index, const Reading& r) {
  if (!cfg_ || index >= cfg_->notifications.size()) return;
  noteSev_[index] = notificationSev(r.present ? r.value : JsonVariantConst(), cfg_->notifications[index].absentOk);
  noteSeen_[index] = true;
}

void Tracker::applyTree(JsonVariantConst root, uint32_t nowMs, int64_t dateMs) {
  if (!cfg_) return;
  for (size_t i = 0; i < cfg_->instruments.size(); i++) applyInstrument(i, lookup(root, cfg_->instruments[i].path), nowMs, dateMs);
  for (size_t i = 0; i < cfg_->notifications.size(); i++) applyNotification(i, lookup(root, cfg_->notifications[i].path));
  setConnected(true);
}

void Tracker::setConnected(bool connected) {
  connected_ = connected;
  if (connected) {
    everConnected_ = true;
  } else {
    for (size_t i = 0; i < kMaxItems; i++) {
      noteSev_[i] = Sev::Unknown;  // forget: a notification cleared during the outage is not re-announced
      noteSeen_[i] = false;
    }
  }
}

void Tracker::snapshot(uint32_t nowMs, Snapshot& out) {
  out = Snapshot();
  if (!cfg_) return;
  out.connected = connected_;
  size_t n = 0;
  for (size_t i = 0; i < cfg_->instruments.size(); i++, n++) {
    const Instrument& in = cfg_->instruments[i];
    State& st = inst_[i];
    SnapItem& it = out.items[n];
    copyStr(it.unit, sizeof it.unit, in.unit.c_str());
    it.hasThresholds = in.hasThresholds;
    if (!st.seen) continue;  // missing: "--", Unknown
    const uint64_t age = ageMs(st, nowMs);
    it.missing = false;
    it.stale = age > static_cast<uint64_t>(in.staleAfterS) * 1000;
    it.ageS = static_cast<uint32_t>(std::min<uint64_t>(age / 1000, 0xFFFFFFFFu));
    it.value = st.value;
    copyStr(it.display, sizeof it.display, st.text);
    if (it.stale) st.sev = Sev::Unknown;  // reset hysteresis memory
    it.sev = st.sev;
  }
  for (size_t i = 0; i < cfg_->notifications.size(); i++, n++) {
    SnapItem& it = out.items[n];
    it.notification = true;
    it.hasThresholds = true;
    if (!everConnected_) continue;
    it.missing = false;
    if (!connected_) {
      it.stale = true;
      continue;
    }
    it.sev = noteSev_[i];
    copyStr(it.display, sizeof it.display, noteLabel(it.sev));
  }
  out.count = n;
}

}  // namespace sk
