enum SDL_GamepadButton { SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_MISC1, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_COUNT };

#include <algorithm>
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
    bool clientActsOnBinding(const std::string& cmd) { return !cmd.empty(); }
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
    int virtualKeyWrites = 0;
    void setVirtualKey(int key, bool down) {
        ++virtualKeyWrites;
        if (down) activeKeys.insert(key); else activeKeys.erase(key);
    }
};
}
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
    struct StickMovement {
        bool forward = false;
        bool back = false;
        bool left = false;
        bool right = false;
    } stickMovement_;

    static constexpr float kWalkThreshold = 0.30f;
    static constexpr float kStrafeThreshold = 0.45f;
    static constexpr float kReleaseHysteresis = 0.10f;
    void applyMovement(float x, float y);
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
const char* padKeyName(SDL_GamepadButton button) {
    switch (button) {
        case SDL_GAMEPAD_BUTTON_SOUTH:             return "PAD1";
        case SDL_GAMEPAD_BUTTON_EAST:             return "PAD2";
        case SDL_GAMEPAD_BUTTON_WEST:             return "PAD3";
        case SDL_GAMEPAD_BUTTON_NORTH:             return "PAD4";
        case SDL_GAMEPAD_BUTTON_BACK:          return "PADBACK";
        case SDL_GAMEPAD_BUTTON_GUIDE:         return "PADSYSTEM";
        case SDL_GAMEPAD_BUTTON_START:         return "PADFORWARD";
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:     return "PADLSTICK";
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:    return "PADRSTICK";
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  return "PADLSHOULDER";
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "PADRSHOULDER";
        case SDL_GAMEPAD_BUTTON_DPAD_UP:       return "PADDUP";
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:     return "PADDDOWN";
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:     return "PADDLEFT";
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:    return "PADDRIGHT";
        case SDL_GAMEPAD_BUTTON_MISC1:         return "PADSOCIAL";
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:       return "PADPADDLE1";
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:       return "PADPADDLE2";
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:       return "PADPADDLE3";
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:       return "PADPADDLE4";
        default:                                  return "";
    }
}
bool pushed(float axis, float threshold, bool wasOn) {
    const float release = std::max(threshold - 0.10f, 0.05f);
    return axis > (wasOn ? release : threshold);
}

void GamepadControls::applyMovement(float x, float y) {
    // SDL's Y is positive downwards, and pushing the stick up means forward.
    const bool forward = pushed(-y, kWalkThreshold, stickMovement_.forward);
    const bool back = pushed(y, kWalkThreshold, stickMovement_.back);
    // Sideways means strafe; the right stick turns the view.
    const bool left = pushed(-x, kStrafeThreshold, stickMovement_.left);
    const bool right = pushed(x, kStrafeThreshold, stickMovement_.right);
    stickMovement_ = {forward, back, left, right};
    auto& bindings = KeybindingManager::getInstance();
    bindings.setVirtualCommand("MOVEFORWARD", "pad-stick", forward);
    bindings.setVirtualCommand("MOVEBACKWARD", "pad-stick", back);
    bindings.setVirtualCommand("STRAFELEFT", "pad-stick", left);
    bindings.setVirtualCommand("STRAFERIGHT", "pad-stick", right);

    // Facing follows the camera while the stick is pushed, which is what the
    // right mouse button does on a desktop. Without it the character walks
    // sideways across the screen while still facing wherever they last were.
    steering_ = forward || back || left || right;
}

bool GamepadControls::schemeHeld(SDL_GamepadButton button) const {
    if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT) return false;
    return core::gamepad().held(button) &&
           routes_[static_cast<std::size_t>(button)].kind == PadKeyAnswer::Kind::Unbound;
}

void GamepadControls::routeButtons() {
    const auto& pad = core::gamepad();
    auto& bindings = KeybindingManager::getInstance();
    const auto route = [&](SDL_GamepadButton button) {
        const int b = static_cast<int>(button);
        const size_t i = static_cast<size_t>(b);
        const bool down = pad.held(button);
        const bool wasDown = buttonWasDown_[i];
        buttonWasDown_[i] = down;
        const char* name = padKeyName(button);
        if (!down) {
            bindings.setVirtualCommand(routes_[i].command, "pad-button" + std::to_string(b), false);
            if (wasDown && keyRouter_ && !routes_[i].key.empty()) keyRouter_(routes_[i].key.c_str(), false);
            routes_[i] = {};
            return;
        }
        if (wasDown || !keyRouter_ || !*name) return;
        if (pointerMode_ && (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_WEST)) {
            routes_[i].kind = PadKeyAnswer::Kind::Taken;
            return;
        }
        routes_[i].key = button != SDL_GAMEPAD_BUTTON_LEFT_SHOULDER &&
                         schemeHeld(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)
            ? "SHIFT-" + std::string(name) : std::string(name);
        const auto answer = keyRouter_(routes_[i].key.c_str(), true);
        routes_[i].kind = answer.kind;
        routes_[i].command = answer.command;
    };
    // Resolve the modifier and pointer mode before A/X, even on a simultaneous
    // press: pointer clicks must never run a gameplay binding or Lua script.
    route(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    route(SDL_GAMEPAD_BUTTON_BACK);
    const bool toggle = schemeHeld(SDL_GAMEPAD_BUTTON_BACK);
    if (toggle && !pointerToggleWasDown_) {
        setPointerMode(!pointerMode_);
        LOG_WARNING("Gamepad: the right stick now ",
                    pointerMode_ ? "moves the pointer" : "turns the view");
    }
    pointerToggleWasDown_ = toggle;
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
        const auto button = static_cast<SDL_GamepadButton>(b);
        if (button != SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && button != SDL_GAMEPAD_BUTTON_BACK)
            route(button);
    }
}

void GamepadControls::applyButtons() {
    auto& bindings = KeybindingManager::getInstance();
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
        const auto& route = routes_[static_cast<size_t>(b)];
        const auto button = static_cast<SDL_GamepadButton>(b);
        const bool pointerClick = pointerMode_ && (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_WEST);
        bindings.setVirtualCommand(route.command, "pad-button" + std::to_string(b),
            route.kind == PadKeyAnswer::Kind::Command && core::gamepad().held(button) && !pointerClick);
    }
}

void GamepadControls::releaseKeys() {
    stickMovement_ = {};
    auto& bindings = KeybindingManager::getInstance();
    for (const char* command : {"MOVEFORWARD", "MOVEBACKWARD", "STRAFELEFT", "STRAFERIGHT"})
        bindings.setVirtualCommand(command, "pad-stick", false);
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
        const auto& route = routes_[static_cast<size_t>(b)];
        bindings.setVirtualCommand(route.command, "pad-button" + std::to_string(b), false);
        if (buttonWasDown_[static_cast<size_t>(b)] && keyRouter_ && !route.key.empty())
            keyRouter_(route.key.c_str(), false);
    }
    // Forgotten as well as released, so a button still held when the pad is
    // live again is looked up afresh rather than answered from before.
    routes_.fill(ButtonRoute{});
    buttonWasDown_.fill(false);
    steering_ = false;
    zoomRemainder_ = 0.0f;
}


}
std::string LuaEngine::bindingCommandForKey(const std::string& key) {
    if (!L_ || key.empty()) return "";
    // Which command the key runs, and then the command's script - both asked
    // of the interface's own tables rather than restated here. GetBindingAction
    // already honours SetBinding, so a key the player rebound in game answers
    // to whatever they moved it to without this knowing anything about it.
    lua_getglobal(L_, "GetBindingAction");
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return ""; }
    lua_pushstring(L_, key.c_str());
    if (lua_pcall(L_, 1, 1, 0) != 0) {
        LOG_WARNING("GetBindingAction(", key, ") failed: ",
                    luaL_optstring(L_, -1, "?"));
        lua_pop(L_, 1);
        return "";
    }
    const char* got = lua_tostring(L_, -1);
    std::string command = got ? got : "";
    lua_pop(L_, 1);
    if (command.empty()) {
        std::string base = key;
        for (const char* prefix : {"ALT-", "CTRL-", "SHIFT-"})
            if (base.starts_with(prefix)) base.erase(0, std::char_traits<char>::length(prefix));
        // Exact chords win; an unassigned modifier chord keeps the bare binding,
        // including pad buttons while the scheme's left bumper is held.
        if (base != key) return bindingCommandForKey(base);
    }
    return command;
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

LuaEngine::PadKeyOutcome LuaEngine::dispatchPadKey(const char* padKey, bool down) {
    PadKeyOutcome outcome;
    if (!L_ || !padKey || !*padKey) return outcome;
    if (!down) {
        outcome.taken = dispatchBindingInput(padKey, false, false, false, false);
        return outcome;
    }

    // The key binding panel, while one of its rows is waiting for a key - and
    // only then. Every other frame that listens for keys is a dialog reading
    // Escape, Enter and digits, and a pad button given to one of those would
    // be swallowed doing nothing: B would stop closing them.
    //
    // Asked in Lua, because "waiting" is the panel's own field and not
    // something the widget tree knows about.
    const ui::Widget* top = topKeyboardFrame();
    if (top && top->name == "KeyBindingFrame") {
        bool waiting = false;
        if (luaL_loadstring(L_, "return KeyBindingFrame ~= nil and KeyBindingFrame.selected ~= nil") == 0 &&
            lua_pcall(L_, 0, 1, 0) == 0) {
            waiting = lua_toboolean(L_, -1) != 0;
        }
        lua_pop(L_, 1);  // the answer, or the error in its place
        if (waiting) {
            callFrameScript(top->id, "OnKeyDown", padKey);
            outcome.taken = true;
            return outcome;
        }
    }

    const std::string command = bindingCommandForKey(padKey);
    if (command.empty()) return outcome;
    if (clientActsOnBinding(command)) {
        outcome.command = command;
        return outcome;
    }
    // Taken whether or not a script ran. A button bound to a command with no
    // script doing nothing is right; falling back to the default scheme would
    // be the button doing what the player bound it away from.
    (void)dispatchBindingInput(padKey, false, false, false, true);
    outcome.taken = true;
    return outcome;
}


struct AddonManager {
    LuaEngine engine;
    LuaEngine* getLuaEngine() { return &engine; }
};
struct Application {
    AddonManager manager;
    AddonManager* addonManager_ = &manager;
    ui::PadKeyAnswer route(const char* padKey, bool down) {
ui::PadKeyAnswer answer;
            auto* engine = addonManager_ ? addonManager_->getLuaEngine() : nullptr;
            if (!engine) return answer;
            auto outcome = engine->dispatchPadKey(padKey, down);
            if (outcome.taken) {
                answer.kind = ui::PadKeyAnswer::Kind::Taken;
            } else if (!outcome.command.empty()) {
                answer.kind = ui::PadKeyAnswer::Kind::Command;
                answer.command = std::move(outcome.command);
            }
            return answer;
    }
};


#include <span>
struct PadBinding { SDL_GamepadButton button; int key; const char* what; const char* command = ""; };
enum { SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_NUMLOCKCLEAR, SDL_SCANCODE_PRINTSCREEN, SDL_SCANCODE_SPACE, SDL_SCANCODE_TAB, SDL_SCANCODE_X };
constexpr PadBinding kBindings[] = {
    {SDL_GAMEPAD_BUTTON_EAST, SDL_SCANCODE_ESCAPE, "Close a window, or the game menu", "TOGGLEGAMEMENU"},
    {SDL_GAMEPAD_BUTTON_SOUTH,             SDL_SCANCODE_SPACE,        "Jump", "JUMP"},
    {SDL_GAMEPAD_BUTTON_WEST,             SDL_SCANCODE_1,            "Action 1", "ACTIONBUTTON1"},
    {SDL_GAMEPAD_BUTTON_NORTH,             SDL_SCANCODE_2,            "Action 2", "ACTIONBUTTON2"},
    {SDL_GAMEPAD_BUTTON_DPAD_UP,       SDL_SCANCODE_3,            "Action 3", "ACTIONBUTTON3"},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT,    SDL_SCANCODE_4,            "Action 4", "ACTIONBUTTON4"},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN,     SDL_SCANCODE_5,            "Action 5", "ACTIONBUTTON5"},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT,     SDL_SCANCODE_6,            "Action 6", "ACTIONBUTTON6"},
    // Shift is not an action of its own: the client already reads it as "the
    // bottom-left bar", which is where actions 7 to 12 live on a keyboard
    // too. So the modifier costs nothing to implement and behaves exactly as
    // a player expects it to.
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  SDL_SCANCODE_LSHIFT,       "Hold: actions 7-12"},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_SCANCODE_TAB,          "Target nearest enemy", "TARGETNEARESTENEMY"},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK,     SDL_SCANCODE_NUMLOCKCLEAR, "Autorun", "TOGGLEAUTORUN"},
    // The last button every pad has and nothing was using. X sits the
    // character down, and dives while swimming - both of them things a
    // keyboard could do and a controller could not reach at all.
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK,    SDL_SCANCODE_X,            "Sit down, or dive", "SITORSTAND"},
};
constexpr PadBinding kExtras[] = {
    {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_SCANCODE_7,          "Action 7", "ACTIONBUTTON7"},
    {SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, SDL_SCANCODE_8,          "Action 8", "ACTIONBUTTON8"},
    {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, SDL_SCANCODE_9,          "Action 9", "ACTIONBUTTON9"},
    {SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, SDL_SCANCODE_0,          "Action 10", "ACTIONBUTTON10"},
    // The share, capture and microphone buttons are all one button to SDL,
    // and on every pad that has one it is the button for keeping a moment.
    {SDL_GAMEPAD_BUTTON_MISC1,   SDL_SCANCODE_PRINTSCREEN, "Screenshot", "SCREENSHOT"},
};

namespace wowee::ui {
using PadBinding = ::PadBinding;
using ::ui::padKeyName;
std::span<const PadBinding> padBindings() { return kBindings; }
std::span<const PadBinding> padExtraBindings() { return kExtras; }
}
std::map<std::string, std::array<std::string, 2>> defaultsMap;
auto& bindingKeys() { return defaultsMap; }
void seedDefaults() {
    const auto seedPad = [](std::span<const wowee::ui::PadBinding> rows) {
        for (const auto& row : rows) {
            if (!row.command || !*row.command) continue;
            const char* key = wowee::ui::padKeyName(row.button);
            if (key && *key) {
                auto& slots = bindingKeys()[row.command];
                if (slots[1].empty()) slots[1] = key;
            }
        }
    };
    seedPad(wowee::ui::padBindings());
    seedPad(wowee::ui::padExtraBindings());
    // The scheme-only bumper selects editable bottom-left action-bar chords.
    for (const auto& row : wowee::ui::padBindings()) {
        if (std::string(row.command).starts_with("ACTIONBUTTON")) {
            const std::string slot = std::string(row.command).substr(12);
            auto& keys = bindingKeys()["MULTIACTIONBAR1BUTTON" + slot];
            if (keys[1].empty()) keys[1] = "SHIFT-" + std::string(wowee::ui::padKeyName(row.button));
        }
    }

}

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

}
