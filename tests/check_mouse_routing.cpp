
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
std::string KeybindingManager::mouseButtonNameFromSDL(uint8_t button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return "BUTTON1";
        case SDL_BUTTON_RIGHT: return "BUTTON2";
        case SDL_BUTTON_MIDDLE: return "BUTTON3";
        case SDL_BUTTON_X1: return "BUTTON4";
        case SDL_BUTTON_X2: return "BUTTON5";
        default: return "";
    }
}
bool LuaEngine::dispatchBindingInput(const std::string& key, bool shift, bool ctrl, bool alt, bool down) {
    if (!L_ || key.empty()) return false;
    if (!down) {
        auto held = heldBindings_.find(key);
        if (held == heldBindings_.end()) return false;
        const std::string command = std::move(held->second);
        heldBindings_.erase(held);
        return !bindingRunsOnUp(command) || runBindingScript(command, false);
    }
    if (heldBindings_.count(key)) return true;
    if (editBoxHasFocus() || ImGui::GetIO().WantTextInput) return false;
    const std::string command = bindingCommandForKey(ui::KeybindingManager::modifiedKey(key, shift, ctrl, alt));
    if (command.empty() || clientActsOnBinding(command)) return false;
    if (!runBindingScript(command, true)) return false;
    heldBindings_[key] = command; // Release the original command even if it is rebound while held.
    return true;
}


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
            bool pointerBindingTaken = false;
            if (state == AppState::IN_GAME && addonManager_ && addonManager_->getLuaEngine() &&
                (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)) {
                const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                if (!down || (!ImGui::GetIO().WantCaptureMouse && !ui::frameXmlOwnsMouse())) {
                    const auto mods = SDL_GetModState();
                    pointerBindingTaken = addonManager_->getLuaEngine()->dispatchBindingInput(
                        ui::KeybindingManager::mouseButtonNameFromSDL(event.button.button),
                        (mods & SDL_KMOD_SHIFT) != 0, (mods & SDL_KMOD_CTRL) != 0,
                        (mods & SDL_KMOD_ALT) != 0, down);
                }
            }


return pointerBindingTaken;
}
};

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
