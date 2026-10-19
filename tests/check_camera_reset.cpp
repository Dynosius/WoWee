// cmake --build build --target check_camera_reset
// ctest --test-dir build -R '^camera_reset$' --output-on-failure
#include "camera_input_seam.hpp"
#include "addons/lua_api_registrations.hpp"
#include "ui/framexml_emitter.hpp"
#include "ui/xml_parser.hpp"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <chrono>
#include <filesystem>

int main() {
    using namespace camera_input_test;
    using namespace wowee;
    assert(SDL_Init(SDL_INIT_EVENTS));
    ImGui::CreateContext();
    auto& bindings = ui::KeybindingManager::getInstance();
    const std::string command = "WOWEERESETCAMERA";
    assert(bindings.commandForKey("R") == command);

    // Execute the real catalogue emitted beside the camera zoom commands.
    ui::XmlNode root;
    std::string error;
    assert(ui::parseXml("<Bindings><Binding name='CAMERAZOOMIN' header='CAMERA'/>"
                        "<Binding name='CAMERAZOOMOUT'/></Bindings>", root, error));
    const auto emitted = ui::emitFrameXml(root);
    assert(emitted.warnings.empty());
    lua_State* L = luaL_newstate();
    assert(luaL_dostring(L, emitted.lua.c_str()) == 0);
    lua_getglobal(L, "__WoweeBindings");
    assert(lua_objlen(L, -1) == 4);
    lua_rawgeti(L, -1, 4);
    assert(std::string(lua_tostring(L, -1)) == command);
    lua_settop(L, 0);
    lua_getglobal(L, ("BINDING_NAME_" + command).c_str());
    assert(std::string(lua_tostring(L, -1)) == "Reset Camera Angles");
    lua_getglobal(L, "__WoweeBindingScripts");
    lua_getfield(L, -1, command.c_str());
    assert(lua_isfunction(L, -1));
    lua_pushstring(L, "down");
    assert(lua_pcall(L, 1, 0, 0) == 0);
    assert(addons::clientActsOnBinding(command)); // Native, not Lua-dispatched.
    assert(!addons::clientActsOnBinding("UNKNOWN_CAMERA_COMMAND"));

    rendering::Camera camera;
    rendering::CameraController controller(&camera);
    controller.setIdleOrbitEnabled(false);
    controller.setDefaultSpawn({-100.0f, -200.0f, -300.0f}, 37.0f, -12.0f);
    camera.setPosition({10.0f, 20.0f, 30.0f});
    glm::vec3 target{40.0f, 50.0f, 60.0f};
    controller.setFollowTarget(&target);
    controller.processMouseWheel(100.0f); // Preserve the first-person zoom.
    controller.setGrounded(false);
    controller.setSitting(true);
    key(SDL_SCANCODE_NUMLOCKCLEAR, true);
    key(SDL_SCANCODE_NUMLOCKCLEAR, false);
    frame(controller);
    assert(controller.isAutoRunning());

    rendering::Camera expectedCamera;
    expectedCamera.setRotation(37.0f, -12.0f);
    const auto unchanged = [&] {
        assert(camera.getPosition() == glm::vec3(10.0f, 20.0f, 30.0f));
        assert(target == glm::vec3(40.0f, 50.0f, 60.0f));
        assert(controller.getFollowTarget() == &target);
        assert(controller.isFalling() && !controller.isGrounded());
        assert(controller.isSitting() && controller.isAutoRunning());
        assert(controller.isFirstPersonView());
    };
    const auto expectReset = [&] {
        assert(controller.getYaw() == 37.0f);
        assert(controller.getFacingYaw() == 37.0f);
        assert(controller.getPitch() == -12.0f);
        assert(glm::length(camera.getForward() - expectedCamera.getForward()) < 0.0001f);
        unchanged(); // Angle-only reset, never the full position/physics reset.
    };
    const auto disturb = [&] {
        controller.applyLookDelta(70.0f, 30.0f);
        controller.setFacingYaw(-75.0f);
        assert(controller.getYaw() != 37.0f && controller.getPitch() != -12.0f);
    };
    const auto expectNotReset = [&] {
        assert(controller.getYaw() != 37.0f && controller.getPitch() != -12.0f);
        assert(controller.getFacingYaw() == -75.0f);
        unchanged();
    };
    const auto tap = [&](SDL_Scancode scan, SDL_Keymod mods = SDL_KMOD_NONE) {
        key(scan, true, mods);
        key(scan, false, mods);
        frame(controller);
    };

    disturb();
    tap(SDL_SCANCODE_R); // Even a down/up in one pump is a press.
    expectReset();
    disturb();
    key(SDL_SCANCODE_R, true);
    frame(controller);
    expectReset();
    disturb();
    frame(controller); // Held key does not keep resetting the view.
    expectNotReset();
    key(SDL_SCANCODE_R, true, SDL_KMOD_NONE, true);
    frame(controller); // Neither does OS repeat.
    expectNotReset();
    key(SDL_SCANCODE_R, false);
    frame(controller);

    bool typing = false;
    ui::setTypedInputProbe([&] { return typing; });
    for (const bool imgui : {false, true}) {
        typing = !imgui;
        ImGui::GetIO().WantTextInput = imgui;
        key(SDL_SCANCODE_R, true);
        frame(controller);
        expectNotReset();
        typing = ImGui::GetIO().WantTextInput = false;
        frame(controller); // No delayed reset when leaving chat with R held.
        expectNotReset();
        key(SDL_SCANCODE_R, false);
        frame(controller);
    }

    // An explicit modifier chord must win, even though plain R is the default.
    assert(bindings.setBinding("CTRL-R", "OTHER_COMMAND"));
    key(SDL_SCANCODE_LCTRL, true, SDL_KMOD_CTRL);
    tap(SDL_SCANCODE_R, SDL_KMOD_CTRL);
    expectNotReset();
    key(SDL_SCANCODE_LCTRL, false);
    frame(controller);
    tap(SDL_SCANCODE_R);
    expectReset();

    // Model the panel's rebind: clear the old key, then assign both slots.
    assert(bindings.setBinding("R"));
    assert(bindings.setBinding("CTRL-F8", command));
    assert(bindings.setBinding("ALT-F9", command));
    const auto path = std::filesystem::temp_directory_path() /
        ("wowee-camera-reset-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cfg");
    const auto edited = bindings.bindings();
    assert(bindings.saveBindings(path.string()));
    bindings.resetToDefaults();
    assert(bindings.loadBindings(path.string()));
    assert(bindings.bindings() == edited);
    disturb();
    tap(SDL_SCANCODE_R);
    expectNotReset(); // No hidden hardcoded R after rebinding and reloading.
    key(SDL_SCANCODE_LCTRL, true, SDL_KMOD_CTRL);
    tap(SDL_SCANCODE_F8, SDL_KMOD_CTRL);
    expectReset();
    key(SDL_SCANCODE_LCTRL, false);
    frame(controller);
    disturb();
    tap(SDL_SCANCODE_F8); // Qualified binding rejects an unmodified press.
    expectNotReset();
    key(SDL_SCANCODE_LALT, true, SDL_KMOD_ALT);
    tap(SDL_SCANCODE_F9, SDL_KMOD_ALT);
    expectReset();
    key(SDL_SCANCODE_LALT, false);
    frame(controller);

    assert(bindings.setBinding("CTRL-F8"));
    assert(bindings.setBinding("ALT-F9"));
    assert(bindings.saveBindings(path.string()));
    bindings.resetToDefaults();
    assert(bindings.loadBindings(path.string()));
    disturb();
    tap(SDL_SCANCODE_R);
    tap(SDL_SCANCODE_F8, SDL_KMOD_CTRL);
    tap(SDL_SCANCODE_F9, SDL_KMOD_ALT);
    expectNotReset(); // Persisted unbinds do not resurrect the R default.

    std::filesystem::remove(path);
    ui::setTypedInputProbe({});
    lua_close(L);
    ImGui::DestroyContext();
    SDL_Quit();
}
