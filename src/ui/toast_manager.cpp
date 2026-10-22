#include "ui/toast_manager.hpp"
#include "rendering/renderer.hpp"
#include "rendering/animation_controller.hpp"
#include "audio/audio_coordinator.hpp"
#include "audio/ui_sound_manager.hpp"

namespace wowee { namespace ui {

// The "Level N!" banner is a frame of the interface's own, raised from
// PLAYER_LEVEL_UP (lua_engine.cpp), and the level-up sound is played where the
// level is read. What is left here is the character's cheer.
void ToastManager::triggerDing(uint32_t /*newLevel*/) {
    if (auto* renderer = services_.renderer) {
        if (auto* ac = renderer->getAnimationController()) ac->playEmote("cheer");
    }
}

void ToastManager::playAchievementEarned() {
    // alertframes.lua registers ACHIEVEMENT_EARNED and raises the badge, so
    // there is nothing of ours left to put on screen. The sound stays:
    // FrameXML's alert frame plays one only for LFG rewards, and an
    // achievement earned in silence is worse than one announced twice.
    auto* ac = services_.audioCoordinator;
    if (ac) {
        if (auto* sfx = ac->getUiSoundManager()) {
            sfx->playAchievementAlert();
        }
    }
}

} } // namespace wowee::ui
