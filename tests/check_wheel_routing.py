#!/usr/bin/env python3
"""Run: python tests/check_wheel_routing.py (g++ or CXX; no game assets).

Compile the production application camera/wheel adapter and command lookup
against small UI/camera sinks. Exercises routing, editable chords, raw fractional
zoom deltas and capture precedence; not SDL event sampling or camera geometry.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from check_pad_routing import section

adapter = section("src/core/application.cpp", "            if (renderer && renderer->getCameraController() && !ImGui::GetIO().WantCaptureMouse",
                  "            // Handle window events")
wheel_clicks = section("src/core/application.cpp", "float wheelClicks(", "} // namespace")
modified_key = section("src/ui/keybinding_manager.cpp", "std::string KeybindingManager::modifiedKey(", "bool KeybindingManager::setBinding(")
lookup = section("src/ui/keybinding_manager.cpp", "std::string KeybindingManager::commandForKey(", "void KeybindingManager::beginInputFrame(")
source = r'''
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>
constexpr int SDL_EVENT_MOUSE_MOTION=1, SDL_EVENT_MOUSE_BUTTON_DOWN=2,
              SDL_EVENT_MOUSE_BUTTON_UP=3, SDL_EVENT_MOUSE_WHEEL=4;
constexpr int SDL_KMOD_SHIFT=1, SDL_KMOD_CTRL=2, SDL_KMOD_ALT=4;
int mods=0;
int SDL_GetModState() { return mods; }
struct ImGuiIO {
    bool WantCaptureMouse=false, WantTextInput=false;
    struct Vec { float x=0, y=0; } MousePos, DisplaySize;
};
namespace ImGui { ImGuiIO& GetIO() { static ImGuiIO io; return io; } }
namespace ui {
bool typing=false;
bool interfaceTakingTypedInput() { return typing; }
struct KeybindingManager {
    std::map<std::string, std::array<std::string,2>> bindings_;
    std::vector<std::pair<std::string,bool>> virtualKeys;
    static KeybindingManager& getInstance() { static KeybindingManager m; return m; }
    static std::string modifiedKey(std::string key, bool shift, bool ctrl, bool alt);
    std::string commandForKey(const std::string& key) const;
    void setVirtualKey(const std::string& key, bool down) { virtualKeys.emplace_back(key,down); }
};
@MODIFIED_KEY@
@LOOKUP@
}
struct Event {
    int type=SDL_EVENT_MOUSE_WHEEL;
    struct { float y=0; uint64_t timestamp=0; } wheel;
    int motion=0, button=0;
};
struct Camera {
    std::vector<float> zooms;
    void processMouseMotion(int) {}
    void processMouseButton(int) {}
    void processMouseWheel(float delta) { zooms.push_back(delta); }
};
struct Renderer {
    Camera camera;
    bool available=true;
    Camera* getCameraController() { return available ? &camera : nullptr; }
};
struct LuaEngine {
    bool taken=false;
    int wheelCalls=0;
    std::vector<bool> bindingCalls;
    bool dispatchMouseWheel(float, float, float) { ++wheelCalls; return taken; }
    bool dispatchBindingInput(const std::string&, bool, bool, bool, bool down) {
        bindingCalls.push_back(down); return true;
    }
};
struct AddonManager {
    LuaEngine engine;
    bool available=true;
    LuaEngine* getLuaEngine() { return available ? &engine : nullptr; }
};
enum class AppState { IN_GAME, OTHER };
@WHEEL_CLICKS@
struct Application {
    Renderer display;
    Renderer* renderer=&display;
    AddonManager addons;
    AddonManager* addonManager_=&addons;
    AppState state=AppState::IN_GAME;
    void route(Event event, bool pointerBindingTaken=false) {
@ADAPTER@
    }
};
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main(int argc, char** argv) {
    check(argc == 2, "scenario required");
    const std::string scenario=argv[1];
    Application app;
    auto& bindings=ui::KeybindingManager::getInstance();
    bindings.bindings_["CAMERAZOOMIN"]={"MOUSEWHEELUP", ""};
    bindings.bindings_["CAMERAZOOMOUT"]={"MOUSEWHEELDOWN", ""};
    Event event;
    event.wheel.y=1;
    float expected=1;
    bool zoom=true, pointerTaken=false, script=false;
    if (scenario=="down") event.wheel.y=expected=-1;
    if (scenario=="fractional_chat") {
        ui::typing=true; event.wheel.y=expected=0.125f;
    }
    if (scenario=="imgui_chat") ImGui::GetIO().WantTextInput=true;
    if (scenario=="popup") ui::typing=true; // Zoom must not consult keyboard capture.
    if (scenario=="reverse") {
        bindings.bindings_["CAMERAZOOMIN"]={"MOUSEWHEELDOWN", ""};
        bindings.bindings_["CAMERAZOOMOUT"]={"MOUSEWHEELUP", ""};
        event.wheel.y=0.375f; expected=-0.375f;
    }
    if (scenario=="chord" || scenario=="exact_chord") {
        mods=scenario=="chord" ? SDL_KMOD_SHIFT : SDL_KMOD_ALT|SDL_KMOD_CTRL|SDL_KMOD_SHIFT;
        bindings.bindings_["CAMERAZOOMOUT"][1]=scenario=="chord" ? "SHIFT-MOUSEWHEELUP" : "ALT-CTRL-SHIFT-MOUSEWHEELUP";
        ui::typing=true; ImGui::GetIO().WantTextInput=true; expected=-1;
    }
    if (scenario=="fallback") mods=SDL_KMOD_CTRL;
    if (scenario=="cleared") { bindings.bindings_.clear(); zoom=false; }
    if (scenario=="ui_consumed") { app.addons.engine.taken=true; zoom=false; }
    if (scenario=="imgui_capture") { ImGui::GetIO().WantCaptureMouse=true; zoom=false; }
    if (scenario=="no_engine") app.addons.available=false;
    if (scenario=="no_addons") app.addonManager_=nullptr;
    if (scenario=="other_state") app.state=AppState::OTHER;
    if (scenario=="no_renderer") { app.renderer=nullptr; zoom=false; }
    if (scenario=="no_camera") { app.display.available=false; zoom=false; }
    if (scenario=="pointer_taken") { pointerTaken=true; zoom=false; }
    if (scenario=="zero") { event.wheel.y=0; zoom=false; }
    if (scenario=="nonzoom" || scenario=="nonzoom_no_engine" || scenario=="nonzoom_other_state") {
        bindings.bindings_["CAMERAZOOMIN"]={};
        bindings.bindings_["SCRIPT"]={"MOUSEWHEELUP", ""};
        zoom=false; script=scenario=="nonzoom";
        if (scenario=="nonzoom_no_engine") app.addons.available=false;
        if (scenario=="nonzoom_other_state") app.state=AppState::OTHER;
    }
    app.route(event,pointerTaken);
    const auto& zooms=app.display.camera.zooms;
    check(zooms.size()==(zoom ? 1u : 0u), "wheel zoom routing mismatch");
    if (zoom) check(std::abs(zooms[0]-expected)<0.00001f, "zoom direction/magnitude mismatch");
    check(app.addons.engine.bindingCalls==(script ? std::vector<bool>{true,false} : std::vector<bool>{}), "script press/release routing mismatch");
    check(bindings.virtualKeys.size()==(script ? 1u : 0u), "non-zoom native wheel routing mismatch");
    if (scenario=="imgui_capture" || scenario=="pointer_taken")
        check(app.addons.engine.wheelCalls==0, "captured wheel reached FrameXML");
}
'''.replace("@ADAPTER@", adapter).replace("@WHEEL_CLICKS@", wheel_clicks).replace("@MODIFIED_KEY@", modified_key).replace("@LOOKUP@", lookup)

with tempfile.TemporaryDirectory(prefix="wowee-wheel-routing-") as tmp:
    path = Path(tmp) / "check.cpp"
    exe = Path(tmp) / "check.exe"
    path.write_text(source, encoding="utf-8")
    subprocess.run(shlex.split(os.environ.get("CXX", "g++")) + ["-std=c++20", "-Wall", "-Wextra", "-Werror",
                   str(path), "-o", str(exe)], check=True)
    failed = []
    for scenario in ("up", "down", "fractional_chat", "imgui_chat", "popup", "reverse", "chord", "exact_chord",
                     "fallback", "cleared", "ui_consumed", "imgui_capture", "no_engine", "no_addons", "other_state",
                     "no_renderer", "no_camera", "pointer_taken", "zero", "nonzoom", "nonzoom_no_engine", "nonzoom_other_state"):
        result = subprocess.run([str(exe), scenario], capture_output=True, text=True)
        print(f"{scenario}: {'PASS' if result.returncode == 0 else 'FAIL'}", flush=True)
        if result.returncode:
            print(result.stdout + result.stderr, end="")
            failed.append(scenario)
    if failed:
        raise SystemExit("Failed: " + ", ".join(failed))
