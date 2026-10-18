// cmake --build build --target check_recording_binding
// ctest --test-dir build -R '^recording_binding$' --output-on-failure
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "ui/framexml_emitter.hpp"
#include "ui/xml_parser.hpp"
#include "ui/keybinding_manager.hpp"
#include <SDL3/SDL.h>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>

int main() {
    using namespace wowee::ui;
    assert(SDL_Init(SDL_INIT_EVENTS));
    ImGui::CreateContext();
    // Execute the real emitter's output, not a second custom command catalogue.
    XmlNode root;
    std::string error;
    assert(parseXml("<Bindings><Binding name='SCREENSHOT' header='MISC'>"
                    "Screenshot();</Binding></Bindings>", root, error));
    const auto emitted = emitFrameXml(root);
    assert(emitted.warnings.empty());
    lua_State* L = luaL_newstate();
    assert(luaL_dostring(L, emitted.lua.c_str()) == 0);
    lua_getglobal(L, "__WoweeBindings");
    assert(lua_objlen(L, -1) == 3);
    lua_rawgeti(L, -1, 2);
    assert(std::string(lua_tostring(L, -1)) == "SCREENSHOT");
    lua_pop(L, 1);
    lua_rawgeti(L, -1, 3);
    const std::string command = lua_tostring(L, -1);
    assert(command == "WOWEETOGGLERECORDING");
    lua_settop(L, 0);
    lua_getglobal(L, ("BINDING_NAME_" + command).c_str());
    assert(std::string(lua_tostring(L, -1)) == "Start/Stop Video Recording");
    lua_getglobal(L, "__WoweeBindingScripts");
    lua_getfield(L, -1, command.c_str());
    assert(lua_isfunction(L, -1));
    lua_pushstring(L, "down");
    assert(lua_pcall(L, 1, 0, 0) == 0); // Native command body must not raise.

    auto& manager = KeybindingManager::getInstance();
    assert(manager.commandForKey("SHIFT-PRINTSCREEN") == command);
    assert(manager.commandForKey("PRINTSCREEN") == "SCREENSHOT");
    // Native gameplay uses isCommandPressed(command) to request the toggle.
    // Exercise real physical edges and both slots, without an encoder/window.
    const auto press = [&](SDL_Scancode scan, SDL_Keymod mods = SDL_KMOD_NONE) {
        manager.beginInputFrame();
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.scancode = scan;
        event.key.mod = mods;
        manager.processInputEvent(event);
        event.type = SDL_EVENT_KEY_UP;
        manager.processInputEvent(event);
    };
    for (int i = 0; i < 2; ++i) {
        press(SDL_SCANCODE_PRINTSCREEN, SDL_KMOD_SHIFT);
        assert(manager.isCommandPressed(command)); // Start, then stop request.
        assert(!manager.isCommandPressed("SCREENSHOT"));
    }
    press(SDL_SCANCODE_PRINTSCREEN);
    assert(manager.isCommandPressed("SCREENSHOT"));
    assert(!manager.isCommandPressed(command));
    assert(manager.setBinding("SHIFT-PRINTSCREEN"));
    assert(manager.setBinding("CTRL-F8", command));
    assert(manager.setBinding("ALT-F9", command));
    assert(manager.bindings().at(command)[0] == "CTRL-F8");
    assert(manager.bindings().at(command)[1] == "ALT-F9");
    press(SDL_SCANCODE_PRINTSCREEN, SDL_KMOD_SHIFT);
    assert(!manager.isCommandPressed(command));
    assert(!manager.isCommandPressed("SCREENSHOT"));
    press(SDL_SCANCODE_F8, SDL_KMOD_CTRL);
    assert(manager.isCommandPressed(command));
    press(SDL_SCANCODE_F9, SDL_KMOD_ALT);
    assert(manager.isCommandPressed(command));

    const auto path = std::filesystem::temp_directory_path() /
        ("wowee-recording-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cfg");
    const auto edited = manager.bindings();
    assert(manager.saveBindings(path.string()));
    manager.resetToDefaults();
    assert(manager.loadBindings(path.string()));
    assert(manager.bindings() == edited);
    assert(manager.setBinding("CTRL-F8"));
    assert(manager.setBinding("ALT-F9"));
    assert(manager.saveBindings(path.string()));
    manager.resetToDefaults();
    assert(manager.loadBindings(path.string()));
    for (const auto [scan, mods] : {std::pair{SDL_SCANCODE_PRINTSCREEN, SDL_KMOD_SHIFT},
                                   std::pair{SDL_SCANCODE_F8, SDL_KMOD_CTRL},
                                   std::pair{SDL_SCANCODE_F9, SDL_KMOD_ALT}}) {
        press(scan, mods);
        assert(!manager.isCommandPressed(command));
    }
    press(SDL_SCANCODE_PRINTSCREEN);
    assert(manager.isCommandPressed("SCREENSHOT"));
    std::filesystem::remove(path);
    lua_close(L);
    ImGui::DestroyContext();
    SDL_Quit();
}
