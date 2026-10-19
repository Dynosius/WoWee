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
    controls.applyMovement(0.0f, -1.0f);
    sink.setVirtualCommand("MOVEFORWARD", "touch", true);
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
    assert(core::Input::getInstance().activeKeys.empty());
    assert(!controls.steering_ && controls.zoomRemainder_ == 0);
    sink.setVirtualCommand("JUMP", "touch", false);
    sink.setVirtualCommand("MOVEFORWARD", "touch", false);
    buttons.fill(false); frame();

    // Movement is command-only: never write keyboard state, including Shift
    // owned by another input path. Exercise actual applyMovement and pushed.
    auto& input = core::Input::getInstance();
    constexpr int keyboardW = 1, keyboardShift = 2; // opaque Input-double keys
    input.setVirtualKey(keyboardW, true);
    input.setVirtualKey(keyboardShift, true);
    const auto externalKeys = input.activeKeys;
    const int keyWrites = input.virtualKeyWrites;
    controls.applyMovement(0.0f, -1.0f);
    assert(input.virtualKeyWrites == keyWrites);
    controls.releaseKeys();
    struct Direction { float x, y, threshold; const char* command; };
    for (const auto d : {Direction{0, -1, 0.30f, "MOVEFORWARD"},
                         Direction{0, 1, 0.30f, "MOVEBACKWARD"},
                         Direction{-1, 0, 0.45f, "STRAFELEFT"},
                         Direction{1, 0, 0.45f, "STRAFERIGHT"}}) {
        const auto move = [&](float amount) { controls.applyMovement(d.x * amount, d.y * amount); };
        move(d.threshold); assert(active.empty() && !controls.steering_);
        const int edges = sink.edges[d.command];
        move(d.threshold + 0.01f);
        assert(active == std::set<std::string>{d.command} && controls.steering_);
        move(d.threshold - 0.05f);
        assert(active == std::set<std::string>{d.command} && controls.steering_);
        assert(sink.edges[d.command] == edges + 1);
        move(d.threshold - 0.10f); assert(active.empty() && !controls.steering_);
        move(d.threshold - 0.05f); assert(active.empty());
        move(1.0f); controls.releaseKeys();
        assert(active.empty() && !controls.steering_);
        // Reset forgets hysteresis too: the release band cannot restart a walk.
        move(d.threshold - 0.05f); assert(active.empty() && !controls.steering_);
    }
    controls.applyMovement(-1.0f, -1.0f);
    assert((active == std::set<std::string>{"MOVEFORWARD", "STRAFELEFT"}));
    controls.applyMovement(1.0f, 1.0f);
    assert((active == std::set<std::string>{"MOVEBACKWARD", "STRAFERIGHT"}));
    controls.releaseKeys(); assert(active.empty());
    // Neutral and reset release only the pad's movement source, never touch.
    const std::set<std::string> movementCommands{
        "MOVEFORWARD", "MOVEBACKWARD", "STRAFELEFT", "STRAFERIGHT"};
    for (const auto& cmd : movementCommands) sink.setVirtualCommand(cmd, "touch", true);
    controls.applyMovement(-1.0f, -1.0f);
    controls.applyMovement(0.0f, 0.0f);
    assert(active == movementCommands && !controls.steering_);
    controls.applyMovement(1.0f, 1.0f);
    controls.releaseKeys();
    assert(active == movementCommands && !controls.steering_);
    for (const auto& cmd : movementCommands) {
        assert(sink.sources[cmd] == std::set<std::string>{"touch"});
        sink.setVirtualCommand(cmd, "touch", false);
    }
    assert(input.virtualKeyWrites == keyWrites && input.activeKeys == externalKeys);
    input.setVirtualKey(keyboardW, false);
    input.setVirtualKey(keyboardShift, false);

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
