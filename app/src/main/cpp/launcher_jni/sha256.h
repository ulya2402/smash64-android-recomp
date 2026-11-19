#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace smash64::crypto {

class Sha256 {
public:
    Sha256();
    void update(const uint8_t* data, size_t len);
    std::string finalHex();

private:
    void transform(const uint8_t block[64]);

    uint32_t state_[8]{};
    uint64_t bitlen_ = 0;
    uint8_t buffer_[64]{};
    size_t buffer_len_ = 0;
};

}  // namespace smash64::crypto
