#include "ui/toast_manager.hpp"
#include "game/game_handler.hpp"
#include "core/application.hpp"
#include "rendering/renderer.hpp"
#include "rendering/animation_controller.hpp"
#include "audio/audio_coordinator.hpp"
#include "audio/ui_sound_manager.hpp"
#include "ui/framexml_takeover.hpp"

#include <imgui.h>
#include <algorithm>
#include <cstdio>

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

void ToastManager::renderZoneText(game::GameHandler& gameHandler) {
    // FrameXML's ZoneTextFrame is the banner whenever the interface loaded;
    // this is the one drawn when it did not.
    if (frameXmlOwns(UiElement::ZoneText)) return;

    // Poll worldStateZoneId for server-driven zone changes (fires on every zone crossing,
    // including sub-zones like Ironforge within Dun Morogh).
    uint32_t wsZoneId = gameHandler.getWorldStateZoneId();
    if (wsZoneId != 0 && wsZoneId != lastKnownWorldStateZoneId_) {
        lastKnownWorldStateZoneId_ = wsZoneId;
        std::string wsName = gameHandler.getWhoAreaName(wsZoneId);
        if (!wsName.empty()) {
            zoneTextName_  = wsName;
            zoneTextTimer_ = ZONE_TEXT_DURATION;
        }
    }

    // Also poll the renderer for zone name changes (covers map-level transitions
    // where worldStateZoneId may not change immediately).
    auto* appRenderer = services_.renderer;
    if (appRenderer) {
        const std::string& zoneName = appRenderer->getCurrentZoneName();
        if (!zoneName.empty() && zoneName != lastKnownZoneName_) {
            lastKnownZoneName_ = zoneName;
            // Only override if the worldState hasn't already queued this zone
            if (zoneTextName_ != zoneName) {
                zoneTextName_  = zoneName;
                zoneTextTimer_ = ZONE_TEXT_DURATION;
            }
        }
    }

    if (zoneTextTimer_ <= 0.0f || zoneTextName_.empty()) return;

    float dt = ImGui::GetIO().DeltaTime;
    zoneTextTimer_ -= dt;
    if (zoneTextTimer_ < 0.0f) zoneTextTimer_ = 0.0f;


    auto* window = services_.window;
    float screenW = window ? static_cast<float>(window->getWidth())  : 1280.0f;
    float screenH = window ? static_cast<float>(window->getHeight()) :  720.0f;

    // Fade: ramp up in first 0.5 s, hold, fade out in last 1.0 s
    float alpha;
    if (zoneTextTimer_ > ZONE_TEXT_DURATION - 0.5f)
        alpha = 1.0f - (zoneTextTimer_ - (ZONE_TEXT_DURATION - 0.5f)) / 0.5f;
    else if (zoneTextTimer_ < 1.0f)
        alpha = zoneTextTimer_;
    else
        alpha = 1.0f;
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    ImFont* font = ImGui::GetFont();

    // "Entering:" header
    const char* header = "Entering:";
    float headerSize = 16.0f;
    float nameSize   = 26.0f;

    ImVec2 headerDim = font->CalcTextSizeA(headerSize, FLT_MAX, 0.0f, header);
    ImVec2 nameDim   = font->CalcTextSizeA(nameSize,   FLT_MAX, 0.0f, zoneTextName_.c_str());

    float centreY = screenH * 0.30f;  // upper third, like WoW
    float headerX = (screenW - headerDim.x) * 0.5f;
    float nameX   = (screenW - nameDim.x)   * 0.5f;
    float headerY = centreY;
    float nameY   = centreY + headerDim.y + 4.0f;

    ImDrawList* draw = ImGui::GetForegroundDrawList();

    // "Entering:" in gold
    draw->AddText(font, headerSize, ImVec2(headerX + 1, headerY + 1),
                  IM_COL32(0, 0, 0, static_cast<int>(alpha * 160)), header);
    draw->AddText(font, headerSize, ImVec2(headerX, headerY),
                  IM_COL32(255, 215, 0, static_cast<int>(alpha * 255)), header);

    // Zone name in white
    draw->AddText(font, nameSize, ImVec2(nameX + 1, nameY + 1),
                  IM_COL32(0, 0, 0, static_cast<int>(alpha * 160)), zoneTextName_.c_str());
    draw->AddText(font, nameSize, ImVec2(nameX, nameY),
                  IM_COL32(255, 255, 255, static_cast<int>(alpha * 255)), zoneTextName_.c_str());
}

} } // namespace wowee::ui
