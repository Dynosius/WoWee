#!/usr/bin/env python3
"""Run from repo root: python tests/check_pad_routing.py (requires g++ or CXX).

Compile the actual routing/release methods, application adapter and Lua dispatch
bodies against small device/Lua/command-sink doubles. No copied routing logic,
SDL window, game assets, or full client link is needed. This is a seam regression,
not a test of Lua execution, SDL sampling, or pointer warping.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def section(path, start, end):
    source = (ROOT / path).read_text(encoding="utf-8")
    return source[source.index(start):source.index(end, source.index(start))]


def main(harness_transform=None):
    dispatch = section("src/addons/lua_engine.cpp",
                       "LuaEngine::PadKeyOutcome LuaEngine::dispatchPadKey(",
                       "/// The topmost frame")
    names = section("src/ui/gamepad_bindings.cpp", "const char* padKeyName(",
                    "SDL_Scancode padClientKeyFor(")
    methods = section("src/ui/gamepad_controls.cpp", "bool GamepadControls::schemeHeld(",
                      "void GamepadControls::update(")
    release = section("src/ui/gamepad_controls.cpp", "void GamepadControls::releaseKeys(",
                      "void GamepadControls::reset(")
    lookup = section("src/addons/lua_engine.cpp", "std::string LuaEngine::bindingCommandForKey(",
                     "bool LuaEngine::dispatchBindingKey(")
    binding_dispatch = section("src/addons/lua_engine.cpp", "bool LuaEngine::dispatchBindingInput(",
                               "bool LuaEngine::bindingRunsOnUp(")
    adapter = section("src/core/application.cpp",
                      "ui::PadKeyAnswer answer;",
                      "\n        });")
    # SDL button names are labels here; their numeric values are immaterial.
    buttons = sorted(set(re.findall(r"SDL_GAMEPAD_BUTTON_[A-Z0-9_]+", names)))
    enum = "enum SDL_GamepadButton { " + ", ".join(buttons) + ", SDL_GAMEPAD_BUTTON_COUNT };\n"
    harness = r'''
#include <array>
#include <cassert>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <cstdint>
#define LOG_WARNING(...) ((void)0)
struct Widget { std::string name; unsigned id = 1; };
namespace ui { using Widget = ::Widget; }
// Minimal Lua seam: GetBindingAction reads the editable map; the capture
// query returns captureWaiting. Lookup/dispatch control flow stays production.
bool captureWaiting = false;
int luaL_loadstring(void*, const char*) { return 0; }
struct LuaState {
    std::map<std::string, std::string>* bindings;
    std::string key, result;
};
void lua_getglobal(void*, const char*) {}
bool lua_isfunction(void*, int) { return true; }
void lua_pushstring(void* state, const char* key) { static_cast<LuaState*>(state)->key = key; }
int lua_pcall(void* state, int args, int, int) {
    auto& s = *static_cast<LuaState*>(state);
    if (args == 1) s.result = (*s.bindings)[s.key];
    return 0;
}
const char* lua_tostring(void* state, int) { return static_cast<LuaState*>(state)->result.c_str(); }
int lua_toboolean(void*, int) { return captureWaiting; }
void lua_pop(void*, int) {}
namespace ImGui {
struct IO { bool WantTextInput = false; };
IO& GetIO() { static IO io; return io; }
}
namespace ui {
struct KeybindingManager {
    std::set<std::string> active;
    std::map<std::string, std::set<std::string>> sources;
    std::map<std::string, int> edges;
    static KeybindingManager& getInstance() { static KeybindingManager m; return m; }
    static std::string modifiedKey(const std::string& key, bool shift, bool ctrl, bool alt) {
        return std::string(alt ? "ALT-" : "") + (ctrl ? "CTRL-" : "") + (shift ? "SHIFT-" : "") + key;
    }
    void setVirtualCommand(const std::string& cmd, const std::string& source, bool down) {
        if (cmd.empty()) return;
        auto& held = sources[cmd];
        if (down) {
            if (held.empty()) ++edges[cmd];
            held.insert(source);
        } else held.erase(source);
        if (held.empty()) active.erase(cmd); else active.insert(cmd);
    }
};
}
struct LuaEngine {
    struct PadKeyOutcome { bool taken = false; std::string command; };
    std::map<std::string, std::string> bindings;
    LuaState state{&bindings, {}, {}};
    void* L_ = &state;
    std::map<std::string, std::string> heldBindings_;
    std::vector<std::pair<std::string, bool>> scriptEdges;
    std::set<std::string> runOnUp;
    bool capturing = false;
    int scripts = 0;
    Widget captureFrame{"KeyBindingFrame"};
    const Widget* topKeyboardFrame() { return capturing ? &captureFrame : nullptr; }
    void callFrameScript(unsigned, const char*, const char*) { ++scripts; }
    std::string bindingCommandForKey(const std::string& key);
    bool clientActsOnBinding(const std::string& cmd) { return cmd == "JUMP"; }
    bool editBoxHasFocus() { return false; }
    bool bindingRunsOnUp(const std::string& cmd) { return runOnUp.count(cmd); }
    bool runBindingScript(const std::string& cmd, bool down) {
        ++scripts;
        scriptEdges.emplace_back(cmd, down);
        return true;
    }
    bool dispatchBindingInput(const std::string&, bool, bool, bool, bool);
    PadKeyOutcome dispatchPadKey(const char* padKey, bool down);
};
namespace core {
struct Pad {
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    bool held(SDL_GamepadButton b) const { return buttons[b]; }
};
Pad& gamepad() { static Pad pad; return pad; }
struct Input {
    static Input& getInstance() { static Input i; return i; }
    std::set<int> activeKeys;
    void setVirtualKey(int key, bool down) {
        if (down) activeKeys.insert(key); else activeKeys.erase(key);
    }
};
}
using SDL_Scancode = int;
namespace ui {
struct PadKeyAnswer {
    enum class Kind { Unbound, Taken, Command };
    Kind kind = Kind::Unbound;
    std::string command;
    std::string key; // The production ButtonRoute also latches its qualified key.
};
struct GamepadControls {
    using ButtonRoute = PadKeyAnswer;
    std::array<ButtonRoute, SDL_GAMEPAD_BUTTON_COUNT> routes_{};
    std::array<std::uint8_t, 8> heldKeys_{};
    bool steering_ = false;
    float zoomRemainder_ = 0;
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttonWasDown_{};
    std::function<PadKeyAnswer(const char*, bool)> keyRouter_;
    bool pointerMode_ = false, pointerToggleWasDown_ = false;
    void setPointerMode(bool value) { pointerMode_ = value; }
    bool schemeHeld(SDL_GamepadButton button) const;
    void routeButtons();
    void applyButtons();
    void releaseKeys();
};
'''
    harness = enum + harness + names + methods + release + "\n}\n"
    harness += lookup + binding_dispatch + dispatch
    harness += r'''
struct AddonManager {
    LuaEngine engine;
    LuaEngine* getLuaEngine() { return &engine; }
};
struct Application {
    AddonManager manager;
    AddonManager* addonManager_ = &manager;
    ui::PadKeyAnswer route(const char* padKey, bool down) {
'''
    harness += adapter + "\n    }\n};\n"
    harness += r'''
int main() {
    Application app;
    auto& engine = app.manager.engine;
    ui::GamepadControls controls;
    controls.keyRouter_ = [&](const char* key, bool down) { return app.route(key, down); };
    auto& buttons = core::gamepad().buttons;
    auto& active = ui::KeybindingManager::getInstance().active;
    const auto frame = [&] {
        controls.routeButtons();
        controls.applyButtons();
    };
    const auto back = SDL_GAMEPAD_BUTTON_BACK;
    // Scheme-only Back passes dispatch -> application adapter -> route latch
    // -> schemeHeld -> pointer edge, and does not repeat while held.
    buttons[back] = true;
    frame();
    assert(controls.routes_[back].kind == ui::PadKeyAnswer::Kind::Unbound);
    assert(controls.pointerMode_);
    frame();
    assert(controls.pointerMode_);
    buttons[back] = false; frame();
    buttons[back] = true; frame();
    assert(!controls.pointerMode_);
    buttons[back] = false; frame();

    // A real client command still routes; clearing it removes all gameplay
    // output on the next press rather than reviving the old scheme's key.
    engine.bindings["PAD1"] = "JUMP";
    buttons[SDL_GAMEPAD_BUTTON_SOUTH] = true; frame();
    assert(active.count("JUMP") == 1);
    buttons[SDL_GAMEPAD_BUTTON_SOUTH] = false; frame();
    assert(active.empty());
    engine.bindings.clear();
    // Exercise every bindable non-Back button with an empty binding.
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
        const auto button = static_cast<SDL_GamepadButton>(b);
        if (button == back || !*ui::padKeyName(button)) continue;
        buttons[b] = true; frame(); frame();
        assert(active.empty());
        assert(!controls.pointerMode_);
        buttons[b] = false; frame();
    }
    assert(engine.scripts == 0);

    // Rebinding or capturing Back must suppress the pointer scheme.
    engine.bindings["PADBACK"] = "JUMP";
    buttons[back] = true; frame();
    assert(!controls.pointerMode_ && active.count("JUMP") == 1);
    buttons[back] = false; frame();
    engine.bindings["PADBACK"] = "SCRIPT_OR_UNKNOWN_COMMAND";
    buttons[back] = true; frame();
    assert(controls.routes_[back].kind == ui::PadKeyAnswer::Kind::Taken);
    assert(!controls.pointerMode_ && active.empty());
    buttons[back] = false; frame();
    engine.bindings.clear();
    engine.capturing = captureWaiting = true;
    buttons[back] = true; frame();
    assert(controls.routes_[back].kind == ui::PadKeyAnswer::Kind::Taken);
    assert(!controls.pointerMode_ && active.empty());
    buttons[back] = false; frame();
    engine.capturing = captureWaiting = false;
    buttons[back] = true; frame();
    assert(controls.pointerMode_ && active.empty());
    buttons.fill(false); frame(); controls.pointerMode_ = false;

    // Real lookup falls back to bare bindings only when no exact chord exists.
    engine.bindings = {{"K", "KEYBOARD"}, {"PAD3", "JUMP"}};
    assert(engine.bindingCommandForKey("ALT-CTRL-SHIFT-K") == "KEYBOARD");
    assert(engine.bindingCommandForKey("SHIFT-PAD3") == "JUMP");
    assert(engine.bindingCommandForKey("SHIFT-PADFORWARD").empty());
    engine.bindings["SHIFT-PAD3"] = "EXPLICIT";
    assert(engine.bindingCommandForKey("SHIFT-PAD3") == "EXPLICIT");
    assert(engine.bindingCommandForKey("").empty());

    // Separate per-button sources: one release cannot stop the other or touch.
    engine.bindings = {{"PAD1", "JUMP"}, {"PAD3", "JUMP"}};
    auto& sink = ui::KeybindingManager::getInstance();
    const auto a = SDL_GAMEPAD_BUTTON_SOUTH;
    const auto x = SDL_GAMEPAD_BUTTON_WEST;
    const int oldEdges = sink.edges["JUMP"];
    buttons[a] = buttons[x] = true; frame(); frame();
    assert(sink.edges["JUMP"] == oldEdges + 1 && sink.sources["JUMP"].size() == 2);
    buttons[a] = false; frame();
    assert(active.count("JUMP") && sink.sources["JUMP"].size() == 1);
    sink.setVirtualCommand("JUMP", "touch", true);
    buttons[x] = false; frame();
    assert(active.count("JUMP") && sink.sources["JUMP"] == std::set<std::string>{"touch"});
    sink.setVirtualCommand("JUMP", "touch", false);
    assert(active.empty());

    // Actual dispatchBindingInput must latch the original script AND chord key.
    engine.bindings = {{"PAD3", "JUMP"}, {"SHIFT-PAD3", "SCRIPT"}};
    engine.runOnUp.insert("SCRIPT");
    engine.scriptEdges.clear();
    buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = buttons[x] = true; frame(); frame();
    assert((engine.scriptEdges == std::vector<std::pair<std::string, bool>>{{"SCRIPT", true}}));
    engine.bindings["SHIFT-PAD3"] = "REBOUND";
    buttons[SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = false; frame();
    assert(engine.heldBindings_.count("SHIFT-PAD3"));
    buttons[x] = false; frame(); frame();
    assert((engine.scriptEdges == std::vector<std::pair<std::string, bool>>{{"SCRIPT", true}, {"SCRIPT", false}}));
    assert(engine.heldBindings_.empty());

    // Reset's actual releaseKeys releases scripts, stick and button sources only.
    engine.bindings = {{"PAD1", "JUMP"}, {"PAD3", "SCRIPT"}};
    buttons[a] = buttons[x] = true; frame();
    sink.setVirtualCommand("JUMP", "touch", true);
    sink.setVirtualCommand("MOVEFORWARD", "pad-stick", true);
    sink.setVirtualCommand("MOVEFORWARD", "touch", true);
    controls.heldKeys_[1] = 1;
    core::Input::getInstance().setVirtualKey(1, true);
    controls.steering_ = true; controls.zoomRemainder_ = 0.5f;
    controls.releaseKeys();
    assert(engine.heldBindings_.empty());
    assert((engine.scriptEdges.back() == std::pair<std::string, bool>{"SCRIPT", false}));
    assert(sink.sources["JUMP"] == std::set<std::string>{"touch"});
    assert(sink.sources["MOVEFORWARD"] == std::set<std::string>{"touch"});
    for (const auto& route : controls.routes_) {
        assert(route.kind == ui::PadKeyAnswer::Kind::Unbound && route.key.empty() && route.command.empty());
    }
    for (bool held : controls.buttonWasDown_) assert(!held);
    for (auto held : controls.heldKeys_) assert(!held);
    assert(core::Input::getInstance().activeKeys.empty());
    assert(!controls.steering_ && controls.zoomRemainder_ == 0);
    sink.setVirtualCommand("JUMP", "touch", false);
    sink.setVirtualCommand("MOVEFORWARD", "touch", false);
    buttons.fill(false); frame();

    // Back must enter pointer mode BEFORE A/X routing in this same frame.
    // Neither a client command nor a Lua script may escape pointer ownership.
    engine.bindings = {{"PAD1", "JUMP"}, {"PAD3", "SCRIPT"}};
    engine.scriptEdges.clear();
    buttons[back] = buttons[a] = buttons[x] = true; frame(); frame();
    assert(controls.pointerMode_ && active.empty() && engine.scriptEdges.empty());
    assert(controls.routes_[a].kind == ui::PadKeyAnswer::Kind::Taken);
    assert(controls.routes_[x].kind == ui::PadKeyAnswer::Kind::Taken);
    buttons.fill(false); frame();
    // Pointer-owned clicks bypass even the binding capture panel.
    engine.capturing = captureWaiting = true;
    const int captures = engine.scripts;
    buttons[a] = buttons[x] = true; frame();
    assert(engine.scripts == captures && active.empty());
    assert(controls.routes_[a].key.empty() && controls.routes_[x].key.empty());
    buttons.fill(false); frame();
    engine.capturing = captureWaiting = false;
}
'''
    if harness_transform:
        harness = harness_transform(harness)
    with tempfile.TemporaryDirectory(prefix="wowee-pad-routing-") as tmp:
        source = Path(tmp) / "check.cpp"
        exe = Path(tmp) / "check.exe"
        source.write_text(harness, encoding="utf-8")
        subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-Wall", "-Wextra",
                        "-Werror", str(source), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    if not harness_transform:
        print("PASS: pad routing, same-frame pointer/capture, sources/reset, Lua release, lookup fallback")


if __name__ == "__main__":
    main()
