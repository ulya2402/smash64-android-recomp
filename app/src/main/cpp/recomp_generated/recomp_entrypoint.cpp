#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <android/log.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <array>
#include <cstdint>
#include <string>

#include "input_manager.h"
#include "n64_memory.h"
#include "n64_os_shim.h"
#include "oboe_audio.h"
#include "recomp_funcs.h"
#include "vulkan_renderer.h"

#define LOG_TAG "Smash64Main"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

int main(int argc, char* argv[]) {
    std::string gameDir = "/data/data/com.recomp.smash64/files/games/nalp";
    if (argc > 1 && argv[1] != nullptr) {
        gameDir = argv[1];
    }

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        LOGE("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    smash64::shim::RuntimeConfig cfg = smash64::shim::loadRuntimeConfig(gameDir);

    smash64::shim::N64Memory memory;
    if (!memory.init(cfg.romPath)) {
        LOGE("ROM not found or invalid at %s", cfg.romPath.c_str());
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Super Smash Bros. (NALP) - Native ARM64",
        1280, 720,
        SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        LOGE("SDL_CreateWindow failed: %s", SDL_GetError());
        memory.shutdown();
        SDL_Quit();
        return 1;
    }

    smash64::gfx::VulkanRenderer renderer;
    if (!renderer.init(window, cfg.shaderCacheDir, cfg.useTurnipDriver)) {
        LOGE("VulkanRenderer init failed");
        SDL_DestroyWindow(window);
        memory.shutdown();
        SDL_Quit();
        return 1;
    }

    smash64::audio::OboeAudioBackend audio;
    audio.start();

    smash64::input::InputManager input;
    input.init(cfg.gameDir);
    input.setTouchOverlayVisible(cfg.touchControlsEnabled);

    std::array<uint64_t, 32> gpr{};
    if (auto entry = smash64::recomp::lookupFunction(smash64::recomp::kNalpEntrypoint)) {
        entry(memory.rdram(), gpr.data());
    }

    bool running = true;
    bool showHud = true;
    bool editLayout = false;
    Uint64 lastTick = SDL_GetTicks();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            input.processEvent(ev);
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        int width = 0, height = 0;
        if (renderer.beginFrame(width, height)) {
            if (showHud) {
                ImGui::SetNextWindowPos(ImVec2(static_cast<float>(width) * 0.25f, 24.0f), ImGuiCond_FirstUseEver);
                ImGui::Begin("Smash 64 Recomp (NALP)", &showHud, ImGuiWindowFlags_AlwaysAutoResize);
                ImGui::Text("GPU: %s (%s)", renderer.activeGpuName(),
                            renderer.isUsingCustomTurnip() ? "Turnip" : "System");
                ImGui::Text("ROM: %zu MB loaded in RDRAM | Entry: 0x%08X",
                            memory.romSize() / (1024 * 1024),
                            smash64::recomp::kNalpEntrypoint);
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                                   "Runtime Shell Active: N64Recomp/RT64 game funcs not yet linked.");

                const auto st = input.state();
                ImGui::Text("N64 Input -> Stick: (%d, %d) | Btns: 0x%04X",
                            st.stickX, st.stickY, st.buttons);

                bool touchOn = input.touchOverlayVisible();
                if (ImGui::Checkbox("N64 Touch Overlay", &touchOn)) {
                    input.setTouchOverlayVisible(touchOn);
                    cfg.touchControlsEnabled = touchOn;
                    smash64::shim::saveRuntimeConfig(cfg);
                }
                ImGui::SameLine();
                ImGui::Checkbox("Customize Layout", &editLayout);

                if (editLayout) {
                    auto& l = input.layout();
                    ImGui::SliderFloat("Scale", &l.scale, 0.65f, 1.40f);
                    ImGui::SliderFloat("Opacity", &l.opacity, 0.25f, 1.0f);
                    ImGui::SliderFloat("Stick X", &l.stickX, 0.08f, 0.35f);
                    ImGui::SliderFloat("Stick Y", &l.stickY, 0.40f, 0.85f);
                    ImGui::SliderFloat("A/B X", &l.abX, 0.65f, 0.92f);
                    ImGui::SliderFloat("A/B Y", &l.abY, 0.45f, 0.85f);
                    ImGui::SliderFloat("C-Pad X", &l.cPadX, 0.62f, 0.90f);
                    ImGui::SliderFloat("C-Pad Y", &l.cPadY, 0.25f, 0.65f);
                    if (ImGui::Button("Save Touch Layout")) {
                        input.saveLayout();
                        editLayout = false;
                    }
                }

                if (ImGui::Button("Quit to Launcher")) {
                    running = false;
                }
                ImGui::End();
            }

            input.renderTouchOverlay(width, height, cfg.touchControlsEnabled, editLayout);
            renderer.endFrame();
        }

        if (cfg.fpsLimit > 0) {
            const Uint64 targetMs = 1000ULL / static_cast<Uint64>(cfg.fpsLimit);
            const Uint64 now = SDL_GetTicks();
            if (now - lastTick < targetMs) {
                SDL_Delay(static_cast<Uint32>(targetMs - (now - lastTick)));
            }
            lastTick = SDL_GetTicks();
        }
    }

    input.shutdown();
    audio.stop();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    memory.shutdown();
    SDL_Quit();
    return 0;
}
