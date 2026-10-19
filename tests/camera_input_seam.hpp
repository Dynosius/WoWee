#pragma once

// cmake --build build --target check_camera_walk
// ctest --test-dir build -R '^camera_walk$' --output-on-failure
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "rendering/camera_controller.hpp"
#include "ui/keybinding_manager.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

namespace camera_input_test {
extern float speed;
extern bool moving;

// Feed real native binding events and the raw Input snapshot used for Ctrl.
// This avoids ImGui trickling and requires no SDL window or game assets.
inline void key(SDL_Scancode scan, bool down, SDL_Keymod mods = SDL_KMOD_NONE,
                bool repeat = false) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scan;
    event.key.mod = mods;
    event.key.repeat = repeat;
    wowee::ui::KeybindingManager::getInstance().processInputEvent(event);
    wowee::core::Input::getInstance().setVirtualKey(scan, down);
}
inline void frame(wowee::rendering::CameraController& controller) {
    wowee::core::Input::getInstance().update();
    controller.update(1.0f / 60.0f);
    wowee::ui::KeybindingManager::getInstance().beginInputFrame();
}
inline void expectSpeed(float expected) {
    if (std::abs(speed - expected) >= 0.0001f)
        std::fprintf(stderr, "Expected camera speed %.2f, got %.2f\n", expected, speed);
    assert(std::abs(speed - expected) < 0.0001f);
}
} // namespace camera_input_test
