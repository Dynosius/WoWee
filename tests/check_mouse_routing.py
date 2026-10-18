#!/usr/bin/env python3
"""Run: python tests/check_mouse_routing.py (g++ or CXX).

Compile the real application mouse adapter, shared conversion and Lua dispatch
body against small UI/script-runner doubles; no window or game assets needed.
"""
import os
import subprocess
import tempfile
from pathlib import Path

from check_pad_routing import section

conversion = section("src/ui/keybinding_manager.cpp",
                     "std::string KeybindingManager::mouseButtonNameFromSDL(",
                     "std::string KeybindingManager::keyNameFromSDL(")
adapter = section("src/core/application.cpp", "            bool pointerBindingTaken = false;",
                  "            // Pass mouse events to camera controller")
dispatch = section("src/addons/lua_engine.cpp", "bool LuaEngine::dispatchBindingInput(",
                   "bool LuaEngine::bindingRunsOnUp(")
source = r'''
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>
constexpr uint8_t SDL_BUTTON_LEFT=1, SDL_BUTTON_MIDDLE=2, SDL_BUTTON_RIGHT=3,
                  SDL_BUTTON_X1=4, SDL_BUTTON_X2=5;
constexpr int SDL_EVENT_MOUSE_BUTTON_DOWN=1, SDL_EVENT_MOUSE_BUTTON_UP=2;
constexpr int SDL_KMOD_SHIFT=1, SDL_KMOD_CTRL=2, SDL_KMOD_ALT=4;
int SDL_GetModState() { return 0; }
namespace ImGui {
struct IO { bool WantCaptureMouse=false, WantTextInput=false; };
IO& GetIO() { static IO io; return io; }
}
namespace ui {
bool frameXmlOwnsMouse() { return ImGui::GetIO().WantCaptureMouse; }
struct KeybindingManager {
    static std::string mouseButtonNameFromSDL(uint8_t button);
    static std::string modifiedKey(std::string key, bool, bool, bool) { return key; }
};
}
using ui::KeybindingManager;
struct LuaEngine {
    void* L_=this;
    std::map<std::string,std::string> heldBindings_;
    std::map<std::string,std::string> bindings{{"BUTTON2","RIGHT"},{"BUTTON3","MIDDLE"}};
    std::vector<std::pair<std::string,bool>> scripts;
    bool editBoxHasFocus() { return false; }
    bool clientActsOnBinding(const std::string&) { return false; }
    bool bindingRunsOnUp(const std::string&) { return true; }
    std::string bindingCommandForKey(const std::string& key) { return bindings[key]; }
    bool runBindingScript(const std::string& cmd, bool down) {
        scripts.emplace_back(cmd,down); return true;
    }
    bool dispatchBindingInput(const std::string&,bool,bool,bool,bool);
};
'''
source += conversion + dispatch
source += r'''
struct AddonManager {
    LuaEngine engine;
    LuaEngine* getLuaEngine() { return &engine; }
};
enum class AppState { IN_GAME };
struct Event { int type; struct { uint8_t button; } button; };
struct Application {
    AppState state=AppState::IN_GAME;
    AddonManager manager;
    AddonManager* addonManager_=&manager;
    bool route(Event event) {
'''
source += adapter + '\nreturn pointerBindingTaken;\n}\n};\n'
source += r'''
int main() {
    Application app;
    auto& engine=app.manager.engine;
    const char* names[]={"", "BUTTON1", "BUTTON3", "BUTTON2", "BUTTON4", "BUTTON5"};
    for (uint8_t b=0; b<=5; ++b)
        assert(KeybindingManager::mouseButtonNameFromSDL(b)==names[b]);
    assert(KeybindingManager::mouseButtonNameFromSDL(255).empty());
    for (auto button : {SDL_BUTTON_RIGHT,SDL_BUTTON_MIDDLE}) {
        const std::string cmd=button==SDL_BUTTON_RIGHT ? "RIGHT" : "MIDDLE";
        engine.scripts.clear();
        assert(app.route({SDL_EVENT_MOUSE_BUTTON_DOWN,{button}}));
        // A repeat is consumed, but cannot run the script twice.
        assert(app.route({SDL_EVENT_MOUSE_BUTTON_DOWN,{button}}));
        // Release must still reach the original script after entering UI/rebinding.
        const auto key=KeybindingManager::mouseButtonNameFromSDL(button);
        engine.bindings[key]="REBOUND";
        ImGui::GetIO().WantCaptureMouse=true;
        assert(app.route({SDL_EVENT_MOUSE_BUTTON_UP,{button}}));
        assert((engine.scripts==std::vector<std::pair<std::string,bool>>{{cmd,true},{cmd,false}}));
        engine.bindings[key]=cmd;
        engine.scripts.clear();
        assert(!app.route({SDL_EVENT_MOUSE_BUTTON_DOWN,{button}}));
        assert(engine.scripts.empty());
        ImGui::GetIO().WantCaptureMouse=false;
    }
    assert(!app.route({SDL_EVENT_MOUSE_BUTTON_DOWN,{255}}));
}
'''
with tempfile.TemporaryDirectory(prefix="wowee-mouse-routing-") as tmp:
    path = Path(tmp) / "check.cpp"
    exe = Path(tmp) / "check.exe"
    path.write_text(source, encoding="utf-8")
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-Wall", "-Wextra",
                    "-Werror", str(path), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: shared mouse conversion, right/middle script isolation, press/release and UI capture")
