#!/usr/bin/env python3
"""Compile default seeding + actual pad dispatch/route/application seam with doubles.

Run from repo root: python tests/check_pad_defaults.py (g++ or CXX).
Start is intentionally unassigned: Escape and B consume the two menu slots.
"""
import re
from check_pad_routing import main, section


def defaults(harness):
    table = section('src/ui/gamepad_bindings.cpp', 'constexpr PadBinding kBindings[]',
                    '\n}  // namespace')
    extras = section('src/ui/gamepad_bindings.cpp', 'constexpr PadBinding kExtras[]',
                     '\n}  // namespace')
    seed = section('src/addons/lua_action_api.cpp', '    const auto seedPad =',
                   '    if (withSaved)')
    tokens = sorted(set(re.findall(r'SDL_SCANCODE_[A-Z0-9_]+', table + extras)))
    declarations = '''
#include <span>
struct PadBinding { SDL_GamepadButton button; int key; const char* what; const char* command = ""; };
''' + 'enum { ' + ', '.join(tokens) + ' };\n' + table + extras + '''
namespace wowee::ui {
using PadBinding = ::PadBinding;
using ::ui::padKeyName;
std::span<const PadBinding> padBindings() { return kBindings; }
std::span<const PadBinding> padExtraBindings() { return kExtras; }
}
std::map<std::string, std::array<std::string, 2>> defaultsMap;
auto& bindingKeys() { return defaultsMap; }
void seedDefaults() {
''' + seed + '\n}\n'
    harness = harness.replace('int main() {', declarations + '\nint main() {')
    harness = harness.replace('return cmd == "JUMP";', 'return !cmd.empty();')
    checks = r'''
    // Actual default seeding preserves keyboard authority and uses slot 2.
    defaultsMap["TOGGLEGAMEMENU"] = {"ESCAPE", ""};
    defaultsMap["SITORSTAND"] = {"X", ""};
    seedDefaults();
    assert(defaultsMap["TOGGLEGAMEMENU"][0] == "ESCAPE");
    assert(defaultsMap["TOGGLEGAMEMENU"][1] == "PAD2");
    int action = 1;
    for (const auto b : {SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
                        SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
                        SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT}) {
        assert(defaultsMap["MULTIACTIONBAR1BUTTON" + std::to_string(action++)][1] ==
               "SHIFT-" + std::string(ui::padKeyName(b)));
    }
    engine.bindings.clear();
    for (const auto& [cmd, slots] : defaultsMap)
        for (const auto& key : slots) if (!key.empty()) engine.bindings[key] = cmd;
    buttons.fill(false); frame();
    controls.pointerMode_ = false;
    const auto press = [&](SDL_GamepadButton b, const std::string& cmd) {
        buttons[b] = true; frame();
        assert(active.size() == 1 && active.count(cmd));
        buttons[b] = false; frame(); assert(active.empty());
    };
    const std::pair<SDL_GamepadButton, std::string> bareCommands[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, "JUMP"},
        {SDL_GAMEPAD_BUTTON_EAST, "TOGGLEGAMEMENU"},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "TARGETNEARESTENEMY"},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, "TOGGLEAUTORUN"},
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, "SITORSTAND"},
    };
    for (const auto& [b, cmd] : bareCommands) press(b, cmd);
    assert(engine.bindings.find("PADFORWARD") == engine.bindings.end());
    buttons[SDL_GAMEPAD_BUTTON_START] = true; frame(); assert(active.empty());
    buttons[SDL_GAMEPAD_BUTTON_START] = false; frame();
    // Same-frame LB+button must keep the bare command unless that exact chord
    // is assigned. Start has neither a bare default nor a chord default.
    buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = true;
    for (const auto& [b, cmd] : bareCommands) press(b, cmd);
    buttons[SDL_GAMEPAD_BUTTON_START] = true; frame(); assert(active.empty());
    buttons[SDL_GAMEPAD_BUTTON_START] = false; frame();
    engine.bindings["SHIFT-PADFORWARD"] = "SITORSTAND";
    press(SDL_GAMEPAD_BUTTON_START, "SITORSTAND");
    engine.bindings.erase("SHIFT-PADFORWARD");
    buttons[SDL_GAMEPAD_BUTTON_START] = true; frame(); assert(active.empty());
    buttons[SDL_GAMEPAD_BUTTON_START] = false; frame();
    action = 1;
    for (const auto b : {SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
                        SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
                        SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT})
        press(b, "MULTIACTIONBAR1BUTTON" + std::to_string(action++));
    engine.bindings["SHIFT-PAD1"] = "SITORSTAND";
    press(SDL_GAMEPAD_BUTTON_SOUTH, "SITORSTAND");
    engine.bindings.erase("SHIFT-PAD1");
    press(SDL_GAMEPAD_BUTTON_SOUTH, "JUMP");
    engine.bindings.erase("SHIFT-PAD3");
    press(SDL_GAMEPAD_BUTTON_WEST, "ACTIONBUTTON1");
    engine.bindings["SHIFT-PAD3"] = "JUMP";
    press(SDL_GAMEPAD_BUTTON_WEST, "JUMP");
    // Releasing the bumper first must still release the original chord key.
    buttons[SDL_GAMEPAD_BUTTON_WEST] = true; frame();
    buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = false; frame();
    assert(active.count("JUMP"));
    buttons[SDL_GAMEPAD_BUTTON_WEST] = false; frame(); assert(active.empty());
    engine.bindings["PADLSHOULDER"] = "JUMP";
    buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = true;
    buttons[SDL_GAMEPAD_BUTTON_WEST] = true; frame();
    assert(active.count("JUMP") && active.count("ACTIONBUTTON1"));
    buttons.fill(false); frame();
    for (const auto b : {SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_RIGHT_STICK}) {
        engine.bindings.erase(ui::padKeyName(b));
        buttons[b] = true; frame(); assert(active.empty());
        buttons[b] = false; frame();
        engine.bindings[ui::padKeyName(b)] = "JUMP";
        press(b, "JUMP");
    }
    engine.bindings["PADFORWARD"] = "TOGGLEGAMEMENU";
    press(SDL_GAMEPAD_BUTTON_START, "TOGGLEGAMEMENU");
    engine.bindings.erase("PADLSHOULDER");
    buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = true;
    press(SDL_GAMEPAD_BUTTON_START, "TOGGLEGAMEMENU");
    buttons.fill(false); frame();
'''
    # Run only these checks; the original test exercises its own JUMP-only double.
    start = harness.index('    const auto back =')
    harness = harness[:start] + checks + '\n}\n'
    return harness


if __name__ == '__main__':
    main(defaults)
    print('PASS: defaults, LB bare fallback, exact chords, unassigned/rebound Start, clear/rebind')
