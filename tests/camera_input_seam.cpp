#include "camera_input_seam.hpp"
#include "core/coordinates.hpp"
#include "core/logger.hpp"
#include <cmath>

namespace camera_input_test {
float speed = 0.0f;
bool moving = false;
}
namespace wowee::rendering {
// Test-only construction: leave the header's initialized state intact rather
// than invoking reset(), which also initializes terrain/physics/render state.
CameraController::CameraController(Camera* cam) : camera(cam) {}
void CameraController::update(float deltaTime) {
    auto& input = core::Input::getInstance();

    // Don't process keyboard input when UI text input (e.g. chat box) has focus.
    // Both interfaces are asked: the flag below is ImGui's own and says nothing
    // about a chat box FrameXML draws, so on its own it let the character walk
    // away while someone was typing.
    bool uiWantsKeyboard = ImGui::GetIO().WantTextInput ||
                           ui::interfaceTakingTypedInput();

    // Suppress movement input after teleport/portal (keys may still be held)
    if (movementSuppressTimer_ > 0.0f) {
        movementSuppressTimer_ -= deltaTime;
    }
    bool movementSuppressed = movementSuppressTimer_ > 0.0f;

    auto& bindings = ui::KeybindingManager::getInstance();
    if (bindings.isCommandPressed("TOGGLERUN")) walking_ = !walking_;
    if (bindings.isCommandPressed("CAMERAZOOMIN")) processMouseWheel(1.0f);
    if (bindings.isCommandPressed("CAMERAZOOMOUT")) processMouseWheel(-1.0f);
    if (bindings.isCommandPressed("WOWEERESETCAMERA")) resetAngles();

    // Determine current key states
    // Arrow keys mirror WASD the way the original client maps them:
    // Up/Down move, Left/Right turn (strafe stays on Q/E).
    bool keyW = !uiWantsKeyboard && !sitting && !movementSuppressed && bindings.isCommandDown("MOVEFORWARD");
    bool keyS = !uiWantsKeyboard && !sitting && !movementSuppressed && bindings.isCommandDown("MOVEBACKWARD");
    bool keyA = !uiWantsKeyboard && !sitting && !movementSuppressed && bindings.isCommandDown("TURNLEFT");
    bool keyD = !uiWantsKeyboard && !sitting && !movementSuppressed && bindings.isCommandDown("TURNRIGHT");
    bool keyQ = !uiWantsKeyboard && !sitting && !movementSuppressed && bindings.isCommandDown("STRAFELEFT");
    bool keyE = !uiWantsKeyboard && !sitting && !movementSuppressed && bindings.isCommandDown("STRAFERIGHT");
    bool shiftDown = !uiWantsKeyboard && (input.isKeyPressed(SDL_SCANCODE_LSHIFT) || input.isKeyPressed(SDL_SCANCODE_RSHIFT));
    bool ctrlDown = !uiWantsKeyboard && (input.isKeyPressed(SDL_SCANCODE_LCTRL) || input.isKeyPressed(SDL_SCANCODE_RCTRL));
    // Ctrl temporarily forces walking without changing the bindable toggle.
    const bool walking = walking_ || ctrlDown;
    bool nowJump = !uiWantsKeyboard && !sitting && !movementSuppressed && bindings.isCommandPressed("JUMP");
    // Swimming and flying need the held state, not the press edge: on land
    // space is a one-shot jump, but in water and in the air it is continuous
    // ascent, and an edge gave a single impulse that then bled away - or, in
    // flight, a single frame of climb per press.
    bool spaceHeld = !uiWantsKeyboard && !sitting && !movementSuppressed && bindings.isCommandDown("JUMP");

    // Idle camera: any input resets the timer; timeout triggers a slow orbit pan
    bool anyInput = leftMouseDown || rightMouseDown || keyW || keyS || keyA || keyD || keyQ || keyE || nowJump;
    if (anyInput) {
        idleTimer_ = 0.0f;
    } else if (!introActive && idleOrbitEnabled_) {
        idleTimer_ += deltaTime;
        if (idleTimer_ >= IDLE_TIMEOUT) {
            idleTimer_ = 0.0f;
            startIntroPan(30.0f, 360.0f); // Slow casual orbit over 30 seconds
            idleOrbit_ = true;
        }
    } else if (!idleOrbitEnabled_) {
        idleTimer_ = 0.0f;
    }

    if (introActive) {
        if (anyInput) {
            introActive = false;
            idleOrbit_ = false;
            idleTimer_ = 0.0f;
        } else {
            introTimer += deltaTime;
            if (idleOrbit_) {
                // Continuous smooth rotation - no lerp endpoint, just constant angular velocity
                float degreesPerSec = introOrbitDegrees / introDuration;
                yaw -= degreesPerSec * deltaTime;
                camera->setRotation(yaw, pitch);
                facingYaw = yaw;
            } else {
                float t = (introDuration > 0.0f) ? std::min(introTimer / introDuration, 1.0f) : 1.0f;
                yaw = introStartYaw + (introEndYaw - introStartYaw) * t;
                pitch = introStartPitch + (introEndPitch - introStartPitch) * t;
                currentDistance = introStartDistance + (introEndDistance - introStartDistance) * t;
                userTargetDistance = introEndDistance;
                camera->setRotation(yaw, pitch);
                facingYaw = yaw;
                if (t >= 1.0f) {
                    introActive = false;
                }
            }
        }
        // Suppress player movement/input during intro.
        keyW = keyS = keyA = keyD = keyQ = keyE = nowJump = spaceHeld = false;
    }

    // The same action the keybinding panel rebinds; forward/backward cancels it.
    if (!uiWantsKeyboard && ui::KeybindingManager::getInstance().isActionPressed(
            ui::KeybindingManager::Action::TOGGLE_AUTORUN)) {
        autoRunning = !autoRunning;
    }
    // Helper: cancel auto-follow and notify game handler
    auto doCancelAutoFollow = [&]() {
        if (autoFollowTarget_) {
            autoFollowTarget_ = nullptr;
            if (autoFollowCancelCallback_) autoFollowCancelCallback_();
        }
    };

    if (keyW || keyS) {
        autoRunning = false;
        doCancelAutoFollow();
    }

    bool mouseAutorun = !uiWantsKeyboard && !sitting && leftMouseDown && rightMouseDown;
    if (mouseAutorun) {
        autoRunning = false;
        doCancelAutoFollow();
    }

    // Auto-follow: face target and run toward them when within range
    bool autoFollowMove = false;
    if (autoFollowTarget_ && followTarget && !movementRooted_) {
        glm::vec3 myPos = *followTarget;
        glm::vec3 tgtPos = *autoFollowTarget_;
        float dx = tgtPos.x - myPos.x;
        float dy = tgtPos.y - myPos.y;
        float distSq2D = dx * dx + dy * dy;

        if (distSq2D > FOLLOW_MAX_DIST * FOLLOW_MAX_DIST) {
            doCancelAutoFollow();
        } else if (distSq2D > FOLLOW_STOP_DIST * FOLLOW_STOP_DIST) {
            // Face target (render-space yaw: atan2(-dx, -dy) -> degrees)
            float targetYawRad = std::atan2(-dx, -dy);
            float targetYawDeg = targetYawRad * 180.0f / core::coords::PI;
            facingYaw = targetYawDeg;
            yaw = targetYawDeg;
            autoFollowMove = true;
        }
        // else: within stop distance, stay put

        // Cancel on strafe/turn keys
        if (keyA || keyD || keyQ || keyE) {
            doCancelAutoFollow();
            autoFollowMove = false;
        }
    }

    // When the server has rooted the player, suppress all horizontal movement input.
    const bool movBlocked = movementRooted_;
    // Auto-follow uses run speed (same as auto-run), not walk speed
    if (autoFollowMove) autoRunning = true;
    bool nowForward = !movBlocked && (keyW || mouseAutorun || autoRunning);
    bool nowBackward = !movBlocked && keyS;
    bool nowStrafeLeft = false;
    bool nowStrafeRight = false;
    bool nowTurnLeft = false;
    bool nowTurnRight = false;

    // WoW-like third-person keyboard behavior:
    // - RMB held: A/D strafe
    // - RMB released: A/D turn character+camera, Q/E strafe
    // Turning is allowed even while rooted; only positional movement is blocked.
    if (thirdPerson && !rightMouseDown) {
        nowTurnLeft = keyA;
        nowTurnRight = keyD;
        nowStrafeLeft = !movBlocked && keyQ;
        nowStrafeRight = !movBlocked && keyE;
    } else {
        nowStrafeLeft = !movBlocked && (keyA || keyQ);
        nowStrafeRight = !movBlocked && (keyD || keyE);
    }

    // Keyboard turning updates camera yaw (character follows yaw in renderer).
    // Use server turn rate (rad/s) when set; otherwise fall back to WOW_TURN_SPEED (deg/s).
    const float activeTurnSpeedDeg = (turnRateOverride_ > 0.0f && turnRateOverride_ < 20.0f
                                       && !std::isnan(turnRateOverride_))
                                         ? glm::degrees(turnRateOverride_)
                                         : WOW_TURN_SPEED;
    if (nowTurnLeft && !nowTurnRight) {
        yaw += activeTurnSpeedDeg * deltaTime;
    } else if (nowTurnRight && !nowTurnLeft) {
        yaw -= activeTurnSpeedDeg * deltaTime;
    }
    if (nowTurnLeft || nowTurnRight) {
        camera->setRotation(yaw, pitch);
        facingYaw = yaw;
    }

    // Tick down gravity suspension timer (used after world entry to prevent
    // falling through WMO floors before collision is loaded)
    if (entryFloorAnchorTimer_ > 0.0f) entryFloorAnchorTimer_ -= deltaTime;
    if (gravitySuspendTimer_ > 0.0f) {
        gravitySuspendTimer_ -= deltaTime;
    }

    // Select physics constants based on mode
    float gravity = useWoWSpeed ? WOW_GRAVITY : GRAVITY;
    float jumpVel = useWoWSpeed ? WOW_JUMP_VELOCITY : JUMP_VELOCITY;

    // Suspend gravity after world entry - hold Z position until timer expires
    // OR a floor is detected. This prevents falling through unloaded WMO floors.
    if (gravitySuspendTimer_ > 0.0f) {
        gravity = 0.0f;
        verticalVelocity = 0.0f;
    }

    // Calculate movement speed based on direction and modifiers
    float speed;
    if (useWoWSpeed) {
        // Movement speeds (WoW-like: Ctrl walk, default run, backpedal slower)
        if (nowBackward && !nowForward) {
            speed = (runBackSpeedOverride_ > 0.0f && runBackSpeedOverride_ < 100.0f
                     && !std::isnan(runBackSpeedOverride_))
                        ? runBackSpeedOverride_ : WOW_BACK_SPEED;
        } else if (walking) {
            speed = (walkSpeedOverride_ > 0.0f && walkSpeedOverride_ < 100.0f && !std::isnan(walkSpeedOverride_))
                        ? walkSpeedOverride_ : WOW_WALK_SPEED;
        } else if (runSpeedOverride_ > 0.0f && runSpeedOverride_ < 100.0f && !std::isnan(runSpeedOverride_)) {
            speed = runSpeedOverride_;
        } else {
            speed = WOW_RUN_SPEED;
        }
    } else {
        // Exploration mode (original behavior)
        speed = movementSpeed;
        if (shiftDown) {
            speed *= sprintMultiplier;
        }
        if (ctrlDown) {
            speed *= slowMultiplier;
        }
    }

    bool hasMoveInput = nowForward || nowBackward || nowStrafeLeft || nowStrafeRight;
    if (useWoWSpeed) {
        // "Sprinting" flag drives run animation/stronger footstep set.
        // In WoW mode this means running pace (not walk/backpedal), not Shift.
        runPace = hasMoveInput && !walking && !nowBackward;
    } else {
        runPace = hasMoveInput && shiftDown;
    }

    camera_input_test::speed = speed;
    camera_input_test::moving = hasMoveInput;
}
void CameraController::startIntroPan(float durationSec, float orbitDegrees) {
    if (!camera) return;
    introActive = true;
    introTimer = 0.0f;
    idleTimer_ = 0.0f;
    introDuration = std::max(0.5f, durationSec);
    introStartYaw = yaw;
    introEndYaw = yaw - orbitDegrees;
    introOrbitDegrees = orbitDegrees;
    introStartPitch = pitch;
    introEndPitch = pitch;
    introStartDistance = currentDistance;
    introEndDistance = currentDistance;
    thirdPerson = true;
}
void CameraController::resetAngles() {
    if (!camera) return;
    yaw = defaultYaw;
    facingYaw = defaultYaw;
    pitch = defaultPitch;
    camera->setRotation(yaw, pitch);
}

void CameraController::applyLookDelta(float dxPixels, float dyPixels) {
    if (!enabled || !camera || introActive) return;

    yaw -= dxPixels * mouseSensitivity;
    const float invert = invertMouse ? 1.0f : -1.0f;
    pitch += dyPixels * mouseSensitivity * invert;
    pitch = glm::clamp(pitch, MIN_PITCH, MAX_PITCH);
    camera->setRotation(yaw, pitch);
}

void CameraController::processMouseWheel(float delta) {
    collisionZoomDebt_ = 0.0f;
    collisionClearSeconds_ = 0.0f;
    float zoomSpeed = glm::max(userTargetDistance * 0.15f, 0.3f);
    userTargetDistance -= delta * zoomSpeed;
    float maxDist = MAX_DISTANCE_NORMAL * maxDistanceFactor_;
    if (cachedInsideWMO) maxDist = std::min(maxDist, MAX_DISTANCE_INTERIOR);
    userTargetDistance = glm::clamp(userTargetDistance, MIN_DISTANCE, maxDist);
}

void CameraController::setFollowTarget(glm::vec3* target) {
    followTarget = target;
    if (target) {
        thirdPerson = true;
        LOG_INFO("Third-person camera enabled");
    } else {
        thirdPerson = false;
        LOG_INFO("Free-fly camera enabled");
    }
}

bool CameraController::isSprinting() const {
    return enabled && camera && runPace;
}

} // namespace wowee::rendering

#include "addons/lua_api_registrations.hpp"
namespace wowee::addons {
bool clientActsOnBinding(const std::string& command) {
    using Manager = wowee::ui::KeybindingManager;
    for (int i = 0; i < Manager::getActionCount(); ++i)
        if (command == Manager::commandForAction(static_cast<Manager::Action>(i))) return true;
    static constexpr const char* native[] = {
        "MOVEFORWARD", "MOVEBACKWARD", "TURNLEFT", "TURNRIGHT", "STRAFELEFT", "STRAFERIGHT",
        "JUMP", "SITORSTAND", "TOGGLERUN", "OPENCHATSLASH", "TARGETNEARESTENEMY",
        "SCREENSHOT", "WOWEETOGGLERECORDING", "TOGGLESHEATH", "FRIENDNAMEPLATES", "CAMERAZOOMIN", "CAMERAZOOMOUT",
        "WOWEERESETCAMERA",
    };
    for (const char* name : native) if (command == name) return true;
    for (const auto& [prefix, count] : {std::pair{"ACTIONBUTTON", 12},
             {"SHAPESHIFTBUTTON", 8}, {"MULTIACTIONBAR1BUTTON", 12}})
        for (int i = 1; i <= count; ++i)
            if (command == prefix + std::to_string(i)) return true;
    return false;
}
} // namespace wowee::addons
