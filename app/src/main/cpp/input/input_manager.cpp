#include "input_manager.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace smash64::input {

void InputManager::init() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids != nullptr) {
        if (count > 0) {
            gamepad_ = SDL_OpenGamepad(ids[0]);
            if (gamepad_) {
                touchVisible_ = false;
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

void InputManager::processEvent(const SDL_Event& ev) {
    switch (ev.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gamepad_) {
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
            pt.assignedRole = (pt.x < 0.45f) ? 1 : 2;
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

void InputManager::renderTouchOverlay(int displayW, int displayH, bool enabled) {
    if (!enabled || !touchVisible_ || displayW <= 0 || displayH <= 0) {
        return;
    }

    touchState_ = {};
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float w = static_cast<float>(displayW);
    const float h = static_cast<float>(displayH);

    // Virtual Analog Stick (Bottom-Left)
    const ImVec2 stickCenter(w * 0.16f, h * 0.72f);
    const float stickRadius = std::min(w, h) * 0.13f;
    dl->AddCircle(stickCenter, stickRadius, IM_COL32(255, 255, 255, 110), 32, 3.0f);

    ImVec2 knobPos = stickCenter;
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
    dl->AddCircleFilled(knobPos, stickRadius * 0.42f, IM_COL32(220, 230, 255, 150), 24);

    // Action Buttons (Bottom-Right): A, B, Z, R, Start
    struct VButton {
        const char* label;
        ImVec2 pos;
        float radius;
        uint16_t mask;
        ImU32 color;
    };
    const float btnR = std::min(w, h) * 0.065f;
    const VButton buttons[] = {
        {"A", ImVec2(w * 0.84f, h * 0.72f), btnR * 1.15f, BTN_A, IM_COL32(45, 130, 240, 170)},
        {"B", ImVec2(w * 0.73f, h * 0.78f), btnR, BTN_B, IM_COL32(40, 185, 95, 170)},
        {"Z", ImVec2(w * 0.74f, h * 0.56f), btnR * 0.9f, BTN_Z, IM_COL32(150, 155, 175, 160)},
        {"R", ImVec2(w * 0.88f, h * 0.50f), btnR * 0.9f, BTN_R, IM_COL32(150, 155, 175, 160)},
        {"START", ImVec2(w * 0.50f, h * 0.12f), btnR * 0.85f, BTN_START, IM_COL32(220, 60, 60, 160)},
    };

    for (const auto& b : buttons) {
        bool pressed = false;
        for (const auto& [id, tp] : activeTouches_) {
            (void)id;
            float dx = tp.x * w - b.pos.x;
            float dy = tp.y * h - b.pos.y;
            if (dx * dx + dy * dy <= (b.radius * 1.25f) * (b.radius * 1.25f)) {
                pressed = true;
                touchState_.buttons |= b.mask;
            }
        }
        dl->AddCircleFilled(b.pos, b.radius, pressed ? IM_COL32(255, 255, 255, 200) : b.color, 24);
        dl->AddText(ImVec2(b.pos.x - 6.0f, b.pos.y - 7.0f), IM_COL32(255, 255, 255, 235), b.label);
    }

    updateMergedState();
}

void InputManager::updateMergedState() {
    mergedState_.buttons = gamepadState_.buttons | touchState_.buttons;
    mergedState_.stickX = (gamepadState_.stickX != 0) ? gamepadState_.stickX : touchState_.stickX;
    mergedState_.stickY = (gamepadState_.stickY != 0) ? gamepadState_.stickY : touchState_.stickY;
}

}  // namespace smash64::input
