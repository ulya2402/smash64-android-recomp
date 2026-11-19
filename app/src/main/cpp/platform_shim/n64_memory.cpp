#include "n64_memory.h"

#include <android/log.h>
#include <sys/mman.h>

#include <cstdio>
#include <cstring>

#define LOG_TAG "Smash64Memory"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace smash64::shim {

bool N64Memory::init(const std::string& romPath) {
    void* rdramMap = mmap(nullptr, kRdramSize, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (rdramMap == MAP_FAILED) {
        LOGE("mmap failed for RDRAM");
        return false;
    }
    rdram_ = static_cast<uint8_t*>(rdramMap);
    std::memset(rdram_, 0, kRdramSize);

    FILE* f = std::fopen(romPath.c_str(), "rb");
    if (!f) {
        LOGE("Failed to open ROM at %s", romPath.c_str());
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::rewind(f);

    if (sz <= 0 || static_cast<size_t>(sz) > kRomMaxSize) {
        LOGE("Invalid ROM size: %ld", sz);
        std::fclose(f);
        return false;
    }

    romSize_ = static_cast<size_t>(sz);
    void* romMap = mmap(nullptr, romSize_, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (romMap == MAP_FAILED) {
        LOGE("mmap failed for ROM");
        std::fclose(f);
        return false;
    }
    rom_ = static_cast<uint8_t*>(romMap);

    size_t readBytes = std::fread(rom_, 1, romSize_, f);
    std::fclose(f);
    if (readBytes != romSize_) {
        LOGE("Short read on ROM: %zu / %zu", readBytes, romSize_);
        return false;
    }

    // Copy initial 1MB boot segment to RDRAM entrypoint 0x80100400 (Super Smash Bros PAL)
    const size_t bootCopySize = romSize_ > 0x101000 ? 0x100000 : (romSize_ > 0x1000 ? romSize_ - 0x1000 : 0);
    if (bootCopySize > 0) {
        std::memcpy(rdram_ + 0x100400, rom_ + 0x1000, bootCopySize);
    }

    LOGI("N64Memory initialized: RDRAM=%zu MB, ROM=%zu bytes", kRdramSize / (1024 * 1024), romSize_);
    return true;
}

void N64Memory::shutdown() {
    if (rom_ != nullptr) {
        munmap(rom_, romSize_);
        rom_ = nullptr;
        romSize_ = 0;
    }
    if (rdram_ != nullptr) {
        munmap(rdram_, kRdramSize);
        rdram_ = nullptr;
    }
}

}  // namespace smash64::shim
