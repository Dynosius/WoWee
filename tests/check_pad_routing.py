#!/usr/bin/env python3
"""Run: python tests/check_pad_routing.py (g++ or CXX; no controller required).

Compile production routing/release bodies and default tables with device/sink
stubs. Checks legacy behavior, not editable chords or new gamepad defaults.
"""
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def section(path, start, end):
    source = (ROOT / path).read_text(encoding="utf-8")
    return source[source.index(start):source.index(end, source.index(start))]


names = section("src/ui/gamepad_bindings.cpp", "const char* padKeyName(",
                "SDL_Scancode padClientKeyFor(")
table = section("src/ui/gamepad_bindings.cpp", "constexpr PadBinding kBindings[]",
                "\n}  // namespace")
extras = section("src/ui/gamepad_bindings.cpp", "constexpr PadBinding kExtras[]",
                 "\n}  // namespace")
methods = section("src/ui/gamepad_controls.cpp", "bool GamepadControls::schemeHeld(",
                  "void GamepadControls::update(")
release = section("src/ui/gamepad_controls.cpp", "void GamepadControls::releaseKeys(",
                  "void GamepadControls::reset(")
buttons = sorted(set(re.findall(r"SDL_GAMEPAD_BUTTON_[A-Z0-9_]+", names)))
keys = sorted(set(re.findall(r"SDL_SCANCODE_[A-Z0-9_]+", table + extras)))
harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <utility>
'''
harness += "enum SDL_GamepadButton { " + ", ".join(buttons) + ", SDL_GAMEPAD_BUTTON_COUNT };\n"
harness += "enum SDL_Scancode { " + ", ".join(keys) + ", SDL_SCANCODE_COUNT };\n"
harness += r'''
constexpr int SDL_KMOD_SHIFT = 1, SDL_KMOD_CTRL = 2;
int mods = 0;
int SDL_GetModState() { return mods; }
namespace core {
struct Pad {
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    bool held(SDL_GamepadButton b) const { return buttons[b]; }
};
Pad& gamepad() { static Pad p; return p; }
struct Input {
    static Input& getInstance() { static Input i; return i; }
    void setVirtualKey(SDL_Scancode, bool) {}
};
}
struct KeybindingManager {
    std::map<std::string, std::set<std::string>> sources;
    std::map<std::string, int> edges;
    static KeybindingManager& getInstance() { static KeybindingManager k; return k; }
    void setVirtualCommand(const std::string& cmd, const std::string& source, bool down) {
        if (down && sources[cmd].empty()) ++edges[cmd];
        if (down) sources[cmd].insert(source); else sources[cmd].erase(source);
    }
    bool down(const std::string& cmd) { return !sources[cmd].empty(); }
};
struct PadBinding { SDL_GamepadButton button; SDL_Scancode key; const char* what; const char* command = ""; };
struct PadKeyAnswer {
    enum class Kind { Unbound, Taken, Command };
    Kind kind = Kind::Unbound;
    std::string command;
};
struct GamepadControls {
    using ButtonRoute = PadKeyAnswer;
    std::array<ButtonRoute, SDL_GAMEPAD_BUTTON_COUNT> routes_{};
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttonWasDown_{};
    std::function<PadKeyAnswer(const char*)> keyRouter_;
    std::set<std::string> commandsDown_;
    std::array<std::uint8_t, SDL_SCANCODE_COUNT> heldKeys_{};
    bool pointerMode_ = false, steering_ = false;
    float zoomRemainder_ = 0;
    bool schemeHeld(SDL_GamepadButton button) const;
    void routeButtons();
    void applyButtons();
    void releaseKeys();
};
'''
harness += table + extras + names
harness += "std::span<const PadBinding> padBindings() { return kBindings; }\n"
harness += "std::span<const PadBinding> padExtraBindings() { return kExtras; }\n"
harness += methods + release + r'''
int main() {
    GamepadControls c;
    auto& b = core::gamepad().buttons;
    auto& k = KeybindingManager::getInstance();
    const auto frame = [&] { c.routeButtons(); c.applyButtons(); };
    const auto clear = [&] { b.fill(false); frame(); mods = 0; };
    // Legacy B AND Start close, aggregated without duplicate press edges.
    b[SDL_GAMEPAD_BUTTON_EAST] = true; frame();
    b[SDL_GAMEPAD_BUTTON_START] = true; frame();
    assert(k.down("TOGGLEGAMEMENU") && k.edges["TOGGLEGAMEMENU"] == 1);
    b[SDL_GAMEPAD_BUTTON_EAST] = false; frame();
    assert(k.down("TOGGLEGAMEMENU") && k.edges["TOGGLEGAMEMENU"] == 1);
    clear(); assert(!k.down("TOGGLEGAMEMENU"));
    // Unbound gameplay buttons still use the old scheme, including sit/autorun.
    b[SDL_GAMEPAD_BUTTON_RIGHT_STICK] = true;
    b[SDL_GAMEPAD_BUTTON_LEFT_STICK] = true; frame();
    assert(k.down("SITORSTAND") && k.down("TOGGLEAUTORUN")); clear();
    // Bumper modifier still works without any SHIFT-PAD binding entries.
    b[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = true;
    b[SDL_GAMEPAD_BUTTON_WEST] = true; frame();
    assert(k.down("MULTIACTIONBAR1BUTTON1") && !k.down("ACTIONBUTTON1")); clear();
    mods = SDL_KMOD_CTRL; b[SDL_GAMEPAD_BUTTON_WEST] = true; frame();
    assert(k.down("SHAPESHIFTBUTTON1")); clear();
    // Rebound/captured buttons override the fallback. Lookup happens once.
    int lookups = 0;
    c.keyRouter_ = [&](const char*) { ++lookups; return PadKeyAnswer{PadKeyAnswer::Kind::Command, "JUMP"}; };
    b[SDL_GAMEPAD_BUTTON_WEST] = true; frame(); frame();
    assert(k.down("JUMP") && !k.down("ACTIONBUTTON1") && lookups == 1); clear();
    c.keyRouter_ = [](const char*) { return PadKeyAnswer{PadKeyAnswer::Kind::Taken, ""}; };
    b[SDL_GAMEPAD_BUTTON_WEST] = true; frame();
    assert(!k.down("JUMP") && !k.down("ACTIONBUTTON1")); clear();
    c.keyRouter_ = {};
    // Pointer clicks must not jump/cast. Back remains a scheme-only role.
    c.pointerMode_ = true;
    b[SDL_GAMEPAD_BUTTON_SOUTH] = b[SDL_GAMEPAD_BUTTON_WEST] = true;
    b[SDL_GAMEPAD_BUTTON_BACK] = true; frame();
    assert(!k.down("JUMP") && !k.down("ACTIONBUTTON1"));
    assert(c.schemeHeld(SDL_GAMEPAD_BUTTON_BACK)); clear(); c.pointerMode_ = false;
    // Reset releases both pad sources, without dropping another device's hold.
    b[SDL_GAMEPAD_BUTTON_SOUTH] = true; frame();
    k.setVirtualCommand("MOVEFORWARD", "pad-stick", true);
    k.setVirtualCommand("MOVEFORWARD", "touch", true);
    c.releaseKeys();
    assert(!k.down("JUMP") && k.down("MOVEFORWARD"));
    assert(!k.sources["MOVEFORWARD"].count("pad-stick"));
    for (const auto& route : c.routes_) assert(route.kind == PadKeyAnswer::Kind::Unbound);
}
'''
def main():
    with tempfile.TemporaryDirectory(prefix="wowee-pad-compat-") as tmp:
        source = Path(tmp) / "check.cpp"
        exe = Path(tmp) / "check.exe"
        source.write_text(harness, encoding="utf-8")
        subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-Wall", "-Wextra",
                        "-Werror", str(source), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    print("PASS: legacy gamepad defaults/fallback, B+Start, modifiers, routing, pointer, reset")


if __name__ == "__main__":
    main()
