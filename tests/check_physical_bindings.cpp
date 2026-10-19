// Standalone (MSYS2 UCRT64, repo root):
// g++ -std=c++20 -Iinclude -Iextern/imgui tests/check_physical_bindings.cpp src/ui/keybinding_manager.cpp src/core/logger.cpp extern/imgui/imgui{,_draw,_tables,_widgets}.cpp -lSDL3 -o build/check_physical_bindings.exe && build/check_physical_bindings.exe
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "ui/keybinding_manager.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cassert>

// The only doubles: active-layout translation and the OS held-key snapshot.
std::array<bool, SDL_SCANCODE_COUNT> held{};
extern "C" const bool* SDLCALL SDL_GetKeyboardState(int* count) {
    *count = held.size();
    return held.data();
}
extern "C" SDL_Keycode SDLCALL SDL_GetKeyFromScancode(SDL_Scancode scan, SDL_Keymod, bool) {
    // AZERTY W/A and QWERTZ Y/Z; '/' is a shifted digit on QWERTZ.
    switch (scan) {
        case SDL_SCANCODE_W: return SDLK_Z;
        case SDL_SCANCODE_A: return SDLK_Q;
        case SDL_SCANCODE_Y: return SDLK_Z;
        case SDL_SCANCODE_Z: return SDLK_Y;
        case SDL_SCANCODE_SLASH: return SDLK_MINUS;
        default: return SDL_SCANCODE_TO_KEYCODE(scan);
    }
}
int main() {
    using Manager = wowee::ui::KeybindingManager;
    assert(SDL_Init(SDL_INIT_EVENTS));
    ImGui::CreateContext();
    auto manager = Manager::getInstance();
    for (const auto [scan, command] : {
             std::pair{SDL_SCANCODE_W, "MOVEFORWARD"}, {SDL_SCANCODE_A, "TURNLEFT"},
             {SDL_SCANCODE_Y, "TOGGLEACHIEVEMENT"}, {SDL_SCANCODE_Z, "TOGGLESHEATH"},
             {SDL_SCANCODE_SLASH, "OPENCHATSLASH"}, {SDL_SCANCODE_GRAVE, "TOGGLEAUTORUN"},
             {SDL_SCANCODE_RETURN, "OPENCHAT"}, {SDL_SCANCODE_KP_ENTER, "OPENCHAT"}}) {
        manager.beginInputFrame();
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.scancode = scan;
        event.key.key = SDL_GetKeyFromScancode(scan, SDL_KMOD_NONE, true);
        manager.processInputEvent(event);
        held[scan] = true;
        manager.updateInput();
        assert(manager.isCommandPressed(command));
        assert(manager.isCommandDown(command));
        held[scan] = false;
        event.type = SDL_EVENT_KEY_UP;
        manager.processInputEvent(event);
        manager.beginInputFrame();
        manager.updateInput();
        assert(!manager.isCommandPressed(command));
        assert(!manager.isCommandDown(command));
    }
    assert(manager.setBinding("SHIFT-W", "JUMP"));
    manager.beginInputFrame();
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_W;
    event.key.mod = SDL_KMOD_SHIFT;
    manager.processInputEvent(event);
    assert(manager.isCommandPressed("JUMP"));
    assert(!manager.isCommandPressed("MOVEFORWARD"));
    wowee::ui::noteInterfaceConsumedKey(ImGuiKey_W);
    assert(!manager.isCommandPressed("JUMP"));
    wowee::ui::clearInterfaceConsumedKeys();
    ImGui::DestroyContext();
    SDL_Quit();
}
