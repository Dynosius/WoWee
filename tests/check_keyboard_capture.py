#!/usr/bin/env python3
"""Run: python tests/check_keyboard_capture.py (g++, SDL3 and ImGui required).

IMGUI_DIR defaults to extern/imgui; IMGUI_LIB may name a matching prebuilt
archive. CXX, SDL3_CFLAGS and SDL3_LIBS (default -lSDL3) can override the toolchain.
Compiles production application capture/probe and Lua frame dispatch bodies,
linking the real binding manager, widget tree and Escape resolver. Only the
Lua callback/ownership shell and held-key snapshot are doubles; no game assets
or window are needed. This does not execute FrameXML's dismissal Lua itself.
"""
import os
import shlex
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def section(path, start, end):
    source = (ROOT / path).read_text(encoding="utf-8")
    at = source.index(start)
    return source[at:source.index(end, at)]


def main():
    methods = section("src/addons/lua_engine.cpp", "static std::string wowKeyName(",
                      "std::string LuaEngine::bindingCommandFor(")
    methods += section("src/addons/lua_engine.cpp", "const ui::Widget* LuaEngine::topKeyboardFrame() const",
                       "void LuaEngine::dispatchFrameChar(")
    methods += section("src/addons/lua_engine.cpp", "bool LuaEngine::dispatchFrameKey(",
                       "void LuaEngine::dispatchKey(")
    probe = section("src/core/application.cpp", "ui::setTypedInputProbe(", "addons::LuaServices luaSvc;")
    # This region is empty before the fix, allowing a runtime fail-before test.
    capture = section("src/core/application.cpp", "Input::getInstance().update();",
                      "if (ui::frameXmlOwnsMouse())").split("Input::getInstance().update();", 1)[1]
    harness = r'''
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
@METHODS@
}
namespace ui = wowee::ui;
struct AddonManager {
    wowee::addons::LuaEngine engine;
    auto* getLuaEngine() { return &engine; }
};
struct Application {
    AddonManager* addonManager_;
    bool addonsLoaded_ = true;
    void configure() { @PROBE@ }
    void capture() {
#define SDL_GetKeyboardState heldKeyboardState
@CAPTURE@
#undef SDL_GetKeyboardState
    }
};
int main(int argc, char** argv) {
    check(argc == 2 && SDL_Init(SDL_INIT_EVENTS), "SDL init failed");
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    AddonManager addons;
    Application app{&addons};
    app.configure();
    auto& engine = addons.engine;
    auto& manager = ui::KeybindingManager::getInstance();
    manager.bindings().clear();
    manager.bindings()["SHORTCUT"] = {"C", ""};
    manager.bindings()["MOVEFORWARD"] = {"W", ""};
    manager.bindings()["TOGGLEGAMEMENU"] = {"ESCAPE", ""};
    const std::string scenario = argv[1];
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
    ImGui::DestroyContext(); SDL_Quit();
}
'''.replace("@METHODS@", methods).replace("@PROBE@", probe).replace("@CAPTURE@", capture)
    imgui = Path(os.environ.get("IMGUI_DIR", ROOT / "extern/imgui"))
    imgui_lib = os.environ.get("IMGUI_LIB")
    if not (imgui / "imgui.h").is_file():
        raise SystemExit("Set IMGUI_DIR to the populated ImGui submodule (and optionally IMGUI_LIB).")
    with tempfile.TemporaryDirectory(prefix="wowee-keyboard-capture-") as tmp:
        tmp = Path(tmp)
        source = tmp / "capture.cpp"
        source.write_text(harness, encoding="utf-8")
        binary = tmp / "capture.exe"
        command = shlex.split(os.environ.get("CXX", "g++")) + ["-std=c++20", "-O0", "-I" + str(ROOT / "include"), "-I" + str(imgui)]
        command += shlex.split(os.environ.get("SDL3_CFLAGS", ""))
        command += [str(source), str(ROOT / "src/ui/keybinding_manager.cpp"), str(ROOT / "src/ui/widget_tree.cpp"),
                    str(ROOT / "src/ui/escape_action.cpp"), str(ROOT / "src/core/logger.cpp")]
        command += [imgui_lib] if imgui_lib else [str(imgui / name) for name in ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp")]
        command += shlex.split(os.environ.get("SDL3_LIBS", "-lSDL3")) + ["-o", str(binary)]
        subprocess.run(command, check=True, cwd=tmp)
        failed = []
        for scenario in ("propagate", "consume", "script_propagate", "script_consume", "hidden", "disabled",
                         "pad_map", "pad_popup", "typing", "imgui_typing", "held", "already_held",
                         "layout_capture", "layout_dialog"):
            result = subprocess.run([str(binary), scenario], cwd=tmp, capture_output=True, text=True)
            print(f"{scenario}: {'PASS' if result.returncode == 0 else 'FAIL'}", flush=True)
            if result.returncode:
                print(result.stdout + result.stderr, end="")
                failed.append(scenario)
        if failed:
            raise SystemExit("Failed: " + ", ".join(failed))


if __name__ == "__main__":
    main()
