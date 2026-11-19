#pragma once

#include <cstdint>
#include <string>

namespace smash64::shim {

struct RuntimeConfig {
    std::string gameDir;
    std::string romPath;
    std::string shaderCacheDir;
    int resolutionScale = 2;
    int fpsLimit = 60;
    bool useTurnipDriver = false;
    bool touchControlsEnabled = true;
};

RuntimeConfig loadRuntimeConfig(const std::string& gameDir);
void saveRuntimeConfig(const RuntimeConfig& cfg);
uint64_t osGetCount();

}  // namespace smash64::shim
