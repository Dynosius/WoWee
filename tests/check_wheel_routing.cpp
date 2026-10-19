
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
std::string KeybindingManager::modifiedKey(std::string key, bool shift, bool ctrl, bool alt) {
    if (shift) key = "SHIFT-" + key;
    if (ctrl) key = "CTRL-" + key;
    if (alt) key = "ALT-" + key;
    return key;
}


std::string KeybindingManager::commandForKey(const std::string& key) const {
    if (key.empty()) return "";
    for (const auto& [command, slots] : bindings_) if (slots[0] == key || slots[1] == key) return command;
    return "";
}

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
float wheelClicks(float delta, uint64_t timestampNs) {
#if defined(__APPLE__)
    constexpr float kLinesPerClick = 20.0f;
    constexpr uint64_t kGestureGapNs = 500'000'000ull;
    static uint64_t lastPreciseNs = 0;
    const bool fractional = delta != std::trunc(delta);
    if (fractional) lastPreciseNs = timestampNs;
    const bool precise = fractional ||
        (lastPreciseNs != 0 && timestampNs - lastPreciseNs < kGestureGapNs);
    return precise ? delta / kLinesPerClick : delta;
#else
    (void)timestampNs;
    return delta;
#endif
}


struct Application {
    Renderer display;
    Renderer* renderer=&display;
    AddonManager addons;
    AddonManager* addonManager_=&addons;
    AppState state=AppState::IN_GAME;
    void route(Event event, bool pointerBindingTaken=false) {
            if (renderer && renderer->getCameraController() && !ImGui::GetIO().WantCaptureMouse && !pointerBindingTaken) {
                if (event.type == SDL_EVENT_MOUSE_MOTION) {
                    renderer->getCameraController()->processMouseMotion(event.motion);
                }
                else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    renderer->getCameraController()->processMouseButton(event.button);
                }
                else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    // The interface gets first refusal, and only where a
                    // frame under the cursor asked for the wheel. Zooming
                    // the camera while scrolling a quest log is what
                    // happens without the check, and a frame that did not
                    // ask must not swallow it either.
                    bool takenByUi = false;
                    if (addonManager_ && addonManager_->getLuaEngine()) {
                        // ImGui's cursor, not SDL's, because that is what
                        // the rest of the widget dispatch is fed and the
                        // two need not agree on a scaled display. Flipped
                        // to bottom-origin for the same reason the button
                        // dispatch is: the tree measures upward.
                        const ImGuiIO& mio = ImGui::GetIO();
                        takenByUi = addonManager_->getLuaEngine()->dispatchMouseWheel(
                            mio.MousePos.x, mio.DisplaySize.y - mio.MousePos.y,
                            wheelClicks(event.wheel.y, event.wheel.timestamp));
                    }
                    if (!takenByUi) {
                        const auto mods = SDL_GetModState();
                        const bool shift = (mods & SDL_KMOD_SHIFT) != 0;
                        const bool ctrl = (mods & SDL_KMOD_CTRL) != 0;
                        const bool alt = (mods & SDL_KMOD_ALT) != 0;
                        const std::string key = event.wheel.y > 0 ? "MOUSEWHEELUP" : "MOUSEWHEELDOWN";
                        auto& bindings = ui::KeybindingManager::getInstance();
                        std::string command = bindings.commandForKey(ui::KeybindingManager::modifiedKey(key, shift, ctrl, alt));
                        if (command.empty()) command = bindings.commandForKey(key);
                        if (!command.empty() && event.wheel.y != 0) {
                            if (command == "CAMERAZOOMIN" || command == "CAMERAZOOMOUT") {
                                // Only pointer capture gates world zoom, not keyboard/text focus.
                                // Preserve fractional trackpad travel and the camera's zoom calibration.
                                renderer->getCameraController()->processMouseWheel(
                                    std::abs(event.wheel.y) * (command == "CAMERAZOOMIN" ? 1.0f : -1.0f));
                            } else if (state == AppState::IN_GAME && addonManager_ && addonManager_->getLuaEngine()) {
                                bindings.setVirtualKey(key, true);
                                auto* engine = addonManager_->getLuaEngine();
                                engine->dispatchBindingInput(key, shift, ctrl, alt, true);
                                engine->dispatchBindingInput(key, shift, ctrl, alt, false);
                            }
                        }
                    }
                }
            }


    }
};
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
void runScenario(const std::string& scenario) {
    mods = 0;
    ui::typing = false;
    ImGui::GetIO() = {};
    Application app;
    auto& bindings = ui::KeybindingManager::getInstance();
    bindings.bindings_.clear();
    bindings.virtualKeys.clear();
    bindings.bindings_["CAMERAZOOMIN"] = {"MOUSEWHEELUP", ""};
    bindings.bindings_["CAMERAZOOMOUT"] = {"MOUSEWHEELDOWN", ""};
    Event event;
    event.wheel.y = 1;
    float expected = 1;
    bool zoom = true, pointerTaken = false, script = false;
    if (scenario == "down") event.wheel.y = expected = -1;
    if (scenario == "fractional_chat") {
        ui::typing = true; event.wheel.y = expected = 0.125f;
    }
    if (scenario == "imgui_chat") ImGui::GetIO().WantTextInput = true;
    if (scenario == "popup") ui::typing = true; // Zoom must not consult keyboard capture.
    if (scenario == "reverse") {
        bindings.bindings_["CAMERAZOOMIN"] = {"MOUSEWHEELDOWN", ""};
        bindings.bindings_["CAMERAZOOMOUT"] = {"MOUSEWHEELUP", ""};
        event.wheel.y = 0.375f; expected = -0.375f;
    }
    if (scenario == "chord" || scenario == "exact_chord") {
        mods = scenario == "chord" ? SDL_KMOD_SHIFT : SDL_KMOD_ALT | SDL_KMOD_CTRL | SDL_KMOD_SHIFT;
        bindings.bindings_["CAMERAZOOMOUT"][1] = scenario == "chord" ? "SHIFT-MOUSEWHEELUP" : "ALT-CTRL-SHIFT-MOUSEWHEELUP";
        ui::typing = true; ImGui::GetIO().WantTextInput = true; expected = -1;
    }
    if (scenario == "fallback") mods = SDL_KMOD_CTRL;
    if (scenario == "cleared") { bindings.bindings_.clear(); zoom = false; }
    if (scenario == "ui_consumed") { app.addons.engine.taken = true; zoom = false; }
    if (scenario == "imgui_capture") { ImGui::GetIO().WantCaptureMouse = true; zoom = false; }
    if (scenario == "no_engine") app.addons.available = false;
    if (scenario == "no_addons") app.addonManager_ = nullptr;
    if (scenario == "other_state") app.state = AppState::OTHER;
    if (scenario == "no_renderer") { app.renderer = nullptr; zoom = false; }
    if (scenario == "no_camera") { app.display.available = false; zoom = false; }
    if (scenario == "pointer_taken") { pointerTaken = true; zoom = false; }
    if (scenario == "zero") { event.wheel.y = 0; zoom = false; }
    if (scenario == "nonzoom" || scenario == "nonzoom_no_engine" || scenario == "nonzoom_other_state") {
        bindings.bindings_["CAMERAZOOMIN"] = {};
        bindings.bindings_["SCRIPT"] = {"MOUSEWHEELUP", ""};
        zoom = false; script = scenario == "nonzoom";
        if (scenario == "nonzoom_no_engine") app.addons.available = false;
        if (scenario == "nonzoom_other_state") app.state = AppState::OTHER;
    }
    app.route(event, pointerTaken);
    const auto& zooms = app.display.camera.zooms;
    check(zooms.size() == (zoom ? 1u : 0u), "wheel zoom routing mismatch");
    if (zoom) check(std::abs(zooms[0] - expected) < 0.00001f, "zoom direction/magnitude mismatch");
    check(app.addons.engine.bindingCalls == (script ? std::vector<bool>{true, false} : std::vector<bool>{}), "script press/release routing mismatch");
    check(bindings.virtualKeys.size() == (script ? 1u : 0u), "non-zoom native wheel routing mismatch");
    if (scenario == "imgui_capture" || scenario == "pointer_taken")
        check(app.addons.engine.wheelCalls == 0, "captured wheel reached FrameXML");
}
int main(int argc, char** argv) {
    if (argc == 2) {
        runScenario(argv[1]);
        return 0;
    }
    for (const char* scenario : {"up", "down", "fractional_chat", "imgui_chat", "popup", "reverse", "chord", "exact_chord",
                                 "fallback", "cleared", "ui_consumed", "imgui_capture", "no_engine", "no_addons", "other_state",
                                 "no_renderer", "no_camera", "pointer_taken", "zero", "nonzoom", "nonzoom_no_engine", "nonzoom_other_state"}) {
        runScenario(scenario);
    }
    return 0;
}
