// Standalone regression check (MSYS2 UCRT64, repo root):
// g++ -std=c++20 -Iinclude -Iextern/imgui tests/check_keybindings.cpp src/ui/keybinding_manager.cpp src/core/logger.cpp extern/imgui/imgui{,_draw,_tables,_widgets}.cpp -lSDL3 -o build/check_keybindings.exe && build/check_keybindings.exe
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "ui/keybinding_manager.hpp"
#include "addons/binding_file.hpp"
#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

int main() {
    using Manager = wowee::ui::KeybindingManager;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800, 600);
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto& bindings = Manager::getInstance();
    const auto frame = [&](ImGuiKey key, bool down, bool shift = false) {
        bindings.beginInputFrame();
        io.AddKeyEvent(ImGuiMod_Shift, shift);
        io.AddKeyEvent(key, down);
        ImGui::NewFrame();
        ImGui::EndFrame();
    };

    assert(bindings.commandForKey("").empty());
    assert(bindings.setBinding("W"));
    assert(bindings.setBinding("UP"));
    assert(bindings.setBinding("R", "MOVEFORWARD"));
    assert(bindings.setBinding("SHIFT-F8", "MOVEFORWARD"));
    frame(ImGuiKey_W, true);
    assert(!bindings.isCommandDown("MOVEFORWARD")); // No hardcoded fallback.
    frame(ImGuiKey_W, false);
    frame(ImGuiKey_R, true);
    assert(bindings.isCommandDown("MOVEFORWARD"));
    assert(bindings.isCommandPressed("MOVEFORWARD"));
    frame(ImGuiKey_R, true);
    assert(!bindings.isCommandPressed("MOVEFORWARD"));
    frame(ImGuiKey_R, false);
    frame(ImGuiKey_F8, true, true);
    assert(bindings.isCommandDown("MOVEFORWARD")); // Secondary plus modifier.
    frame(ImGuiKey_F8, false);
    frame(ImGuiKey_R, true, true);
    assert(bindings.isCommandDown("MOVEFORWARD")); // Unbound Shift-R falls back to R.
    assert(bindings.setBinding("SHIFT-R", "ACTIONBUTTON1"));
    assert(!bindings.isCommandDown("MOVEFORWARD"));
    assert(bindings.isCommandDown("ACTIONBUTTON1"));
    frame(ImGuiKey_R, false);

    // A formerly reserved movement key can open a panel instead.
    assert(bindings.setBinding("W", "TOGGLECHARACTER0"));
    frame(ImGuiKey_W, true);
    assert(bindings.isActionPressed(Manager::Action::TOGGLE_CHARACTER_SCREEN));
    wowee::ui::noteInterfaceConsumedKey(ImGuiKey_W);
    assert(!bindings.isCommandPressed("TOGGLECHARACTER0"));
    wowee::ui::clearInterfaceConsumedKeys();
    wowee::ui::setTypedInputProbe([] { return true; });
    assert(!bindings.isCommandDown("TOGGLECHARACTER0"));
    wowee::ui::setTypedInputProbe({});
    io.WantTextInput = true;
    assert(!bindings.isCommandDown("TOGGLECHARACTER0"));
    io.WantTextInput = false;
    frame(ImGuiKey_W, false);

    assert(bindings.setBinding("MOUSEX1", "TOGGLEAUTORUN"));
    assert(bindings.commandForKey("BUTTON4") == "TOGGLEAUTORUN");
    assert(bindings.setBinding("PAD1", "JUMP"));
    bindings.setVirtualKey("PAD1", true);
    assert(bindings.isCommandPressed("JUMP"));
    bindings.beginInputFrame();
    assert(!bindings.isCommandPressed("JUMP"));
    assert(bindings.isCommandDown("JUMP"));
    bindings.setVirtualKey("PAD1", false);
    // Releasing one source must not cancel another device's held command.
    bindings.setVirtualCommand("JUMP", "stick", true);
    bindings.setVirtualCommand("JUMP", "button", true);
    bindings.setVirtualCommand("JUMP", "stick", false);
    assert(bindings.isCommandDown("JUMP"));
    bindings.setVirtualCommand("JUMP", "button", false);
    assert(!bindings.isCommandDown("JUMP"));

    assert(bindings.setBinding("MOUSEWHEELUP", "SCREENSHOT"));
    bindings.setVirtualKey("MOUSEWHEELUP", true);
    assert(bindings.isCommandPressed("SCREENSHOT"));
    bindings.beginInputFrame();
    assert(!bindings.isCommandPressed("SCREENSHOT"));
    assert(!bindings.isCommandDown("SCREENSHOT"));

    const auto beforeInvalid = bindings.bindings();
    assert(!bindings.setBinding("SHIFT-NOT_A_KEY", "JUMP"));
    assert(!bindings.setBinding("F9", "HEADER_MOVEMENT"));
    assert(!bindings.setBinding("F9", "JUMP\nSCREENSHOT"));
    assert(bindings.bindings() == beforeInvalid);
    for (auto key : {ImGuiKey_Minus, ImGuiKey_Equal, ImGuiKey_Comma,
                     ImGuiKey_NumLock, ImGuiKey_Keypad7, ImGuiKey_KeypadDivide,
                     ImGuiKey_PrintScreen, ImGuiKey_MouseX1, ImGuiKey_MouseX2})
        assert(Manager::keyFromName(Manager::keyName(key)) == key);
    assert(Manager::keyNameFromSDL(SDLK_KP_7) == "NUMPAD7");
    assert(Manager::keyNameFromSDL(SDLK_PRINTSCREEN) == "PRINTSCREEN");
    assert(Manager::keyNameFromSDL(SDLK_MINUS) == "-");

    const auto path = std::filesystem::temp_directory_path() /
        ("wowee-bindings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cfg");
    assert(bindings.setBinding(",", "ACTIONBUTTON11"));
    assert(bindings.setBinding("=", "ACTIONBUTTON12"));
    // Resolve the recording default's ownership before arranging a secondary-only slot.
    assert(bindings.setBinding("SHIFT-PRINTSCREEN", "SCREENSHOT"));
    bindings.bindings()["SCREENSHOT"] = {"", "SHIFT-PRINTSCREEN"};
    bindings.bindings()["MOVEFORWARD"] = {}; // Unbound survives reload.
    const auto saved = bindings.bindings();
    assert(bindings.saveBindings(path.string()));
    bindings.resetToDefaults();
    assert(bindings.loadBindings(path.string()));
    assert(bindings.bindings() == saved);
    const auto missing = path.string() + ".missing";
    assert(!bindings.loadBindings(missing));
    assert(bindings.bindings() == saved);
    assert(!bindings.saveBindings((path / "not-a-directory.cfg").string()));
    std::filesystem::remove(path);
    std::istringstream legacy("ACTIONBUTTON12==,SHIFT-=\nJUMP=SPACE,\n");
    const auto parsed = wowee::addons::parseSavedBindings(legacy);
    assert(parsed.at("ACTIONBUTTON12")[0] == "=");
    assert(parsed.at("ACTIONBUTTON12")[1] == "SHIFT-=");
    assert(parsed.at("JUMP")[0] == "SPACE");
    bindings.resetToDefaults();
    ImGui::DestroyContext();
}
