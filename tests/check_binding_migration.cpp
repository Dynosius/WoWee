// Standalone (MSYS2 UCRT64, repo root):
// g++ -std=c++20 -Iinclude -Iextern/imgui tests/check_binding_migration.cpp src/ui/keybinding_manager.cpp src/core/logger.cpp extern/imgui/imgui{,_draw,_tables,_widgets}.cpp -lSDL3 -o build/check_binding_migration.exe && build/check_binding_migration.exe
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "ui/keybinding_manager.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;
std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
int main() {
    using Manager = wowee::ui::KeybindingManager;
    const auto root = fs::temp_directory_path() / ("wowee-migration-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    const auto file = root / "bindings.cfg", legacy = root / "settings.cfg";
    const std::string stale = "TOGGLECHARACTER0=C,\nTOGGLEQUESTLOG=L,\nJUMP=SPACE,PAD1\n";
    std::ofstream(file, std::ios::binary) << stale;
    std::ofstream(legacy) << "[Keybindings]\ntoggle_character_screen=F9\n"
        "toggle_quest_log=None\ntoggle_inventory=\n";
    // Copies start with pristine process state, allowing a restart without a window.
    auto manager = Manager::getInstance();
    assert(manager.loadBindingsWithLegacy(file.string(), legacy.string()));
    assert(manager.commandForKey("F9") == "TOGGLECHARACTER0");
    assert(manager.commandForKey("C").empty());
    assert(manager.bindings().at("TOGGLEQUESTLOG")[0].empty());
    assert(manager.bindings().at("OPENALLBAGS")[0].empty());
    assert(manager.commandForKey("PAD1") == "JUMP");
    assert(readFile(file).starts_with("# wowee-native-settings-merged-v1"));
    manager.setKeyForAction(Manager::Action::TOGGLE_CHARACTER_SCREEN, ImGuiKey_F10);
    assert(manager.saveBindings(file.string()));
    const auto canonical = readFile(file);
    auto restarted = Manager::getInstance();
    assert(restarted.loadBindingsWithLegacy(file.string(), legacy.string()));
    assert(restarted.commandForKey("F10") == "TOGGLECHARACTER0");
    assert(restarted.commandForKey("F9").empty());
    assert(restarted.bindings().at("TOGGLEQUESTLOG")[0].empty());
    // Default, unsaved edit, Cancel, and repeated Load never import again.
    restarted.resetToDefaults();
    assert(restarted.commandForKey("C") == "TOGGLECHARACTER0");
    restarted.setKeyForAction(Manager::Action::TOGGLE_CHARACTER_SCREEN, ImGuiKey_F11);
    assert(restarted.loadBindingsWithLegacy(file.string(), legacy.string()));
    assert(restarted.commandForKey("F10") == "TOGGLECHARACTER0");
    assert(restarted.loadBindingsWithLegacy(file.string(), legacy.string()));
    assert(readFile(file) == canonical);

    // Block the temporary file: existing legacy data must remain byte-for-byte.
    std::ofstream(file, std::ios::binary) << stale;
    fs::create_directory(file.string() + ".tmp");
    auto failed = Manager::getInstance();
    const auto legacyBytes = readFile(legacy);
    assert(!failed.loadBindingsWithLegacy(file.string(), legacy.string()));
    assert(failed.commandForKey("F9") == "TOGGLECHARACTER0");
    assert(readFile(file) == stale);
    assert(readFile(legacy) == legacyBytes);
    // Even without a disk marker, normal reset/load must not reimport in-session.
    failed.resetToDefaults();
    assert(failed.loadBindingsWithLegacy(file.string(), legacy.string()));
    assert(failed.commandForKey("F9").empty());
    fs::remove(file.string() + ".tmp");
    failed.setKeyForAction(Manager::Action::TOGGLE_CHARACTER_SCREEN, ImGuiKey_F12);
    assert(failed.saveBindings(file.string()));
    auto recovered = Manager::getInstance();
    assert(recovered.loadBindingsWithLegacy(file.string(), legacy.string()));
    assert(recovered.commandForKey("F12") == "TOGGLECHARACTER0");
    assert(recovered.commandForKey("F9").empty());
    fs::remove_all(root);
}
