#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace smash64::input {

struct N64ControllerState {
    uint16_t buttons = 0;
    int8_t stickX = 0;
    int8_t stickY = 0;
};

enum N64ButtonMask : uint16_t {
    BTN_A = 0x8000,
    BTN_B = 0x4000,
    BTN_Z = 0x2000,
    BTN_START = 0x1000,
    BTN_DUP = 0x0800,
    BTN_DDOWN = 0x0400,
    BTN_DLEFT = 0x0200,
    BTN_DRIGHT = 0x0100,
    BTN_L = 0x0020,
    BTN_R = 0x0010,
    BTN_CUP = 0x0008,
    BTN_CDOWN = 0x0004,
    BTN_CLEFT = 0x0002,
    BTN_CRIGHT = 0x0001,
};

struct TouchPoint {
    float x = 0.0f;  // Normalized 0..1
    float y = 0.0f;
    bool active = false;
    int assignedRole = 0;  // 0=none, 1=analog, 2=buttons
};

struct TouchLayoutConfig {
    float stickX = 0.16f;
    float stickY = 0.68f;
    float abX = 0.84f;
    float abY = 0.70f;
    float cPadX = 0.78f;
    float cPadY = 0.42f;
    float dpadX = 0.30f;
    float dpadY = 0.82f;
    float scale = 1.0f;
    float opacity = 0.78f;
};

class InputManager {
public:
    void init(const std::string& gameDir);
    void shutdown();
    void processEvent(const SDL_Event& ev);
    void renderTouchOverlay(int displayW, int displayH, bool enabled, bool editMode);

    void loadLayout();
    void saveLayout() const;

    N64ControllerState state() const { return mergedState_; }
    bool hasPhysicalGamepad() const { return gamepad_ != nullptr; }
    bool touchOverlayVisible() const { return touchVisible_; }
    void setTouchOverlayVisible(bool v) { touchVisible_ = v; }
    TouchLayoutConfig& layout() { return layout_; }

private:
    void updateMergedState();
    bool isRealGamepad(SDL_JoystickID id) const;

    std::string layoutPath_;
    TouchLayoutConfig layout_{};
    SDL_Gamepad* gamepad_ = nullptr;
    N64ControllerState gamepadState_{};
    N64ControllerState touchState_{};
    N64ControllerState mergedState_{};
    std::unordered_map<SDL_FingerID, TouchPoint> activeTouches_;
    bool touchVisible_ = true;
};

}  // namespace smash64::input
