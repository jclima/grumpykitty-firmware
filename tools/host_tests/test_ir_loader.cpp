#include <cassert>
#include <cstdio>
#include <string>
#include "../../src/app/app_09/infrared_stream.h"

static FILE* input(const std::string& s) { FILE* f = tmpfile(); assert(f); fwrite(s.data(), 1, s.size(), f); rewind(f); return f; }
static size_t failRead(void*, size_t, size_t, FILE*) { return 0; }
int main() {
  FILE* f = input("name: split\ntrailing"); infrared_stream::Reader r(f); int lines = 0; size_t bytes = 0;
  while (!r.step([&](const char*) { ++lines; }, 2)) {} bytes = r.bytes(); assert(lines == 2 && bytes == 20); fclose(f);
  f = input(std::string(8192, 'x') + "\n"); infrared_stream::Reader ok(f); while (!ok.step([](const char*) {})) {} assert(!ok.failed()); fclose(f);
  f = input(std::string(8193, 'x') + "\n"); infrared_stream::Reader bad(f); while (!bad.step([](const char*) {})) {} assert(bad.failed()); fclose(f);
  f = input("cancel"); infrared_stream::Reader cancelled(f); cancelled.cancel(); assert(cancelled.step([](const char*) {})); fclose(f);
  f = input("read failure"); infrared_stream::Reader readFailed(f, 4 * 1024 * 1024, failRead); assert(readFailed.step([](const char*) {})); assert(readFailed.failed()); fclose(f);
  f = input("budget"); infrared_stream::Reader budget(f, 3); while (!budget.step([](const char*) {})) {} assert(budget.failed() && budget.bytes() > 3); fclose(f);
  return 0;
}
