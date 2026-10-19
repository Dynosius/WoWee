
#include "ui/keybinding_manager.hpp"
#include "ui/widget_tree.hpp"
#include "ui/escape_action.hpp"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
std::array<bool, SDL_SCANCODE_COUNT> held{};
const bool* heldKeyboardState(int* count) { *count = held.size(); return held.data(); }
namespace wowee::addons {
class LuaEngine {
public:
    ui::WidgetTree widgets_;
    void* L_ = this;
    bool typing = false;
    int calls = 0;
    std::string lastKey;
    int propagateAfterScript = -1;
    bool editBoxHasFocus() const { return typing; }
    bool keyboardFrameHasFocus() const { return topKeyboardFrame() != nullptr; }
    bool keyboardFrameBlocksInput() const;
    const ui::Widget* topKeyboardFrame() const;
    bool dispatchFrameKey(int sdlKeycode, bool down, int bindingKeycode = 0);
    void callFrameScript(uint32_t id, const char*, const char* key) {
        ++calls;
        lastKey = key;
        if (propagateAfterScript != -1)
            widgets_.get(id)->propagateKeys = propagateAfterScript != 0;
    }
};
static std::string wowKeyName(int sym) {
    return ui::KeybindingManager::keyNameFromSDL(static_cast<uint32_t>(sym));
}

const ui::Widget* LuaEngine::topKeyboardFrame() const {
    const ui::Widget* best = nullptr;
    // Only the frames that enabled the keyboard, in ascending id order like the
    // full scan this replaced. Key dispatch and held-key capture ask this on
    // the input path, so it must not walk the whole tree.
    for (uint32_t id : widgets_.keyboardFrames()) {
        const ui::Widget* w = widgets_.get(id);
        if (!w || !w->keyboardEnabled || !w->visible) continue;
        if (!best) { best = w; continue; }
        if (w->effStrata > best->effStrata ||
            (w->effStrata == best->effStrata && w->effLevel >= best->effLevel)) {
            best = w;
        }
    }
    return best;
}

bool LuaEngine::keyboardFrameBlocksInput() const {
    const ui::Widget* frame = topKeyboardFrame();
    return frame && !frame->propagateKeys;
}

bool LuaEngine::dispatchFrameKey(int sdlKeycode, bool down, int bindingKeycode) {
    if (!L_) return false;
    const ui::Widget* best = topKeyboardFrame();
    if (!best) return false;
    const std::string key = wowKeyName(best->name == "KeyBindingFrame" && bindingKeycode
                                     ? bindingKeycode : sdlKeycode);
    if (key.empty()) return false;

    callFrameScript(best->id, down ? "OnKeyDown" : "OnKeyUp", key.c_str());
    // Consumed unless the frame asked for the key to carry on, which is WoW's
    // default and the reason a dialog stops the character walking.
    return !best->propagateKeys;
}


}
namespace ui = wowee::ui;
struct AddonManager {
    wowee::addons::LuaEngine engine;
    auto* getLuaEngine() { return &engine; }
};
struct Application {
    AddonManager* addonManager_;
    bool addonsLoaded_ = true;
    void configure() { ui::setTypedInputProbe(
            [this]() -> bool {
                if (!addonManager_ || !addonsLoaded_) return false;
                auto* engine = addonManager_->getLuaEngine();
                return engine != nullptr && engine->editBoxHasFocus();
            });
         }
    void capture() {
#define SDL_GetKeyboardState heldKeyboardState

        // A keyboard frame is not text focus: propagated shortcuts and controller
        // commands must still work. Block only physically held native keys when
        // the top frame does not propagate, including frames without key repeats.
        if (addonManager_ && addonsLoaded_) {
            if (auto* engine = addonManager_->getLuaEngine(); engine && engine->keyboardFrameBlocksInput()) {
                int count = 0;
                const bool* keys = SDL_GetKeyboardState(&count);
                for (int scan = 1; scan < count; ++scan) {
                    if (!keys[scan]) continue;
                    const auto key = ui::KeybindingManager::keyFromName(ui::KeybindingManager::keyNameFromSDL(
                        SDL_SCANCODE_TO_KEYCODE(scan)));
                    if (key != ImGuiKey_None) ui::noteInterfaceConsumedKey(key);
                }
            }
        }
        
#undef SDL_GetKeyboardState
    }
};
void runScenario(const std::string& scenario) {
    held.fill(false);
    ImGui::GetIO().WantTextInput = false;
    AddonManager addons;
    Application app{&addons};
    app.configure();
    auto& engine = addons.engine;
    auto& manager = ui::KeybindingManager::getInstance();
    manager.setVirtualCommand("TOGGLEGAMEMENU", "pad:PAD2", false);
    manager.bindings().clear();
    manager.bindings()["SHORTCUT"] = {"C", ""};
    manager.bindings()["MOVEFORWARD"] = {"W", ""};
    manager.bindings()["TOGGLEGAMEMENU"] = {"ESCAPE", ""};
    const auto id = engine.widgets_.create(ui::WidgetKind::Frame, engine.widgets_.root(),
        scenario == "layout_capture" ? "KeyBindingFrame" :
        scenario == "pad_map" ? "WorldMapFrame" : "StaticPopup1");
    auto* frame = engine.widgets_.get(id);
    engine.widgets_.setKeyboardEnabled(id, true);
    frame->visible = true;
    frame->propagateKeys = false;
    if (scenario == "hidden") frame->visible = false;
    if (scenario == "disabled") engine.widgets_.setKeyboardEnabled(id, false);
    if (scenario == "propagate") frame->propagateKeys = true;
    if (scenario == "script_propagate") engine.propagateAfterScript = 1;
    if (scenario == "script_consume") { frame->propagateKeys = true; engine.propagateAfterScript = 0; }
    if (scenario == "typing") engine.typing = true;
    if (scenario == "imgui_typing") ImGui::GetIO().WantTextInput = true;
    const auto begin = [&] { manager.beginInputFrame(); ui::clearInterfaceConsumedKeys(); };
    const auto keyDown = [&](SDL_Scancode scan) {
        held[scan] = true;
        SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = scan;
        manager.processInputEvent(event);
        const auto code = SDL_GetKeyFromScancode(scan, SDL_KMOD_NONE, true);
        if (engine.dispatchFrameKey(code, true))
            ui::noteInterfaceConsumedKey(ui::KeybindingManager::keyFromName(
                ui::KeybindingManager::keyNameFromSDL(code)));
    };
    begin();
    app.capture();
    begin();
    if (scenario == "layout_capture" || scenario == "layout_dialog") {
        check(engine.dispatchFrameKey(SDLK_Z, true, SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_W)), "frame did not consume layout key");
        check(engine.lastKey == (scenario == "layout_capture" ? "W" : "Z"), "physical capture/layout dialog mismatch");
    } else if (scenario == "pad_map" || scenario == "pad_popup" || scenario == "typing" || scenario == "imgui_typing") {
        // B's default native command must survive a non-editable keyboard frame.
        manager.setVirtualCommand("TOGGLEGAMEMENU", "pad:PAD2", true);
        app.capture();
        const bool typing = scenario == "typing" || scenario == "imgui_typing";
        check(manager.isCommandPressed("TOGGLEGAMEMENU") == !typing, "controller dismissal capture mismatch");
        ui::EscapeState state;
        state.interfaceConsumedKey = ui::interfaceConsumedKey(ImGuiKey_Escape);
        check(ui::resolveEscape(state) == ui::EscapeAction::AskTheInterface, "controller Escape was spuriously consumed");
        manager.setVirtualCommand("TOGGLEGAMEMENU", "pad:PAD2", false);
    } else if (scenario == "held" || scenario == "already_held") {
        if (scenario == "already_held") frame->visible = false;
        keyDown(SDL_SCANCODE_W);
        frame->visible = true;
        app.capture();
        check(!manager.isCommandDown("MOVEFORWARD"), "native movement leaked into keyboard frame");
        begin(); // No repeated KEY_DOWN: consumption still has to cover the held key.
        app.capture();
        check(!manager.isCommandDown("MOVEFORWARD"), "held native movement leaked on next frame");
        check(!ui::interfaceConsumedKey(ImGuiKey_Escape), "capture consumed an unheld Escape");
        frame->visible = false;
        begin(); app.capture();
        check(manager.isCommandDown("MOVEFORWARD"), "hidden frame kept capturing native movement");
    } else {
        keyDown(SDL_SCANCODE_C);
        app.capture();
        const bool consumed = scenario == "consume" || scenario == "script_consume";
        check(manager.isCommandPressed("SHORTCUT") == !consumed, "native shortcut propagation mismatch");
        check(engine.calls == ((scenario == "hidden" || scenario == "disabled") ? 0 : 1), "frame dispatch mismatch");
    }
    ui::setTypedInputProbe({});
}
int main(int argc, char** argv) {
    check(SDL_Init(SDL_INIT_EVENTS), "SDL init failed");
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    if (argc == 2) {
        runScenario(argv[1]);
    } else {
        for (const char* scenario : {"propagate", "consume", "script_propagate", "script_consume", "hidden", "disabled",
                                     "pad_map", "pad_popup", "typing", "imgui_typing", "held", "already_held",
                                     "layout_capture", "layout_dialog"}) {
            runScenario(scenario);
        }
    }
    ImGui::DestroyContext();
    SDL_Quit();
    return 0;
}
