#include <cassert>
#include <cstdint>
#include <vector>
#include "../../src/app/app_09/infrared_file_utils.h"

int main() {
    using namespace infrared_file;
    std::vector<uint16_t> ok(kMaxRawDurations, 488);
    assert(validRaw(ok));
    ok.back() = 0;
    assert(!validRaw(ok));
    std::vector<uint16_t> tooMany(kMaxRawDurations + 1, 1);
    assert(!validRaw(tooMany));
    std::vector<uint16_t> tooLong(11, 50000);
    assert(!validRaw(tooLong));
    std::vector<uint16_t> boundary(8, 62500);
    assert(validRaw(boundary));
    std::vector<uint16_t> overflow(2, 65535);
    assert(validRaw(overflow));
    return 0;
}
