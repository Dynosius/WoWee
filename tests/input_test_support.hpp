#pragma once

#include <catch_amalgamated.hpp>
#include "ui/keybinding_manager.hpp"
#include "core/env.hpp"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace input_test {
namespace fs = std::filesystem;
using Manager = wowee::ui::KeybindingManager;

// Keep the logger's destination alive for the whole executable: its stream is
// process-global, not a per-test resource. Never touch the player's files.
struct Environment {
    std::string name;
    std::optional<std::string> previous;
    Environment(std::string key, const std::string& value) : name(std::move(key)) {
        if (const char* old = std::getenv(name.c_str())) previous = old;
        set(value.c_str());
    }
    void set(const char* value) const {
        if (value) wowee::core::setEnvVar(name.c_str(), value);
        else wowee::core::unsetEnvVar(name.c_str());
    }
    ~Environment() { set(previous ? previous->c_str() : nullptr); }
};
struct Files {
    fs::path root = fs::temp_directory_path() / ("wowee-input-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Environment config{"WOWEE_CONFIG_ROOT", root.string()};
    Environment log{"WOWEE_LOG_FILE", (root / "input.log").string()};
    Environment stdoutLog{"WOWEE_LOG_STDOUT", "0"};
    Files() { fs::create_directories(root); }
    // Logs intentionally remain available for diagnosing a failed CTest run.
};
inline Files& files() { static Files instance; return instance; }
inline std::string contents(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
inline void writeFile(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    REQUIRE(static_cast<bool>(out << text));
}
inline SDL_Event key(SDL_Scancode scan, bool down = true,
                     SDL_Keymod mod = SDL_KMOD_NONE, bool repeat = false,
                     SDL_Keycode layout = 0) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scan;
    event.key.key = layout ? layout : SDL_SCANCODE_TO_KEYCODE(scan);
    event.key.mod = mod;
    event.key.down = down;
    event.key.repeat = repeat;
    return event;
}
struct Fixture {
    Files& storage = files();
    fs::path root = [this] {
        static unsigned sequence = 0;
        return storage.root / ("case-" + std::to_string(++sequence));
    }();
    Environment config{"WOWEE_CONFIG_ROOT", root.string()};
    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    Manager& manager = Manager::getInstance();
    Manager previous = manager;
    SDL_Keymod previousModifiers = SDL_GetModState();
    Fixture() {
        fs::create_directories(root);
        REQUIRE(SDL_InitSubSystem(SDL_INIT_EVENTS));
        ImGui::SetCurrentContext(context);
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().DisplaySize = ImVec2(800, 600);
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        manager.resetToDefaults();
        SDL_Event lost{}; lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        manager.processInputEvent(lost);
        lost.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        manager.processInputEvent(lost);
        manager.beginInputFrame();
        wowee::ui::setTypedInputProbe({});
        wowee::ui::clearInterfaceConsumedKeys();
        SDL_SetModState(SDL_KMOD_NONE);
    }
    ~Fixture() {
        wowee::ui::setTypedInputProbe({});
        wowee::ui::clearInterfaceConsumedKeys();
        manager = previous;
        SDL_SetModState(previousModifiers);
        ImGui::DestroyContext(context);
        ImGui::SetCurrentContext(previousContext);
        SDL_QuitSubSystem(SDL_INIT_EVENTS);
    }
    void event(SDL_Scancode scan, bool down = true, SDL_Keymod mod = SDL_KMOD_NONE,
               bool repeat = false, SDL_Keycode layout = 0) {
        manager.processInputEvent(key(scan, down, mod, repeat, layout));
    }
    fs::path path(const char* name) const { return root / name; }
};
} // namespace input_test
