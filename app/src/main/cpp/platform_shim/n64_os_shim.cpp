#include "n64_os_shim.h"

#include <ctime>
#include <fstream>
#include <sstream>

namespace smash64::shim {

RuntimeConfig loadRuntimeConfig(const std::string& gameDir) {
    RuntimeConfig cfg;
    cfg.gameDir = gameDir;
    cfg.romPath = gameDir + "/rom.z64";
    cfg.shaderCacheDir = gameDir + "/shader_cache";

    std::ifstream in(gameDir + "/args.txt");
    if (!in.is_open()) {
        return cfg;
    }

    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key == "resolution_scale") cfg.resolutionScale = std::stoi(val);
        else if (key == "fps_limit") cfg.fpsLimit = std::stoi(val);
        else if (key == "vulkan_driver") cfg.useTurnipDriver = (val == "turnip");
        else if (key == "touch_controls") cfg.touchControlsEnabled = (val == "1" || val == "true");
    }
    return cfg;
}

void saveRuntimeConfig(const RuntimeConfig& cfg) {
    std::ofstream out(cfg.gameDir + "/args.txt", std::ios::trunc);
    if (!out.is_open()) return;
    out << "resolution_scale=" << cfg.resolutionScale << "\n";
    out << "fps_limit=" << cfg.fpsLimit << "\n";
    out << "vulkan_driver=" << (cfg.useTurnipDriver ? "turnip" : "system") << "\n";
    out << "touch_controls=" << (cfg.touchControlsEnabled ? "1" : "0") << "\n";
}

uint64_t osGetCount() {
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    // VR4300 counter ticks at 46.875 MHz (half of 93.75 MHz CPU clock)
    uint64_t ns = static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
    return (ns * 46875ULL) / 1000000ULL;
}

}  // namespace smash64::shim
