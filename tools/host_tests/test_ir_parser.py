#!/usr/bin/env python3
"""Host regression for the production Flipper IR line parser.

This deliberately extracts the parser methods from infrared.cpp rather than
reimplementing them in the test.  The small generated translation unit stubs
only the hardware/application surface those methods need.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/app/app_09/infrared.h"
SOURCE = ROOT / "src/app/app_09/infrared.cpp"
UTILS = ROOT / "src/app/app_09/infrared_file_utils.h"
STREAM = ROOT / "src/app/app_09/infrared_stream.h"


def extract_struct(text: str, name: str) -> str:
    m = re.search(r"struct\s+" + name + r"\s*\{", text)
    if not m:
        raise RuntimeError(f"production struct {name} not found")
    end = text.find("};", m.end())
    if end < 0:
        raise RuntimeError(f"production struct {name} has no closing brace")
    return text[m.start() : end + 2]


def extract_function(text: str, needle: str) -> str:
    start = text.find(needle)
    if start < 0:
        raise RuntimeError(f"production definition {needle!r} not found")
    brace = text.find("{", start)
    if brace < 0:
        raise RuntimeError(f"production definition {needle!r} has no body")
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start : i + 1]
    raise RuntimeError(f"production definition {needle!r} is unterminated")


def source() -> str:
    h = HEADER.read_text()
    cpp = SOURCE.read_text()
    signal = extract_struct(h, "IrSignal")
    remote = extract_struct(h, "IrRemote")
    methods = "\n\n".join(
        extract_function(cpp, needle)
        for needle in (
            "void App09::_resetLoadSignal(IrSignal& s)",
            "void App09::_commitLoadSignal()",
            "void App09::_parseLoadLine(const char* raw)",
        )
    )
    valid = extract_function(cpp, "static bool signalImportValid(const IrSignal& sig)")
    return f'''#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include <strings.h>
#include "{UTILS}"
#include "{STREAM}"

enum decode_type_t : int {{ UNKNOWN = 0, NEC = 1 }};
{signal}
{remote}

static decode_type_t flipperProtoToType(const char* p) {{
  return p && !strcasecmp(p, "NEC") ? NEC : UNKNOWN;
}}
static bool flipperEncodeFrame(const char*, uint32_t, uint32_t, uint64_t&, uint16_t&) {{ return false; }}

class App09 {{
public:
  IrSignal _loadSignal;
  IrRemote _loadResult;
  char _loadFilter[32]{{}};
  char _loadProto[24]{{}};
  size_t _loadRawBytes = 0;
  uint32_t _loadRawUsec = 0;
  int _loadMaxSignals = 0;
  bool _loadInSignal = false;
  bool _loadValidSignal = true;
  bool _loadSawValue = false;
  bool _loadSawBits = false;
  bool _loadFailed = false;
  const char* _loadError = nullptr;
  void _resetLoadSignal(IrSignal&);
  void _commitLoadSignal();
  void _parseLoadLine(const char*);
}};

{valid}
{methods}

static FILE* input(const std::string& s) {{
  FILE* f = tmpfile(); assert(f); fwrite(s.data(), 1, s.size(), f); rewind(f); return f;
}}
static void reset(App09& a, int cap = 0, const char* filter = "") {{
  a._loadResult.signals.clear(); a._loadMaxSignals = cap;
  strncpy(a._loadFilter, filter, sizeof(a._loadFilter) - 1);
  a._loadFilter[sizeof(a._loadFilter) - 1] = 0;
  a._loadFailed = false; a._loadError = nullptr; a._loadInSignal = false;
  a._loadRawBytes = 0; a._loadRawUsec = 0; a._loadValidSignal = true;
  a._resetLoadSignal(a._loadSignal);
}}
static void parse(App09& a, const std::string& text) {{
  FILE* f = input(text); infrared_stream::Reader reader(f);
  while (!reader.step([&](const char* line) {{ a._parseLoadLine(line); }})) {{}}
  if (!reader.failed() && a._loadInSignal && !a._loadFailed) a._commitLoadSignal();
  fclose(f);
}}
static std::string raw(const char* name, const char* data = " 100 200") {{
  return std::string("name: ") + name + "\\ntype: raw\\nfrequency: 38000\\ndata:" + data + "\\n";
}}
int main() {{
  App09 a;
  reset(a); parse(a, raw("one").substr(0, raw("one").size() - 1));
  assert(!a._loadFailed && a._loadResult.signals.size() == 1);
  assert(a._loadResult.signals[0].name[0] == 'o' && a._loadResult.signals[0].rawData[1] == 200);

  reset(a); parse(a, raw("one") + raw("two"));
  assert(a._loadResult.signals.size() == 2);

  reset(a, 0, "wanted"); parse(a, raw("other") + raw("wanted"));
  assert(a._loadResult.signals.size() == 1 && !strcmp(a._loadResult.signals[0].name, "wanted"));

  reset(a); parse(a, raw("bad", " 100 nope")); assert(a._loadResult.signals.empty());
  reset(a); parse(a, raw("boundary", " 65535 65535 65535 65535 65535 65535 65535 41255"));
  assert(!a._loadFailed && a._loadResult.signals.size() == 1 && a._loadResult.signals[0].rawData.size() == 8);
  reset(a); parse(a, raw("long", " 62501 62501 62501 62501 62501 62501 62501 62501")); assert(a._loadResult.signals.empty());
  reset(a); parse(a, "name: parsed\\ntype: parsed\\nprotocol: NEC\\n" + raw("raw-after"));
  assert(a._loadResult.signals.size() == 1 && a._loadResult.signals[0].isRaw && a._loadResult.signals[0].protocol == UNKNOWN);

  reset(a); std::string many; for (int i = 0; i < 256; ++i) many += raw((std::string("s") + std::to_string(i)).c_str());
  parse(a, many); assert(!a._loadFailed && a._loadResult.signals.size() == 256);
  assert(a._loadRawBytes == 256 * 2 * sizeof(uint16_t));
  assert(a._loadResult.signals.front().rawData.size() == 2 && a._loadResult.signals.back().rawData[0] == 100);
  reset(a); many += raw("overflow"); parse(a, many);
  assert(a._loadFailed && a._loadResult.signals.size() == 256);
  reset(a, 256, "wanted"); many.clear();
  for (int i = 0; i < 300; ++i) many += raw("other");
  for (int i = 0; i < 256; ++i) many += raw("wanted");
  parse(a, many); assert(!a._loadFailed && a._loadResult.signals.size() == 256);
  many += raw("wanted"); reset(a, 256, "wanted"); parse(a, many);
  assert(a._loadFailed && a._loadResult.signals.size() == 256);
  return 0;
}}
'''


def main() -> int:
    try:
        with tempfile.TemporaryDirectory() as d:
            tu = Path(d) / "ir_parser.cpp"
            exe = Path(d) / "ir_parser"
            tu.write_text(source())
            cxx = __import__("os").environ.get("CXX", "c++")
            subprocess.run([cxx, "-std=c++17", "-Wall", "-Wextra", str(tu), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
    except (subprocess.CalledProcessError, RuntimeError) as e:
        print(f"ir parser host test failed: {e}", file=sys.stderr)
        return 1
    print("ir parser host test: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
