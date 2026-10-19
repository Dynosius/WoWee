#include "camera_input_seam.hpp"

int main() {
    using namespace camera_input_test;
    using namespace wowee;
    assert(SDL_Init(SDL_INIT_EVENTS));
    ImGui::CreateContext();
    auto& bindings = ui::KeybindingManager::getInstance();
    bool typing = false;
    ui::setTypedInputProbe([&] { return typing; });
    rendering::Camera camera;
    rendering::CameraController controller(&camera);
    controller.setIdleOrbitEnabled(false);
    controller.setUseWoWSpeed(true);

    key(SDL_SCANCODE_W, true);
    frame(controller);
    expectSpeed(7.0f);
    assert(moving && controller.isSprinting());
    for (const auto ctrl : {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL}) {
        key(ctrl, true, SDL_KMOD_CTRL);
        frame(controller);
        expectSpeed(2.5f);
        assert(moving && !controller.isSprinting());
        frame(controller); // Holding Ctrl must not toggle or restore run.
        expectSpeed(2.5f);
        key(ctrl, false);
        frame(controller);
        expectSpeed(7.0f);
        assert(controller.isSprinting());
    }

    // Toggle remains bindable, edge-triggered, and independent of Ctrl.
    key(SDL_SCANCODE_KP_DIVIDE, true);
    frame(controller);
    expectSpeed(2.5f);
    frame(controller);
    expectSpeed(2.5f);
    key(SDL_SCANCODE_KP_DIVIDE, true, SDL_KMOD_NONE, true);
    frame(controller); // OS key repeat is not another toggle.
    expectSpeed(2.5f);
    key(SDL_SCANCODE_KP_DIVIDE, false);
    key(SDL_SCANCODE_LCTRL, true, SDL_KMOD_CTRL);
    frame(controller);
    expectSpeed(2.5f);
    key(SDL_SCANCODE_LCTRL, false);
    frame(controller);
    expectSpeed(2.5f); // Release restores the persistent walk, not run.

    assert(bindings.setBinding("NUMPADDIVIDE"));
    assert(bindings.setBinding("F8", "TOGGLERUN"));
    assert(bindings.setBinding("CTRL-F8", "TOGGLERUN"));
    key(SDL_SCANCODE_KP_DIVIDE, true);
    frame(controller);
    expectSpeed(2.5f); // Old key is dead after rebinding.
    key(SDL_SCANCODE_KP_DIVIDE, false);
    key(SDL_SCANCODE_LCTRL, true, SDL_KMOD_CTRL);
    key(SDL_SCANCODE_F8, true, SDL_KMOD_CTRL);
    frame(controller); // Toggle to run while Ctrl still forces walking.
    expectSpeed(2.5f);
    key(SDL_SCANCODE_F8, false, SDL_KMOD_CTRL);
    key(SDL_SCANCODE_LCTRL, false);
    frame(controller);
    expectSpeed(7.0f);
    assert(controller.isSprinting());

    // Both typing paths suppress Ctrl, movement and toggle edges. Holding a
    // key across leaving chat must not manufacture a delayed toggle.
    for (const bool imgui : {false, true}) {
        typing = !imgui;
        ImGui::GetIO().WantTextInput = imgui;
        key(SDL_SCANCODE_LCTRL, true, SDL_KMOD_CTRL);
        key(SDL_SCANCODE_F8, true, SDL_KMOD_CTRL);
        frame(controller);
        expectSpeed(7.0f);
        assert(!moving && !controller.isSprinting());
        typing = ImGui::GetIO().WantTextInput = false;
        key(SDL_SCANCODE_LCTRL, false);
        frame(controller);
        expectSpeed(7.0f);
        key(SDL_SCANCODE_F8, false);
    }

    // An explicit Ctrl-W chord wins over ordinary forward movement; merely
    // using Ctrl to walk must not bypass the shared modifier-chord resolver.
    assert(bindings.setBinding("CTRL-W", "TESTCHORD"));
    key(SDL_SCANCODE_LCTRL, true, SDL_KMOD_CTRL);
    frame(controller);
    assert(!moving);
    assert(bindings.isCommandDown("TESTCHORD"));
    key(SDL_SCANCODE_LCTRL, false);
    frame(controller);
    assert(moving && controller.isSprinting());

    // Server overrides and backpedal precedence stay unchanged.
    controller.setRunSpeedOverride(10.0f);
    controller.setWalkSpeedOverride(3.0f);
    frame(controller);
    expectSpeed(10.0f);
    key(SDL_SCANCODE_RCTRL, true, SDL_KMOD_CTRL);
    frame(controller);
    expectSpeed(3.0f);
    key(SDL_SCANCODE_W, false, SDL_KMOD_CTRL);
    key(SDL_SCANCODE_S, true, SDL_KMOD_CTRL);
    controller.setRunBackSpeedOverride(4.0f);
    frame(controller);
    expectSpeed(4.0f);
    assert(moving && !controller.isSprinting());
    key(SDL_SCANCODE_S, false, SDL_KMOD_CTRL);
    key(SDL_SCANCODE_RCTRL, false);

    // Free-fly exploration retains its independent slow/sprint modifiers.
    controller.setUseWoWSpeed(false);
    controller.setMovementSpeed(50.0f);
    key(SDL_SCANCODE_LCTRL, true, SDL_KMOD_CTRL);
    frame(controller);
    expectSpeed(15.0f);
    key(SDL_SCANCODE_LSHIFT, true, SDL_KMOD_CTRL | SDL_KMOD_SHIFT);
    frame(controller);
    expectSpeed(45.0f);
    key(SDL_SCANCODE_LCTRL, false, SDL_KMOD_SHIFT);
    frame(controller);
    expectSpeed(150.0f);
    key(SDL_SCANCODE_LSHIFT, false);
    frame(controller);
    expectSpeed(50.0f);

    ui::setTypedInputProbe({});
    ImGui::DestroyContext();
    SDL_Quit();
}
