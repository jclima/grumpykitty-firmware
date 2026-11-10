#include <cassert>
#include <cstdint>
#include <vector>
#include "../../src/bsp/imu/BMI270_protocol.hpp"

int main() {
    std::uint8_t address[2]{};
    bmi270_protocol::initAddress(0, address);
    assert(address[0] == 0 && address[1] == 0);
    bmi270_protocol::initAddress(128, address);
    assert(address[0] == 0 && address[1] == 4);
    bmi270_protocol::initAddress(8192 - 128, address);
    assert(address[0] == 0 && address[1] == 252);
    assert(bmi270_protocol::initReady(0x01));
    assert(bmi270_protocol::initReady(0xF1));
    assert(!bmi270_protocol::initReady(0x02));
    assert(!bmi270_protocol::initReady(0x00));

    std::vector<std::size_t> sizes;
    std::vector<std::uint8_t> uploaded;
    std::vector<std::uint8_t> hi;
    std::vector<std::uint8_t> lo;
    std::uint8_t image[8192]{};
    for (std::size_t i = 0; i < sizeof(image); ++i) image[i] = static_cast<std::uint8_t>(i);
    assert(bmi270_protocol::upload(
        image, 0, sizeof(image),
        [&](const std::uint8_t a[2]) { lo.push_back(a[0]); hi.push_back(a[1]); return true; },
        [&](const std::uint8_t* p, std::size_t n) {
            sizes.push_back(n);
            uploaded.insert(uploaded.end(), p, p + n);
            return true;
        }));
    assert(sizes.size() == 64);
    assert(uploaded.size() == sizeof(image));
    for (std::size_t i = 0; i < sizeof(image); ++i) assert(uploaded[i] == image[i]);
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        assert(sizes[i] == 128);
        assert(lo[i] == static_cast<std::uint8_t>(((i * 128) >> 1) & 15));
        assert(hi[i] == static_cast<std::uint8_t>((i * 128) >> 5));
    }

    int calls = 0;
    assert(!bmi270_protocol::upload(
        image, 0, sizeof(image),
        [&](const std::uint8_t[2]) { return ++calls != 4; },
        [&](const std::uint8_t*, std::size_t) { return true; }));
    assert(calls == 4);
    calls = 0;
    assert(!bmi270_protocol::upload(
        image, 0, sizeof(image),
        [&](const std::uint8_t[2]) { return true; },
        [&](const std::uint8_t*, std::size_t) { return ++calls != 4; }));
    assert(calls == 4);
}
