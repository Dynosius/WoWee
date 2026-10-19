#!/usr/bin/env python3
"""Run: python tests/check_game_screen_commands.py (g++ or CXX on PATH).

Compile the actual GameScreen button loops and text-focus guard against small
binding/game sinks, linking the real form filter, slot type and page arithmetic.
No window, assets, SDL, or full client build required. --measure prints idle
allocation counts without asserting the performance contract (for before/after).
This checks the gameplay polling seam, not physical input or server responses.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--measure", action="store_true")
    args = parser.parse_args()
    screen = (ROOT / "src/ui/game_screen.cpp").read_text(encoding="utf-8")
    anchor = '            if (bindings.isCommandPressed("WOWEETOGGLERECORDING")) toggleRecording();'
    start = screen.index(anchor) + len(anchor)
    end = screen.index('\n    }\n\n    // A link says it can be clicked.', start)
    # Keep the real enclosing guard as well as the loops; omit unrelated UI toggles.
    guard_start = screen.rindex('        if (!textFocus)', 0, start)
    guard = screen[guard_start:screen.index('\n', guard_start)]
    loops = guard + screen[start:end]
    handler = (ROOT / "include/game/game_handler.hpp").read_text(encoding="utf-8")
    slots_start = handler.index('static constexpr int SLOTS_PER_BAR')
    slots = handler[slots_start:handler.index(';', slots_start) + 1]
    source = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <set>
#include <string>
#include <vector>
#include "game/shapeshift_forms.hpp"
#include "game/spell_defines.hpp"
#include "ui/action_bar_panel.hpp"

bool countAllocations = false;
size_t allocations = 0;
void* operator new(size_t size) {
    if (countAllocations) ++allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }

namespace game = wowee::game;
using wowee::ui::ActionBarPanel;
std::vector<std::string> events;
namespace wowee::game {
struct GameHandler {
    SLOT_DECLARATION
    uint8_t classId = 11;
    std::set<uint32_t> known;
    int formReads = 0;
    uint64_t target = 0;
    std::array<ActionBarSlot, 6 * SLOTS_PER_BAR> bar{};
    uint8_t getPlayerClass() const { return classId; }
    const auto& getKnownSpells() { ++formReads; return known; }
    const auto& getActionBar() const { return bar; }
    bool hasTarget() const { return target != 0; }
    uint64_t getTargetGuid() const { return target; }
    void castSpell(uint32_t id, uint64_t guid = 0) {
        events.push_back("spell:" + std::to_string(id) + ":" + std::to_string(guid));
    }
    void useItemById(uint32_t id) { events.push_back("item:" + std::to_string(id)); }
    std::string getMacroText(uint32_t id) const { return "macro:" + std::to_string(id); }
};
}
struct Bindings {
    std::set<std::string> pressed;
    int queries = 0;
    std::array<const std::string*, 32> names{};
    bool isCommandPressed(const std::string& command, bool repeat = false) {
        assert(!repeat);
        names[queries++ % names.size()] = &command;
        return pressed.count(command) != 0;
    }
} bindings;
struct ChatPanel {
    void executeMacroText(game::GameHandler&, const std::string& text) { events.push_back(text); }
} chatPanel_;
game::GameHandler gameHandler;
ActionBarPanel actionBarPanel_;
void poll(bool textFocus = false) {
PRODUCTION_LOOPS
}
void reset() {
    bindings = {};
    gameHandler = {};
    actionBarPanel_ = {};
    events.clear();
}
void expect(std::initializer_list<std::string> expected) {
    assert(events == std::vector<std::string>(expected));
}
int main(int argc, char**) {
    // Measure only the loops: fixtures/sink setup and first-use statics are outside.
    for (uint8_t classId : {uint8_t{11}, uint8_t{8}}) {
        reset();
        gameHandler.classId = classId;
        for (const auto& form : game::allShapeshiftForms(11))
            gameHandler.known.insert(form.spellId);
        poll(); // Warm immutable command strings once.
        const auto firstNames = bindings.names;
        gameHandler.formReads = 0;
        allocations = 0;
        countAllocations = true;
        for (int i = 0; i < 100; ++i) poll();
        countAllocations = false;
        std::printf("idle class %u: %zu allocations, %d form reads / 100 frames\n",
                    unsigned(classId), allocations, gameHandler.formReads);
        std::fflush(stdout);
        if (argc == 1) {
            assert(gameHandler.formReads == 0);
            assert(allocations == 0);
            assert(events.empty());
            // Even short (SSO) command strings reuse distinct, stable objects.
            assert(bindings.queries == 101 * int(bindings.names.size()));
            assert(bindings.names == firstNames);
            for (size_t i = 0; i < firstNames.size(); ++i)
                for (size_t j = 0; j < i; ++j) assert(firstNames[i] != firstNames[j]);
        }
    }
    if (argc > 1) return 0;

    // Every form button uses the current filtered bar order, with no target arg.
    const auto allForms = game::allShapeshiftForms(11);
    for (size_t i = 0; i < allForms.size(); ++i) {
        reset();
        for (const auto& form : allForms) gameHandler.known.insert(form.spellId);
        gameHandler.target = 999;
        bindings.pressed = {"SHAPESHIFTBUTTON" + std::to_string(i + 1)};
        poll();
        expect({"spell:" + std::to_string(allForms[i].spellId) + ":0"});
        assert(gameHandler.formReads == 1);
    }
    reset();
    gameHandler.known = {allForms[0].spellId, allForms[2].spellId};
    bindings.pressed = {"SHAPESHIFTBUTTON2", "SHAPESHIFTBUTTON8"};
    poll();
    expect({"spell:" + std::to_string(allForms[2].spellId) + ":0"});
    assert(gameHandler.formReads == 1); // One snapshot even for simultaneous presses.
    gameHandler.known = {allForms[1].spellId, allForms[3].spellId};
    events.clear();
    poll();
    expect({"spell:" + std::to_string(allForms[3].spellId) + ":0"});
    assert(gameHandler.formReads == 2); // Learning/unlearning needs no invalidation.
    gameHandler.classId = 8;
    events.clear();
    poll();
    expect({}); // Class changes and out-of-range forms are safe too.
    assert(gameHandler.formReads == 3); // Even an empty result is built only once.

    // All form presses precede action slots; never stop after the first press.
    reset();
    gameHandler.known = {allForms[0].spellId, allForms[2].spellId};
    bindings.pressed = {"SHAPESHIFTBUTTON1", "SHAPESHIFTBUTTON2", "ACTIONBUTTON1"};
    gameHandler.bar[0] = {game::ActionBarSlot::SPELL, 123};
    gameHandler.target = 999;
    poll();
    expect({"spell:" + std::to_string(allForms[0].spellId) + ":0",
            "spell:" + std::to_string(allForms[2].spellId) + ":0", "spell:123:999"});
    assert(gameHandler.formReads == 1);

    // Every main page/button and bottom-left button, and bottom's tie priority.
    for (int page = 1; page <= 6; ++page) {
        for (int i = 0; i < game::GameHandler::SLOTS_PER_BAR; ++i) {
            reset();
            actionBarPanel_.setMainActionBarPage(page);
            gameHandler.bar[ActionBarPanel::actionSlotForPage(page, i)] = {game::ActionBarSlot::SPELL, 123};
            bindings.pressed = {"ACTIONBUTTON" + std::to_string(i + 1)};
            poll();
            expect({"spell:123:0"});
            assert(gameHandler.formReads == 0); // Action presses do not query forms.
            events.clear();
            gameHandler.bar[ActionBarPanel::actionSlotForPage(6, i)] = {game::ActionBarSlot::SPELL, 456};
            bindings.pressed.insert("MULTIACTIONBAR1BUTTON" + std::to_string(i + 1));
            gameHandler.target = 999;
            poll();
            expect({"spell:456:999"});
            events.clear();
            bindings.pressed.erase("ACTIONBUTTON" + std::to_string(i + 1));
            poll();
            expect({"spell:456:999"});
        }
    }
    reset();
    bindings.pressed = {"ACTIONBUTTON1"};
    gameHandler.bar[0] = {game::ActionBarSlot::SPELL, 123};
    gameHandler.bar[12] = {game::ActionBarSlot::SPELL, 456};
    poll();
    expect({"spell:123:0"});
    events.clear();
    actionBarPanel_.setMainActionBarPage(2);
    poll();
    expect({"spell:456:0"}); // Page changes are live without rebuilding names.

    reset();
    bindings.pressed = {"ACTIONBUTTON1", "ACTIONBUTTON2", "ACTIONBUTTON3", "ACTIONBUTTON4", "ACTIONBUTTON5"};
    gameHandler.bar[0] = {game::ActionBarSlot::SPELL, 100, 1.0f}; // Cooldown: no cast.
    gameHandler.bar[1] = {game::ActionBarSlot::ITEM, 200, 1.0f}; // Item path unchanged.
    gameHandler.bar[2] = {game::ActionBarSlot::MACRO, 300};
    gameHandler.bar[3] = {game::ActionBarSlot::ITEM, 0};
    poll(); // Includes an empty slot; only the item and macro should run, in order.
    expect({"item:200", "macro:300"});
    events.clear();
    bindings.pressed.insert("SHAPESHIFTBUTTON1");
    bindings.queries = 0;
    gameHandler.formReads = 0;
    poll(true);
    expect({});
    assert(bindings.queries == 0 && gameHandler.formReads == 0);
    std::puts("PASS: idle allocations, lazy/live forms, command names, targeting, priority, text focus and dispatch");
}
'''.replace("SLOT_DECLARATION", slots).replace("PRODUCTION_LOOPS", loops)
    with tempfile.TemporaryDirectory(prefix="wowee-game-screen-commands-") as tmp:
        path = Path(tmp) / "check.cpp"
        exe = Path(tmp) / "check.exe"
        path.write_text(source, encoding="utf-8")
        subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O0", "-Wall", "-Wextra",
                        "-Werror", "-I", str(ROOT / "include"), str(path),
                        str(ROOT / "src/game/shapeshift_forms.cpp"), "-o", str(exe)], check=True)
        subprocess.run([str(exe)] + (["--measure"] if args.measure else []), check=True)


if __name__ == "__main__":
    main()
