#include "ui/keybinding_manager.hpp"
#include "core/logger.hpp"
#include "addons/binding_file.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace wowee::ui {
namespace {
constexpr const char* legacySettingsMarker = "# wowee-native-settings-merged-v1";
struct ActionBinding { const char* command; const char* legacy; const char* label; const char* key; const char* secondKey = ""; };
constexpr ActionBinding actions[] = {
    {"TOGGLECHARACTER0", "toggle_character_screen", "Character Screen", "C"},
    {"OPENALLBAGS", "toggle_inventory", "Inventory", "I"},
    {"TOGGLEBACKPACK", "toggle_bags", "Bags", "B"},
    {"TOGGLESPELLBOOK", "toggle_spellbook", "Spellbook", "P"},
    {"TOGGLETALENTS", "toggle_talents", "Talents", "N"},
    {"TOGGLEQUESTLOG", "toggle_quests", "Quests", "L"},
    {"TOGGLEMINIMAP", "toggle_minimap", "Minimap", ""},
    {"TOGGLEGAMEMENU", "toggle_settings", "Settings", "ESCAPE"},
    {"OPENCHAT", "toggle_chat", "Chat", "ENTER", "NUMPADENTER"},
    {"TOGGLESOCIAL", "toggle_guild_roster", "Social", "O"},
    {"TOGGLELFGPARENT", "toggle_dungeon_finder", "Dungeon Finder", "J"},
    {"TOGGLEWORLDMAP", "toggle_world_map", "World Map", "M"},
    {"NAMEPLATES", "toggle_nameplates", "Nameplates", "V"},
    {"TOGGLEACHIEVEMENT", "toggle_achievements", "Achievements", "Y"},
    {"TOGGLECHARACTER1", "toggle_skills", "Skills / Professions", "K"},
    {"TOGGLECHARACTER4", "toggle_pvp", "Player vs Player", "H"},
    {"TOGGLEAUTORUN", "toggle_autorun", "Autorun", "`", "NUMLOCK"},
};
static_assert(std::size(actions) == KeybindingManager::getActionCount());
std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::function<bool()>& typedInputProbe() { static std::function<bool()> probe; return probe; }
std::vector<ImGuiKey>& consumedKeys() { static std::vector<ImGuiKey> keys; return keys; }
int& slashEchoPumps() { static int pumps = 0; return pumps; }
}

KeybindingManager& KeybindingManager::getInstance() { static KeybindingManager instance; return instance; }
KeybindingManager::KeybindingManager() { initializeDefaults(); }
const char* KeybindingManager::commandForAction(Action action) {
    const auto i = static_cast<size_t>(action);
    return i < std::size(actions) ? actions[i].command : "";
}
void KeybindingManager::initializeDefaults() {
    for (const auto& action : actions) bindings_[action.command] = {action.key, action.secondKey};
    const Bindings defaults = {
        {"MOVEFORWARD", {"W", "UP"}}, {"MOVEBACKWARD", {"S", "DOWN"}},
        {"TURNLEFT", {"A", "LEFT"}}, {"TURNRIGHT", {"D", "RIGHT"}},
        {"STRAFELEFT", {"Q", ""}}, {"STRAFERIGHT", {"E", ""}},
        {"JUMP", {"SPACE", ""}}, {"SITORSTAND", {"X", ""}},
        {"TOGGLERUN", {"NUMPADDIVIDE", ""}}, {"TOGGLESHEATH", {"Z", ""}},
        {"OPENCHATSLASH", {"/", ""}}, {"TARGETNEARESTENEMY", {"TAB", ""}},
        {"SCREENSHOT", {"PRINTSCREEN", ""}}, {"FRIENDNAMEPLATES", {"SHIFT-V", ""}},
        {"WOWEETOGGLERECORDING", {"SHIFT-PRINTSCREEN", ""}},
        {"CAMERAZOOMIN", {"MOUSEWHEELUP", ""}}, {"CAMERAZOOMOUT", {"MOUSEWHEELDOWN", ""}},
    };
    bindings_.insert(defaults.begin(), defaults.end());
    for (int i = 1; i <= 12; ++i) {
        const std::string key = i < 10 ? std::to_string(i) : i == 10 ? "0" : i == 11 ? "-" : "=";
        bindings_["ACTIONBUTTON" + std::to_string(i)] = {key, ""};
        bindings_["MULTIACTIONBAR1BUTTON" + std::to_string(i)] = {"SHIFT-" + key, ""};
        if (i <= 8) bindings_["SHAPESHIFTBUTTON" + std::to_string(i)] = {"CTRL-" + key, ""};
    }
}
void KeybindingManager::resetToDefaults() { bindings_.clear(); initializeDefaults(); }

std::string KeybindingManager::keyName(ImGuiKey key) {
    switch (key) {
        case ImGuiKey_None: return "";
        case ImGuiKey_LeftArrow: return "LEFT";
        case ImGuiKey_RightArrow: return "RIGHT";
        case ImGuiKey_UpArrow: return "UP";
        case ImGuiKey_DownArrow: return "DOWN";
        case ImGuiKey_Apostrophe: return "'";
        case ImGuiKey_Comma: return ",";
        case ImGuiKey_Minus: return "-";
        case ImGuiKey_Period: return ".";
        case ImGuiKey_Slash: return "/";
        case ImGuiKey_Semicolon: return ";";
        case ImGuiKey_Equal: return "=";
        case ImGuiKey_LeftBracket: return "[";
        case ImGuiKey_RightBracket: return "]";
        case ImGuiKey_Backslash: return "\\";
        case ImGuiKey_GraveAccent: return "`";
        case ImGuiKey_KeypadDecimal: return "NUMPADDECIMAL";
        case ImGuiKey_KeypadDivide: return "NUMPADDIVIDE";
        case ImGuiKey_KeypadMultiply: return "NUMPADMULTIPLY";
        case ImGuiKey_KeypadSubtract: return "NUMPADMINUS";
        case ImGuiKey_KeypadAdd: return "NUMPADPLUS";
        case ImGuiKey_KeypadEnter: return "NUMPADENTER";
        case ImGuiKey_KeypadEqual: return "NUMPADEQUALS";
        case ImGuiKey_LeftCtrl: return "LCTRL";
        case ImGuiKey_RightCtrl: return "RCTRL";
        case ImGuiKey_LeftShift: return "LSHIFT";
        case ImGuiKey_RightShift: return "RSHIFT";
        case ImGuiKey_LeftAlt: return "LALT";
        case ImGuiKey_RightAlt: return "RALT";
        case ImGuiKey_LeftSuper: return "LGUI";
        case ImGuiKey_RightSuper: return "RGUI";
        case ImGuiKey_MouseLeft: return "BUTTON1";
        case ImGuiKey_MouseRight: return "BUTTON2";
        case ImGuiKey_MouseMiddle: return "BUTTON3";
        case ImGuiKey_MouseX1: return "BUTTON4";
        case ImGuiKey_MouseX2: return "BUTTON5";
        default: break;
    }
    if (key >= ImGuiKey_Keypad0 && key <= ImGuiKey_Keypad9) return "NUMPAD" + std::to_string(key - ImGuiKey_Keypad0);
    if (key < ImGuiKey_NamedKey_BEGIN || key >= ImGuiKey_GamepadStart) return "";
    return upper(ImGui::GetKeyName(key));
}
ImGuiKey KeybindingManager::keyFromName(const std::string& name) {
    if (name.empty()) return ImGuiKey_None;
    if (name == "MOUSEX1" || name == "MOUSE4") return ImGuiKey_MouseX1;
    if (name == "MOUSEX2" || name == "MOUSE5") return ImGuiKey_MouseX2;
    // Built once, in the order the linear scan visited, first match winning, so
    // answers are unchanged. Every binding poll resolves its key through here.
    static const std::unordered_map<std::string, ImGuiKey> byName = [] {
        std::unordered_map<std::string, ImGuiKey> map;
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
            const auto key = static_cast<ImGuiKey>(k);
            map.emplace(keyName(key), key);
            map.emplace(upper(ImGui::GetKeyName(key)), key);
        }
        map.erase("");
        return map;
    }();
    const auto it = byName.find(name);
    return it == byName.end() ? ImGuiKey_None : it->second;
}
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
std::string KeybindingManager::keyNameFromSDL(uint32_t key) {
    switch (key) {
        case SDLK_RETURN: return "ENTER";
        case SDLK_KP_0: return "NUMPAD0";
        case SDLK_KP_1: return "NUMPAD1";
        case SDLK_KP_2: return "NUMPAD2";
        case SDLK_KP_3: return "NUMPAD3";
        case SDLK_KP_4: return "NUMPAD4";
        case SDLK_KP_5: return "NUMPAD5";
        case SDLK_KP_6: return "NUMPAD6";
        case SDLK_KP_7: return "NUMPAD7";
        case SDLK_KP_8: return "NUMPAD8";
        case SDLK_KP_9: return "NUMPAD9";
        case SDLK_KP_PERIOD: return "NUMPADDECIMAL";
        case SDLK_KP_DIVIDE: return "NUMPADDIVIDE";
        case SDLK_KP_MULTIPLY: return "NUMPADMULTIPLY";
        case SDLK_KP_MINUS: return "NUMPADMINUS";
        case SDLK_KP_PLUS: return "NUMPADPLUS";
        case SDLK_KP_ENTER: return "NUMPADENTER";
        case SDLK_KP_EQUALS: return "NUMPADEQUALS";
        case SDLK_LCTRL: return "LCTRL";
        case SDLK_RCTRL: return "RCTRL";
        case SDLK_LSHIFT: return "LSHIFT";
        case SDLK_RSHIFT: return "RSHIFT";
        case SDLK_LALT: return "LALT";
        case SDLK_RALT: return "RALT";
        case SDLK_LGUI: return "LGUI";
        case SDLK_RGUI: return "RGUI";
        case SDLK_NUMLOCKCLEAR: return "NUMLOCK";
        default: return upper(SDL_GetKeyName(key));
    }
}
std::string KeybindingManager::modifiedKey(std::string key, bool shift, bool ctrl, bool alt) {
    if (shift) key = "SHIFT-" + key;
    if (ctrl) key = "CTRL-" + key;
    if (alt) key = "ALT-" + key;
    return key;
}

bool KeybindingManager::setBinding(const std::string& requestedKey, const std::string& command) {
    std::string key = requestedKey;
    std::string base = key;
    for (const char* mod : {"ALT-", "CTRL-", "SHIFT-"}) if (base.starts_with(mod)) base.erase(0, std::char_traits<char>::length(mod));
    if (key.find_first_of("\r\n\t") != std::string::npos || base.empty()) return false;
    const bool pad = base.starts_with("PAD") && base.size() > 3 && base.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == std::string::npos;
    const auto physical = keyFromName(base);
    if (physical == ImGuiKey_None && !pad && base != "MOUSEWHEELUP" && base != "MOUSEWHEELDOWN") return false;
    if (physical != ImGuiKey_None) key.replace(key.size() - base.size(), base.size(), keyName(physical));
    if (command.starts_with("HEADER_") || command.find_first_of("\r\n\t=") != std::string::npos) return false;
    for (auto& [name, slots] : bindings_) for (auto& slot : slots) if (slot == key) slot.clear();
    if (!command.empty()) {
        auto& slots = bindings_[command];
        (slots[0].empty() ? slots[0] : slots[1]) = key;
    }
    return true;
}
bool KeybindingManager::loadBindings(const std::string& path) {
    std::ifstream in(path);
    if (!in) { LOG_WARNING("Could not read bindings from ", path); return false; }
    const auto saved = wowee::addons::parseSavedBindings(in);
    if (in.bad()) { LOG_WARNING("Could not read bindings from ", path); return false; }

    Bindings legacyDefaults;
    in.clear();
    in.seekg(0);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.ends_with(',') || line.find('\t') != std::string::npos) continue;
        const auto command = line.substr(0, line.find('='));
        const auto original = bindings_.find(command);
        const auto row = saved.find(command);
        if (original == bindings_.end() || row == saved.end() ||
            row->second[0].empty() || !row->second[1].empty()) continue;
        const auto& defaults = original->second;
        legacyDefaults[command][1] = defaults[1] == row->second[0] ? defaults[0] : defaults[1];
    }
    // Clear every saved command first: unbound slots must replace defaults too.
    for (const auto& [command, slots] : saved) bindings_[command] = {};
    for (const auto& [command, slots] : saved) {
        for (size_t i = 0; i < slots.size(); ++i) {
            if (slots[i].empty()) continue;
            if (setBinding(slots[i], command)) {
                // Preserve a secondary-only assignment rather than compacting it.
                auto& target = bindings_[command];
                if (i == 1 && slots[0].empty()) std::swap(target[0], target[1]);
            }
        }
    }
    // Recover only unclaimed defaults after all saved keys have been applied.
    for (const auto& [command, slots] : legacyDefaults) {
        if (!slots[1].empty() && commandForKey(slots[1]).empty()) setBinding(slots[1], command);
    }
    in.clear();
    in.seekg(0);
    std::string header;
    std::getline(in, header);
    if (!header.empty() && header.back() == '\r') header.pop_back();
    legacySettingsMigrated_ = legacySettingsMigrated_ || header == legacySettingsMarker;
    return true;
}
bool KeybindingManager::loadBindingsWithLegacy(const std::string& path, const std::string& legacyPath) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec || (exists && !loadBindings(path))) return false;
    if (legacySettingsMigrated_) return true;
    const bool legacyExists = std::filesystem::exists(legacyPath, ec);
    if (ec || (legacyExists && !loadFromConfigFile(legacyPath))) return false;
    // Settings used to override FrameXML's native actions, including explicit
    // unbinds. Merge once, then let the unified store win. Keep this guard even
    // if saving fails: Defaults/Cancel must not resurrect legacy assignments.
    // The disk marker is published only together with a successful atomic save.
    legacySettingsMigrated_ = true;
    return saveBindings(path);
}
bool KeybindingManager::saveBindings(const std::string& path) const {
    const std::filesystem::path file(path), temp(path + ".tmp");
    std::error_code ec;
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(temp, std::ios::trunc);
    if (legacySettingsMigrated_) out << legacySettingsMarker << '\n';
    // Tab is unambiguous even when either key is ',' or '='. Read legacy commas too.
    for (const auto& [command, slots] : bindings_) out << command << '=' << slots[0] << '\t' << slots[1] << '\n';
    out.close();
    if (!out) { LOG_WARNING("Could not write bindings to ", temp.string()); return false; }
#ifdef _WIN32
    if (MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
#else
    std::filesystem::rename(temp, file, ec);
    if (!ec) return true;
#endif
    LOG_WARNING("Could not replace bindings at ", path, "; previous file retained");
    return false;
}
std::string KeybindingManager::commandForKey(const std::string& key) const {
    if (key.empty()) return "";
    for (const auto& [command, slots] : bindings_) if (slots[0] == key || slots[1] == key) return command;
    return "";
}
void KeybindingManager::beginInputFrame() {
    pressedKeys_.clear();
    virtualPressed_.clear();
    virtualCommandPressed_.clear();
    virtualKeys_["MOUSEWHEELUP"] = virtualKeys_["MOUSEWHEELDOWN"] = false;
}
void KeybindingManager::processInputEvent(const SDL_Event& event) {
    nativeInput_ = true;
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        inputFocused_ = false;
        down_.fill(false);
        pressedKeys_.clear();
        virtualKeys_.clear();
        virtualPressed_.clear();
        virtualCommands_.clear();
        virtualCommandPressed_.clear();
        return;
    }
    if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) inputFocused_ = true;
    if (!inputFocused_) return;

    ImGuiKey key = ImGuiKey_None;
    bool down = false;
    bool repeat = false;
    bool shift = false, ctrl = false, alt = false;
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
        key = keyFromName(keyNameFromSDL(SDL_GetKeyFromScancode(event.key.scancode, SDL_KMOD_NONE, true)));
        down = event.type == SDL_EVENT_KEY_DOWN;
        repeat = event.key.repeat;
        shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
        ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
        alt = (event.key.mod & SDL_KMOD_ALT) != 0;
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        key = keyFromName(mouseButtonNameFromSDL(event.button.button));
        down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        // Unlike SDL_GetModState(), these reflect the queue position, not its final snapshot.
        shift = down_[ImGuiKey_LeftShift - ImGuiKey_NamedKey_BEGIN] || down_[ImGuiKey_RightShift - ImGuiKey_NamedKey_BEGIN];
        ctrl = down_[ImGuiKey_LeftCtrl - ImGuiKey_NamedKey_BEGIN] || down_[ImGuiKey_RightCtrl - ImGuiKey_NamedKey_BEGIN];
        alt = down_[ImGuiKey_LeftAlt - ImGuiKey_NamedKey_BEGIN] || down_[ImGuiKey_RightAlt - ImGuiKey_NamedKey_BEGIN];
    }
    if (key == ImGuiKey_None) return;
    down_[key - ImGuiKey_NamedKey_BEGIN] = down;
    // Repeats and modifier-only changes never manufacture another action-key edge.
    // Keep the chord even if its modifiers or key are released later in this pump.
    if (down && !repeat && !interfaceTakingTypedInput() && !ImGui::GetIO().WantTextInput) {
        const auto base = keyName(key);
        const auto chord = modifiedKey(base, shift, ctrl, alt);
        // Resolve fallback at the edge, before modifiers can change in this pump.
        pressedKeys_.insert(commandForKey(chord).empty() ? base : chord);
    }
}
void KeybindingManager::updateInput() {
    nativeInput_ = true;
    if (!inputFocused_) return;
    std::array<bool, ImGuiKey_NamedKey_COUNT> next{};
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    for (int s = 1; s < count; ++s) {
        if (!keys[s]) continue;
        // key_event=true respects the current layout but not held Shift/CapsLock.
        const auto name = keyNameFromSDL(SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(s), SDL_KMOD_NONE, true));
        const auto key = keyFromName(name);
        if (key != ImGuiKey_None) next[key - ImGuiKey_NamedKey_BEGIN] = true;
    }
    const auto mouse = SDL_GetMouseState(nullptr, nullptr);
    for (uint8_t button = SDL_BUTTON_LEFT; button <= SDL_BUTTON_X2; ++button) {
        const auto key = keyFromName(mouseButtonNameFromSDL(button));
        next[key - ImGuiKey_NamedKey_BEGIN] = (mouse & SDL_BUTTON_MASK(button)) != 0;
    }
    down_ = next;
}
void KeybindingManager::setVirtualKey(const std::string& key, bool down) {
    if (down && !virtualKeys_[key]) virtualPressed_.insert(key);
    virtualKeys_[key] = down;
}
void KeybindingManager::setVirtualCommand(const std::string& command, const std::string& source, bool down) {
    if (command.empty()) return;
    auto& sources = virtualCommands_[command];
    if (down && sources.empty()) virtualCommandPressed_.insert(command);
    if (down) sources.insert(source); else sources.erase(source);
    if (sources.empty()) virtualCommandPressed_.erase(command);
}
bool KeybindingManager::keyActive(const std::string& binding, bool pressed, bool repeat) const {
    if (binding.empty()) return false;
    std::string base = binding;
    bool alt = false, ctrl = false, shift = false;
    for (auto [prefix, flag] : {std::pair{"ALT-", &alt}, {"CTRL-", &ctrl}, {"SHIFT-", &shift}}) {
        if (base.starts_with(prefix)) { *flag = true; base.erase(0, std::char_traits<char>::length(prefix)); }
    }
    const auto& io = ImGui::GetIO();
    const ImGuiKey key = keyFromName(base);
    if (key != ImGuiKey_None && interfaceConsumedKey(key)) return false;
    if ((base.starts_with("BUTTON") || base.starts_with("MOUSEWHEEL")) && io.WantCaptureMouse) return false;
    if (pressed && nativeInput_ && pressedKeys_.count(binding)) return true;
    const auto held = [&](ImGuiKey key) { return nativeInput_ ? down_[key - ImGuiKey_NamedKey_BEGIN] : ImGui::IsKeyDown(key); };
    const bool actualShift = nativeInput_ ? held(ImGuiKey_LeftShift) || held(ImGuiKey_RightShift) : io.KeyShift;
    const bool actualCtrl = nativeInput_ ? held(ImGuiKey_LeftCtrl) || held(ImGuiKey_RightCtrl) : io.KeyCtrl;
    const bool actualAlt = nativeInput_ ? held(ImGuiKey_LeftAlt) || held(ImGuiKey_RightAlt) : io.KeyAlt;
    if (shift != actualShift || ctrl != actualCtrl || alt != actualAlt) {
        // Unmodified movement still works with modifiers unless that chord was rebound.
        if (shift || ctrl || alt || !commandForKey(modifiedKey(base, actualShift, actualCtrl, actualAlt)).empty()) return false;
    }
    const auto v = virtualKeys_.find(base);
    if (v != virtualKeys_.end() && (pressed ? virtualPressed_.count(base) != 0 : v->second)) return true;
    if (key == ImGuiKey_None) return false;
    if (!pressed) return held(key);
    if (nativeInput_) return false;
    return ImGui::IsKeyPressed(key, repeat);
}
bool KeybindingManager::isCommandPressed(const std::string& command, bool repeat) const {
    if (interfaceTakingTypedInput() || ImGui::GetIO().WantTextInput) return false;
    if (virtualCommandPressed_.count(command)) return true;
    const auto it = bindings_.find(command);
    if (it == bindings_.end()) return false;
    return keyActive(it->second[0], true, repeat) || keyActive(it->second[1], true, repeat);
}
bool KeybindingManager::isCommandDown(const std::string& command) const {
    if (interfaceTakingTypedInput() || ImGui::GetIO().WantTextInput) return false;
    const auto v = virtualCommands_.find(command);
    if (v != virtualCommands_.end() && !v->second.empty()) return true;
    const auto it = bindings_.find(command);
    return it != bindings_.end() && (keyActive(it->second[0], false) || keyActive(it->second[1], false));
}
bool KeybindingManager::isActionPressed(Action action, bool repeat) { return isCommandPressed(commandForAction(action), repeat); }
ImGuiKey KeybindingManager::getKeyForAction(Action action) const {
    const auto it = bindings_.find(commandForAction(action));
    return it == bindings_.end() ? ImGuiKey_None : keyFromName(it->second[0].empty() ? it->second[1] : it->second[0]);
}
void KeybindingManager::setKeyForAction(Action action, ImGuiKey key) {
    const std::string command = commandForAction(action);
    bindings_[command] = {};
    if (key != ImGuiKey_None) setBinding(keyName(key), command);
}

// Compatibility import/export for old settings and standalone tools only.
// The running UI saves bindings.cfg, never a second keybinding section.
bool KeybindingManager::loadFromConfigFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    const auto previous = bindings_;
    std::string line;
    bool section = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "[Keybindings]") { section = true; continue; }
        if (!line.empty() && line[0] == '[') { section = false; continue; }
        if (!section) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string command = line.substr(0, eq);
        if (command == "toggle_quest_log") command = "toggle_quests";
        for (const auto& action : actions) if (command == action.legacy) { command = action.command; break; }
        std::string keys = line.substr(eq + 1);
        const auto sep = keys.find('\t');
        std::array<std::string, 2> slots{keys.substr(0, sep), sep == std::string::npos ? "" : keys.substr(sep + 1)};
        bindings_[command] = {};
        for (auto& key : slots) {
            if (key.empty() || key == "None") continue;
            key = upper(key);
            const auto imgui = keyFromName(key);
            if (imgui != ImGuiKey_None) key = keyName(imgui);
            setBinding(key, command);
        }
    }
    if (in.bad()) { bindings_ = previous; return false; }
    return true;
}
void KeybindingManager::saveToConfigFile(const std::string& path) const {
    std::ifstream in(path);
    std::string content, line;
    bool section = false;
    while (std::getline(in, line)) {
        if (line == "[Keybindings]") { section = true; continue; }
        if (!line.empty() && line[0] == '[') section = false;
        if (!section) content += line + "\n";
    }
    content += "[Keybindings]\n";
    for (const auto& [command, slots] : bindings_) content += command + "=" + slots[0] + "\t" + slots[1] + "\n";
    std::ofstream out(path);
    if (!(out << content)) LOG_ERROR("Could not save bindings to ", path);
}

bool interfaceTakingTypedInput() { const auto& probe = typedInputProbe(); return probe && probe(); }
void setTypedInputProbe(std::function<bool()> probe) { typedInputProbe() = std::move(probe); }
bool interfaceConsumedKey(ImGuiKey key) { const auto& keys = consumedKeys(); return std::find(keys.begin(), keys.end(), key) != keys.end(); }
void noteInterfaceConsumedKey(ImGuiKey key) { if (!interfaceConsumedKey(key)) consumedKeys().push_back(key); }
void clearInterfaceConsumedKeys() { consumedKeys().clear(); }
void noteChatOpenedBySlash() { slashEchoPumps() = 2; }
bool consumeChatSlashEcho(const char* text) {
    if (slashEchoPumps() <= 0 || !text || text[0] != '/' || text[1] != '\0') return false;
    slashEchoPumps() = 0;
    return true;
}
void ageChatSlashEcho() { if (slashEchoPumps() > 0) --slashEchoPumps(); }
} // namespace wowee::ui
