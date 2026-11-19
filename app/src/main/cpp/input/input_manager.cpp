#include "input_manager.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>

namespace smash64::input {

bool InputManager::isRealGamepad(SDL_JoystickID id) const {
    const char* name = SDL_GetGamepadNameForID(id);
    if (!name) return false;
    std::string s(name);
    // Ignore Android virtual/sensor pseudo-controllers
    if (s.find("Sensor") != std::string::npos || s.find("Accelerometer") != std::string::npos) {
        return false;
    }
    return true;
}

void InputManager::init(const std::string& gameDir) {
    layoutPath_ = gameDir + "/touch_layout.cfg";
    loadLayout();

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids != nullptr) {
        for (int i = 0; i < count; ++i) {
            if (isRealGamepad(ids[i])) {
                gamepad_ = SDL_OpenGamepad(ids[i]);
                if (gamepad_) {
                    touchVisible_ = false;
                    break;
                }
            }
        }
        SDL_free(ids);
    }
}

void InputManager::shutdown() {
    if (gamepad_ != nullptr) {
        SDL_CloseGamepad(gamepad_);
        gamepad_ = nullptr;
    }
    activeTouches_.clear();
}

void InputManager::loadLayout() {
    std::ifstream in(layoutPath_);
    if (!in.is_open()) return;
    std::string key;
    float val = 0.0f;
    while (in >> key >> val) {
        if (key == "stickX") layout_.stickX = val;
        else if (key == "stickY") layout_.stickY = val;
        else if (key == "abX") layout_.abX = val;
        else if (key == "abY") layout_.abY = val;
        else if (key == "cPadX") layout_.cPadX = val;
        else if (key == "cPadY") layout_.cPadY = val;
        else if (key == "dpadX") layout_.dpadX = val;
        else if (key == "dpadY") layout_.dpadY = val;
        else if (key == "scale") layout_.scale = std::clamp(val, 0.6f, 1.5f);
        else if (key == "opacity") layout_.opacity = std::clamp(val, 0.25f, 1.0f);
    }
}

void InputManager::saveLayout() const {
    std::ofstream out(layoutPath_, std::ios::trunc);
    if (!out.is_open()) return;
    out << "stickX " << layout_.stickX << "\n";
    out << "stickY " << layout_.stickY << "\n";
    out << "abX " << layout_.abX << "\n";
    out << "abY " << layout_.abY << "\n";
    out << "cPadX " << layout_.cPadX << "\n";
    out << "cPadY " << layout_.cPadY << "\n";
    out << "dpadX " << layout_.dpadX << "\n";
    out << "dpadY " << layout_.dpadY << "\n";
    out << "scale " << layout_.scale << "\n";
    out << "opacity " << layout_.opacity << "\n";
}

void InputManager::processEvent(const SDL_Event& ev) {
    switch (ev.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gamepad_ && isRealGamepad(ev.gdevice.which)) {
                gamepad_ = SDL_OpenGamepad(ev.gdevice.which);
                if (gamepad_) {
                    touchVisible_ = false;
                }
            }
            break;

        case SDL_EVENT_GAMEPAD_REMOVED:
            if (gamepad_ && SDL_GetGamepadID(gamepad_) == ev.gdevice.which) {
                SDL_CloseGamepad(gamepad_);
                gamepad_ = nullptr;
                touchVisible_ = true;
            }
            break;

        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            const bool down = (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
            auto setBtn = [&](uint16_t mask) {
                if (down) gamepadState_.buttons |= mask;
                else gamepadState_.buttons &= ~mask;
            };
            switch (ev.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_SOUTH: setBtn(BTN_A); break;
                case SDL_GAMEPAD_BUTTON_WEST: setBtn(BTN_B); break;
                case SDL_GAMEPAD_BUTTON_NORTH: setBtn(BTN_CUP); break;
                case SDL_GAMEPAD_BUTTON_EAST: setBtn(BTN_CDOWN); break;
                case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: setBtn(BTN_L); break;
                case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: setBtn(BTN_R); break;
                case SDL_GAMEPAD_BUTTON_START: setBtn(BTN_START); break;
                case SDL_GAMEPAD_BUTTON_DPAD_UP: setBtn(BTN_DUP); break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN: setBtn(BTN_DDOWN); break;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT: setBtn(BTN_DLEFT); break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: setBtn(BTN_DRIGHT); break;
                default: break;
            }
            break;
        }

        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            auto scaleAxis = [](Sint16 v) -> int8_t {
                float norm = static_cast<float>(v) / 32767.0f;
                if (std::fabs(norm) < 0.12f) return 0;
                return static_cast<int8_t>(std::clamp(norm * 80.0f, -80.0f, 80.0f));
            };
            if (ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
                gamepadState_.stickX = scaleAxis(ev.gaxis.value);
            } else if (ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
                gamepadState_.stickY = static_cast<int8_t>(-scaleAxis(ev.gaxis.value));
            } else if (ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
                if (ev.gaxis.value > 8000) gamepadState_.buttons |= BTN_Z;
                else gamepadState_.buttons &= ~BTN_Z;
            }
            break;
        }

        case SDL_EVENT_FINGER_DOWN: {
            TouchPoint pt{};
            pt.x = ev.tfinger.x;
            pt.y = ev.tfinger.y;
            pt.active = true;
            pt.assignedRole = (pt.x < 0.40f && pt.y > 0.35f) ? 1 : 2;
            activeTouches_[ev.tfinger.fingerID] = pt;
            break;
        }

        case SDL_EVENT_FINGER_MOTION: {
            auto it = activeTouches_.find(ev.tfinger.fingerID);
            if (it != activeTouches_.end()) {
                it->second.x = ev.tfinger.x;
                it->second.y = ev.tfinger.y;
            }
            break;
        }

        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            activeTouches_.erase(ev.tfinger.fingerID);
            break;

        default:
            break;
    }

    updateMergedState();
}

void InputManager::renderTouchOverlay(bool enabled, bool editMode) {
    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    const float w = disp.x;
    const float h = disp.y;
    if (!enabled || !touchVisible_ || w <= 0.0f || h <= 0.0f) {
        return;
    }

    touchState_ = {};
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float unit = std::min(w, h) * layout_.scale;
    const int alpha = static_cast<int>(layout_.opacity * 255.0f);

    // 1. N64 Octagonal Analog Stick (Left)
    const ImVec2 stickCenter(w * layout_.stickX, h * layout_.stickY);
    const float stickRadius = unit * 0.145f;
    ImVec2 octPts[8];
    for (int i = 0; i < 8; ++i) {
        float ang = (static_cast<float>(i) * 3.14159265f / 4.0f) + (3.14159265f / 8.0f);
        octPts[i] = ImVec2(stickCenter.x + std::cos(ang) * stickRadius,
                           stickCenter.y + std::sin(ang) * stickRadius);
    }
    dl->AddConvexPolyFilled(octPts, 8, IM_COL32(20, 24, 34, alpha / 2));
    dl->AddPolyline(octPts, 8, IM_COL32(200, 210, 230, alpha), ImDrawFlags_Closed, 3.0f);

    ImVec2 knobPos = stickCenter;
    if (!editMode) {
        for (const auto& [id, tp] : activeTouches_) {
            (void)id;
            if (tp.assignedRole == 1) {
                float dx = tp.x * w - stickCenter.x;
                float dy = tp.y * h - stickCenter.y;
                float dist = std::sqrt(dx * dx + dy * dy);
                if (dist > stickRadius && dist > 0.001f) {
                    dx = dx * (stickRadius / dist);
                    dy = dy * (stickRadius / dist);
                }
                knobPos = ImVec2(stickCenter.x + dx, stickCenter.y + dy);
                touchState_.stickX = static_cast<int8_t>(std::clamp((dx / stickRadius) * 80.0f, -80.0f, 80.0f));
                touchState_.stickY = static_cast<int8_t>(std::clamp((-dy / stickRadius) * 80.0f, -80.0f, 80.0f));
                break;
            }
        }
    }
    dl->AddCircleFilled(knobPos, stickRadius * 0.44f, IM_COL32(215, 222, 235, alpha), 24);
    dl->AddCircle(knobPos, stickRadius * 0.44f, IM_COL32(255, 255, 255, alpha), 24, 2.0f);

    // 2. Super Smash Bros. N64 Color-Coded Action Buttons
    struct VButton {
        const char* label;
        ImVec2 pos;
        float radius;
        uint16_t mask;
        ImU32 fill;
        ImU32 border;
    };

    const float rMain = unit * 0.072f;
    const float rC = unit * 0.048f;
    const float rTrig = unit * 0.056f;
    const float rDpad = unit * 0.044f;

    const ImVec2 abCenter(w * layout_.abX, h * layout_.abY);
    const ImVec2 cCenter(w * layout_.cPadX, h * layout_.cPadY);
    const ImVec2 dCenter(w * layout_.dpadX, h * layout_.dpadY);

    const VButton buttons[] = {
        // A (Blue - Normal/Smash) & B (Green - Special)
        {"A", ImVec2(abCenter.x + rMain * 0.65f, abCenter.y - rMain * 0.35f), rMain * 1.10f, BTN_A,
         IM_COL32(30, 136, 229, alpha), IM_COL32(144, 202, 249, alpha)},
        {"B", ImVec2(abCenter.x - rMain * 0.95f, abCenter.y + rMain * 0.55f), rMain * 0.96f, BTN_B,
         IM_COL32(67, 160, 71, alpha), IM_COL32(165, 214, 167, alpha)},

        // Yellow C-Buttons (Jump / Quick Smash)
        {"C^", ImVec2(cCenter.x, cCenter.y - rC * 1.65f), rC, BTN_CUP,
         IM_COL32(253, 216, 53, alpha), IM_COL32(255, 245, 157, alpha)},
        {"Cv", ImVec2(cCenter.x, cCenter.y + rC * 1.65f), rC, BTN_CDOWN,
         IM_COL32(253, 216, 53, alpha), IM_COL32(255, 245, 157, alpha)},
        {"C<", ImVec2(cCenter.x - rC * 1.65f, cCenter.y), rC, BTN_CLEFT,
         IM_COL32(253, 216, 53, alpha), IM_COL32(255, 245, 157, alpha)},
        {"C>", ImVec2(cCenter.x + rC * 1.65f, cCenter.y), rC, BTN_CRIGHT,
         IM_COL32(253, 216, 53, alpha), IM_COL32(255, 245, 157, alpha)},

        // L / R / Z (Shield & Grab)
        {"L", ImVec2(w * 0.07f, h * 0.16f), rTrig, BTN_L,
         IM_COL32(96, 108, 128, alpha), IM_COL32(176, 190, 197, alpha)},
        {"R", ImVec2(w * 0.93f, h * 0.16f), rTrig, BTN_R,
         IM_COL32(96, 108, 128, alpha), IM_COL32(176, 190, 197, alpha)},
        {"Z", ImVec2(w * 0.93f, h * 0.36f), rTrig, BTN_Z,
         IM_COL32(69, 90, 100, alpha), IM_COL32(144, 164, 174, alpha)},

        // START (N64 Red)
        {"START", ImVec2(w * 0.50f, h * 0.10f), rTrig * 0.85f, BTN_START,
         IM_COL32(229, 57, 53, alpha), IM_COL32(255, 138, 128, alpha)},

        // D-Pad (Taunt / Menu)
        {"^", ImVec2(dCenter.x, dCenter.y - rDpad * 1.55f), rDpad, BTN_DUP,
         IM_COL32(55, 62, 75, alpha), IM_COL32(140, 150, 165, alpha)},
        {"v", ImVec2(dCenter.x, dCenter.y + rDpad * 1.55f), rDpad, BTN_DDOWN,
         IM_COL32(55, 62, 75, alpha), IM_COL32(140, 150, 165, alpha)},
        {"<", ImVec2(dCenter.x - rDpad * 1.55f, dCenter.y), rDpad, BTN_DLEFT,
         IM_COL32(55, 62, 75, alpha), IM_COL32(140, 150, 165, alpha)},
        {">", ImVec2(dCenter.x + rDpad * 1.55f, dCenter.y), rDpad, BTN_DRIGHT,
         IM_COL32(55, 62, 75, alpha), IM_COL32(140, 150, 165, alpha)},
    };

    for (const auto& b : buttons) {
        bool pressed = false;
        if (!editMode) {
            for (const auto& [id, tp] : activeTouches_) {
                (void)id;
                float dx = tp.x * w - b.pos.x;
                float dy = tp.y * h - b.pos.y;
                if (dx * dx + dy * dy <= (b.radius * 1.25f) * (b.radius * 1.25f)) {
                    pressed = true;
                    touchState_.buttons |= b.mask;
                }
            }
        }
        dl->AddCircleFilled(b.pos, b.radius, pressed ? IM_COL32(255, 255, 255, 220) : b.fill, 24);
        dl->AddCircle(b.pos, b.radius, b.border, 24, 2.5f);
        ImVec2 txtSz = ImGui::CalcTextSize(b.label);
        dl->AddText(ImVec2(b.pos.x - txtSz.x * 0.5f, b.pos.y - txtSz.y * 0.5f),
                    IM_COL32(255, 255, 255, 245), b.label);
    }

    updateMergedState();
}

void InputManager::updateMergedState() {
    mergedState_.buttons = gamepadState_.buttons | touchState_.buttons;
    mergedState_.stickX = (gamepadState_.stickX != 0) ? gamepadState_.stickX : touchState_.stickX;
    mergedState_.stickY = (gamepadState_.stickY != 0) ? gamepadState_.stickY : touchState_.stickY;
}

}  // namespace smash64::input
