#include "recomp_funcs.h"

#include <array>

namespace smash64::recomp {

namespace {

constexpr std::array<RecompSymbol, 1> kSymbolTable = {{
    {kNalpEntrypoint, "nalp_boot_entrypoint", &nalp_boot_entrypoint},
}};

}  // namespace

RecompFuncPtr lookupFunction(uint32_t vram) {
    for (const auto& sym : kSymbolTable) {
        if (sym.vram == vram) {
            return sym.fn;
        }
    }
    return nullptr;
}

void nalp_boot_entrypoint(uint8_t* rdram, uint64_t* gpr) {
    // Dispatches N64ModernRuntime / N64Recomp generated functions once NALP ELF symbols are linked.
    (void)rdram;
    if (gpr != nullptr) {
        gpr[29] = 0x800F0000ULL;  // Initial MIPS $sp
    }
}

}  // namespace smash64::recomp
