// Run from the repo root (MSYS2 UCRT64):
// g++ -std=c++20 -Iinclude -Iextern/imgui tests/check_autorun_binding.cpp src/ui/keybinding_manager.cpp src/core/logger.cpp extern/imgui/imgui{,_draw,_tables,_widgets}.cpp -lSDL3 -o build/check_autorun_binding.exe && build/check_autorun_binding.exe
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "ui/keybinding_manager.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>

int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800, 600);
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto& manager = wowee::ui::KeybindingManager::getInstance();
    const auto action = wowee::ui::KeybindingManager::Action::TOGGLE_AUTORUN;
    assert(manager.getKeyForAction(action) == ImGuiKey_GraveAccent);
    assert(manager.bindings().at("TOGGLEAUTORUN")[1] == "NUMLOCK");
    const auto press = [&](ImGuiKey key, bool down) {
        io.AddKeyEvent(key, down);
        ImGui::NewFrame();
        const bool pressed = manager.isActionPressed(action);
        ImGui::EndFrame();
        return pressed;
    };
    for (const auto key : {ImGuiKey_GraveAccent, ImGuiKey_NumLock}) {
        assert(press(key, true));
        assert(!press(key, true));
        assert(!press(key, false));
    }
    manager.setKeyForAction(action, ImGuiKey_R);
    assert(!press(ImGuiKey_GraveAccent, true));
    assert(!press(ImGuiKey_GraveAccent, false));
    assert(!press(ImGuiKey_NumLock, true));
    assert(!press(ImGuiKey_NumLock, false));
    assert(press(ImGuiKey_R, true));
    assert(!press(ImGuiKey_R, true)); // Held keys must not toggle repeatedly.
    assert(!press(ImGuiKey_R, false));
    wowee::ui::setTypedInputProbe([] { return true; });
    assert(!press(ImGuiKey_R, true));
    assert(!press(ImGuiKey_R, false));
    wowee::ui::setTypedInputProbe({});

    for (const auto key : {ImGuiKey_MouseX1, ImGuiKey_MouseX2}) {
        manager.setKeyForAction(action, key);
        const int button = key == ImGuiKey_MouseX1 ? 3 : 4;
        const auto click = [&](bool down) {
            io.AddMouseButtonEvent(button, down);
            ImGui::NewFrame();
            ImGui::EndFrame(); // The camera polls after the preceding UI frame.
            return manager.isActionPressed(action);
        };
        assert(click(true));
        assert(!click(true)); // Holding must not toggle autorun repeatedly.
        assert(!click(false));
        wowee::ui::setTypedInputProbe([] { return true; });
        assert(!click(true));
        assert(!click(false));
        wowee::ui::setTypedInputProbe({});
    }

    const auto path = std::filesystem::temp_directory_path() /
        ("wowee-autorun-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cfg");
    for (const auto key : {ImGuiKey_R, ImGuiKey_NumLock, ImGuiKey_MouseX1, ImGuiKey_MouseX2}) {
        manager.setKeyForAction(action, key);
        manager.saveToConfigFile(path.string());
        manager.setKeyForAction(action, ImGuiKey_F8);
        manager.loadFromConfigFile(path.string());
        assert(manager.getKeyForAction(action) == key);
    }
    std::filesystem::remove(path);
    manager.resetToDefaults();
    ImGui::DestroyContext();
}
