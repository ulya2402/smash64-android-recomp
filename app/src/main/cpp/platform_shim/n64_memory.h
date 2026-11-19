#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace smash64::shim {

class N64Memory {
public:
    static constexpr size_t kRdramSize = 8 * 1024 * 1024;  // 8 MB (with Expansion Pak)
    static constexpr size_t kRomMaxSize = 64 * 1024 * 1024;

    bool init(const std::string& romPath);
    void shutdown();

    uint8_t* rdram() const { return rdram_; }
    uint8_t* rom() const { return rom_; }
    size_t romSize() const { return romSize_; }

    inline uint32_t readU32BE(uint32_t vaddr) const {
        const uint32_t phys = vaddr & 0x007FFFFCu;
        uint32_t val = *reinterpret_cast<const uint32_t*>(rdram_ + phys);
        return __builtin_bswap32(val);
    }

    inline void writeU32BE(uint32_t vaddr, uint32_t val) {
        const uint32_t phys = vaddr & 0x007FFFFCu;
        *reinterpret_cast<uint32_t*>(rdram_ + phys) = __builtin_bswap32(val);
    }

    inline uint16_t readU16BE(uint32_t vaddr) const {
        const uint32_t phys = vaddr & 0x007FFFFEu;
        uint16_t val = *reinterpret_cast<const uint16_t*>(rdram_ + phys);
        return __builtin_bswap16(val);
    }

    inline void writeU16BE(uint32_t vaddr, uint16_t val) {
        const uint32_t phys = vaddr & 0x007FFFFEu;
        *reinterpret_cast<uint16_t*>(rdram_ + phys) = __builtin_bswap16(val);
    }

private:
    uint8_t* rdram_ = nullptr;
    uint8_t* rom_ = nullptr;
    size_t romSize_ = 0;
};

}  // namespace smash64::shim
