#pragma once

#include <cstdint>

namespace smash64::recomp {

using RecompFuncPtr = void (*)(uint8_t* rdram, uint64_t* gpr);

struct RecompSymbol {
    uint32_t vram;
    const char* name;
    RecompFuncPtr fn;
};

// Entrypoint for Super Smash Bros. (Europe) (En,Fr,De) [NALP]
constexpr uint32_t kNalpEntrypoint = 0x80100400u;

RecompFuncPtr lookupFunction(uint32_t vram);
void nalp_boot_entrypoint(uint8_t* rdram, uint64_t* gpr);

}  // namespace smash64::recomp
