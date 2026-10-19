// Run: cmake --build build --target check_input_edges && ctest --test-dir build -R '^input_edges$' --output-on-failure
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "ui/keybinding_manager.hpp"
#include <SDL3/SDL.h>
#include <cassert>

int main() {
    assert(SDL_Init(SDL_INIT_EVENTS));
    ImGui::CreateContext();
    auto manager = wowee::ui::KeybindingManager::getInstance();
    const auto key = [&](SDL_Scancode scan, bool down, SDL_Keymod mods = SDL_KMOD_NONE, bool repeat = false) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.scancode = scan;
        event.key.mod = mods;
        event.key.repeat = repeat;
        assert(SDL_PushEvent(&event));
    };
    const auto pump = [&] {
        SDL_Event event;
        while (SDL_PollEvent(&event)) manager.processInputEvent(event);
    };
    // Both Enter keys open chat, but a focused edit box can consume either.
    for (const auto scan : {SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER}) {
        manager.beginInputFrame();
        key(scan, true);
        key(scan, false);
        pump();
        assert(manager.isCommandPressed("OPENCHAT"));
        wowee::ui::noteInterfaceConsumedKey(scan == SDL_SCANCODE_RETURN ? ImGuiKey_Enter : ImGuiKey_KeypadEnter);
        assert(!manager.isCommandPressed("OPENCHAT"));
        wowee::ui::clearInterfaceConsumedKeys();
    }
    manager.setKeyForAction(wowee::ui::KeybindingManager::Action::TOGGLE_CHAT, ImGuiKey_F8);
    manager.beginInputFrame();
    key(SDL_SCANCODE_KP_ENTER, true);
    key(SDL_SCANCODE_KP_ENTER, false);
    pump();
    assert(!manager.isCommandPressed("OPENCHAT")); // Rebinding has no hardcoded fallback.
    // Unassigned modifier chords retain the bare-key action, even if every
    // modifier/key is released before the snapshot is sampled.
    for (const auto mods : {SDL_KMOD_SHIFT, SDL_KMOD_CTRL, SDL_KMOD_ALT,
                           SDL_KMOD_CTRL | SDL_KMOD_SHIFT, SDL_KMOD_ALT | SDL_KMOD_CTRL | SDL_KMOD_SHIFT}) {
        for (const auto [scan, command] : {
                 std::pair{SDL_SCANCODE_SPACE, "JUMP"}, {SDL_SCANCODE_TAB, "TARGETNEARESTENEMY"},
                 {SDL_SCANCODE_ESCAPE, "TOGGLEGAMEMENU"}, {SDL_SCANCODE_Z, "TOGGLESHEATH"},
                 {SDL_SCANCODE_NUMLOCKCLEAR, "TOGGLEAUTORUN"}, {SDL_SCANCODE_C, "TOGGLECHARACTER0"},
                 {SDL_SCANCODE_Y, "TOGGLEACHIEVEMENT"}, {SDL_SCANCODE_M, "TOGGLEWORLDMAP"}}) {
            manager.beginInputFrame();
            if (mods & SDL_KMOD_SHIFT) key(SDL_SCANCODE_LSHIFT, true, mods);
            if (mods & SDL_KMOD_CTRL) key(SDL_SCANCODE_LCTRL, true, mods);
            if (mods & SDL_KMOD_ALT) key(SDL_SCANCODE_LALT, true, mods);
            key(scan, true, mods);
            key(scan, false, mods);
            key(SDL_SCANCODE_LSHIFT, false);
            key(SDL_SCANCODE_LCTRL, false);
            key(SDL_SCANCODE_LALT, false);
            pump();
            manager.updateInput();
            assert(manager.isCommandPressed(command));
            manager.beginInputFrame();
            assert(!manager.isCommandPressed(command));
        }
    }
    manager.bindings().clear();
    manager.bindings()["PLAIN"] = {"C", "K"};
    manager.bindings()["CHORD"] = {"SHIFT-C", "SHIFT-K"};
    manager.bindings()["EXACT"] = {"CTRL-SHIFT-K", ""};
    manager.bindings()["RIGHT"] = {"BUTTON2", ""};
    manager.bindings()["MIDDLE"] = {"BUTTON3", ""};
    const auto mouse = [&](Uint8 button, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = button;
        assert(SDL_PushEvent(&event));
    };
    for (const auto button : {SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE}) {
        const char* command = button == SDL_BUTTON_RIGHT ? "RIGHT" : "MIDDLE";
        const char* other = button == SDL_BUTTON_RIGHT ? "MIDDLE" : "RIGHT";
        manager.beginInputFrame();
        mouse(button, true);
        pump();
        assert(manager.isCommandPressed(command));
        assert(manager.isCommandDown(command));
        assert(!manager.isCommandPressed(other));
        assert(!manager.isCommandDown(other));
        manager.beginInputFrame();
        assert(!manager.isCommandPressed(command));
        assert(manager.isCommandDown(command));
        assert(!manager.isCommandDown(other));
        mouse(button, false);
        pump();
        assert(!manager.isCommandDown(command));

        // A complete mouse tap in one pump retains only its own press edge.
        manager.beginInputFrame();
        mouse(button, true);
        mouse(button, false);
        pump();
        manager.updateInput();
        assert(manager.isCommandPressed(command));
        assert(!manager.isCommandPressed(other));
        assert(!manager.isCommandDown(command));
        assert(!manager.isCommandDown(other));
        manager.beginInputFrame();
        assert(!manager.isCommandPressed(command));
        assert(!manager.isCommandPressed(other));
    }

    manager.beginInputFrame();
    key(SDL_SCANCODE_C, true);
    key(SDL_SCANCODE_C, false);
    pump();
    manager.updateInput(); // final snapshot is up, but the press survives
    assert(manager.isCommandPressed("PLAIN"));
    assert(!manager.isCommandDown("PLAIN"));
    manager.beginInputFrame();
    manager.updateInput();
    assert(!manager.isCommandPressed("PLAIN"));

    // Only the action-key edge selects a chord, never a modifier transition.
    manager.beginInputFrame();
    key(SDL_SCANCODE_K, true);
    key(SDL_SCANCODE_LSHIFT, true, SDL_KMOD_SHIFT);
    pump();
    assert(manager.isCommandPressed("PLAIN")); // secondary slot; edge preceded Shift
    assert(!manager.isCommandPressed("CHORD"));
    manager.beginInputFrame();
    key(SDL_SCANCODE_K, true, SDL_KMOD_SHIFT, true); // OS repeat is not a new press
    pump();
    assert(!manager.isCommandPressed("PLAIN"));
    assert(!manager.isCommandPressed("CHORD"));
    key(SDL_SCANCODE_K, false, SDL_KMOD_SHIFT);
    key(SDL_SCANCODE_K, true, SDL_KMOD_SHIFT);
    key(SDL_SCANCODE_LSHIFT, false);
    pump();
    assert(manager.isCommandPressed("CHORD")); // correct even after Shift release
    assert(!manager.isCommandPressed("PLAIN"));
    manager.beginInputFrame();
    assert(!manager.isCommandPressed("PLAIN")); // still-held key cannot retrigger
    assert(!manager.isCommandPressed("CHORD"));
    key(SDL_SCANCODE_K, false);
    key(SDL_SCANCODE_K, true, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
    pump();
    assert(manager.isCommandPressed("EXACT"));
    assert(!manager.isCommandPressed("CHORD"));

    manager.beginInputFrame();
    bool typing = true;
    wowee::ui::setTypedInputProbe([&] { return typing; });
    key(SDL_SCANCODE_C, true);
    pump();
    typing = false;
    assert(!manager.isCommandPressed("PLAIN"));
    key(SDL_SCANCODE_C, false);
    key(SDL_SCANCODE_C, true);
    pump();
    wowee::ui::noteInterfaceConsumedKey(ImGuiKey_C);
    assert(!manager.isCommandPressed("PLAIN"));
    wowee::ui::clearInterfaceConsumedKeys();
    wowee::ui::setTypedInputProbe({});

    manager.beginInputFrame();
    manager.setVirtualCommand("VIRTUAL", "a", true);
    assert(manager.isCommandPressed("VIRTUAL"));
    manager.setVirtualCommand("VIRTUAL", "b", true);
    manager.setVirtualCommand("VIRTUAL", "a", false);
    assert(manager.isCommandDown("VIRTUAL"));
    manager.setVirtualCommand("VIRTUAL", "b", false);
    assert(!manager.isCommandPressed("VIRTUAL"));
    assert(!manager.isCommandDown("VIRTUAL"));
    manager.setVirtualCommand("VIRTUAL", "a", true);
    manager.setVirtualKey("C", true);
    key(SDL_SCANCODE_C, true);
    pump();
    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    assert(SDL_PushEvent(&focus));
    pump();
    manager.updateInput();
    assert(!manager.isCommandPressed("VIRTUAL"));
    assert(!manager.isCommandDown("VIRTUAL"));
    assert(!manager.isCommandPressed("PLAIN"));
    assert(!manager.isCommandDown("PLAIN"));
    focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    manager.processInputEvent(focus);
    manager.beginInputFrame();
    key(SDL_SCANCODE_C, true);
    pump();
    assert(manager.isCommandPressed("PLAIN"));
    ImGui::DestroyContext();
    SDL_Quit();
}
