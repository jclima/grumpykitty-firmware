#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>

namespace bmi270_protocol {
constexpr std::size_t kInitChunkBytes = 128;
inline void initAddress(std::size_t offset, std::uint8_t out[2]) {
    out[0] = static_cast<std::uint8_t>((offset >> 1) & 0x0F);
    out[1] = static_cast<std::uint8_t>(offset >> 5);
}
inline bool initReady(std::uint8_t status) { return (status & 0x0F) == 1; }

template <typename SetAddress, typename WriteData>
inline bool upload(const std::uint8_t* data, std::size_t offset,
                   std::size_t length, SetAddress&& setAddress,
                   WriteData&& writeData) {
    if (!data) return false;
    while (length) {
        const std::size_t n = length < kInitChunkBytes ? length : kInitChunkBytes;
        std::uint8_t address[2];
        initAddress(offset, address);
        if (!setAddress(address) || !writeData(data, n)) return false;
        data += n;
        offset += n;
        length -= n;
    }
    return true;
}
}
