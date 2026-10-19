#!/usr/bin/env python3
"""Generate the camera's real input/speed phase for focused native tests.

The renderer/terrain/taxi/physics phases are deliberately not linked. The real
CameraController declaration, Input, KeybindingManager, ImGui and Camera are
used; no binding, modifier, walking or angle-reset logic is duplicated here.
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
source = (root / "src/rendering/camera_controller.cpp").read_text(encoding="utf-8")


def section(start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


output = r'''
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
'''
output += section("    auto& input = core::Input::getInstance();", "    // Get camera axes")
output += '''
    camera_input_test::speed = speed;
    camera_input_test::moving = hasMoveInput;
}
'''
output += section("void CameraController::startIntroPan(", "float CameraController::raymarchTerrainCameraLimit(")
output += section("void CameraController::resetAngles()", "void CameraController::reset()")
output += section("void CameraController::applyLookDelta(", "bool CameraController::isMoving()")
output += section("bool CameraController::isSprinting()", "void CameraController::triggerMountJump()")
output += "\n} // namespace wowee::rendering\n"

# Also test the real Lua/native dispatch gate without linking the rest of the
# action API (game handlers, action bars and addon services).
action_api = (root / "src/addons/lua_action_api.cpp").read_text(encoding="utf-8")
begin = action_api.index("bool clientActsOnBinding(")
end = action_api.index("// GetBindingKey(command)", begin)
output += '\n#include "addons/lua_api_registrations.hpp"\nnamespace wowee::addons {\n'
output += action_api[begin:end]
output += "\n} // namespace wowee::addons\n"
Path(sys.argv[1]).write_text(output, encoding="utf-8")
