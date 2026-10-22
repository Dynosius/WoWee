#include "game/reputation_standing.hpp"
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
#include <cmath>

namespace wowee { namespace ui {

// ---------------------------------------------------------------------------
// Setup toast callbacks on GameHandler (idempotent - safe to call every frame)
// ---------------------------------------------------------------------------
void ToastManager::setupCallbacks(game::GameHandler& gameHandler) {
    // NOTE: Level-up and achievement callbacks are registered by Application
    // (which also triggers the 3D level-up effect).  Application routes to
    // triggerDing() / triggerAchievementToast() via the public API.

    // Area discovery toast callback
    if (!areaDiscoveryCallbackSet_) {
        gameHandler.setAreaDiscoveryCallback([this](const std::string& areaName, uint32_t xpGained) {
            discoveryToastName_ = areaName.empty() ? "New Area" : areaName;
            discoveryToastXP_   = xpGained;
            discoveryToastTimer_ = DISCOVERY_TOAST_DURATION;
        });
        areaDiscoveryCallbackSet_ = true;
    }

    // Ghost-state callback to flash "You have been resurrected!" on revival
    if (!ghostStateCallbackSet_) {
        gameHandler.setGhostStateCallback([this](bool isGhost) {
            if (!isGhost) {
                resurrectFlashTimer_ = kResurrectFlashDuration;
            }
        });
        ghostStateCallbackSet_ = true;
    }

    // Quest completion toast callback
    if (!questCompleteCallbackSet_) {
        gameHandler.setQuestCompleteCallback([this](uint32_t id, const std::string& title) {
            questCompleteToasts_.push_back({.questId = id, .title = title, .age = 0.0f});
            if (questCompleteToasts_.size() > 3) questCompleteToasts_.erase(questCompleteToasts_.begin());
        });
        questCompleteCallbackSet_ = true;
    }
}

// ---------------------------------------------------------------------------
// Render early toasts (before action bars)
// ---------------------------------------------------------------------------
void ToastManager::renderEarlyToasts(float deltaTime, game::GameHandler& gameHandler) {
    // Zone entry detection - fire a toast when the renderer's zone name changes
    if (auto* rend = services_.renderer) {
        const std::string& curZone = rend->getCurrentZoneName();
        if (!curZone.empty() && curZone != lastKnownZone_) {
            if (!lastKnownZone_.empty()) {
                zoneToasts_.push_back({.zoneName = curZone, .age = 0.0f});
                if (zoneToasts_.size() > 3)
                    zoneToasts_.erase(zoneToasts_.begin());
            }
            lastKnownZone_ = curZone;
        }
    }

    renderQuestCompleteToasts(deltaTime);
    renderZoneToasts(deltaTime);
    renderAreaTriggerToasts(deltaTime, gameHandler);
}

// ---------------------------------------------------------------------------
// Render late toasts (after escape menu / settings)
// ---------------------------------------------------------------------------
void ToastManager::renderLateToasts(game::GameHandler& gameHandler) {
    renderDingEffect();
    // FrameXML's AlertFrame answers ACHIEVEMENT_EARNED, which this client
    // fires, and it is suppressed only while the achievements element is not
    // owned. So with it owned both popups appeared, one over the other, on
    // every achievement - the same shape the zone banner had before its own
    // gate went in a few lines below.
    renderDiscoveryToast();
    renderResurrectFlash();
    renderZoneText(gameHandler);
}

void ToastManager::renderQuestCompleteToasts(float deltaTime) {
    for (auto& e : questCompleteToasts_) e.age += deltaTime;
    questCompleteToasts_.erase(
        std::remove_if(questCompleteToasts_.begin(), questCompleteToasts_.end(),
            [](const QuestCompleteToastEntry& e) { return e.age >= kQuestCompleteToastLifetime; }),
        questCompleteToasts_.end());

    if (questCompleteToasts_.empty()) return;

    auto* window = services_.window;
    float screenW = window ? static_cast<float>(window->getWidth()) : 1280.0f;
    float screenH = window ? static_cast<float>(window->getHeight()) : 720.0f;

    const float toastW = 260.0f;
    const float toastH = 40.0f;
    const float padY   = 4.0f;
    const float baseY  = screenH - 220.0f;  // above rep toasts

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    float fontSize = ImGui::GetFontSize();

    for (int i = 0; i < static_cast<int>(questCompleteToasts_.size()); ++i) {
        const auto& e = questCompleteToasts_[i];
        constexpr float kSlideDur = 0.3f;
        float slideIn  = std::min(e.age, kSlideDur) / kSlideDur;
        float slideOut = std::min(std::max(0.0f, kQuestCompleteToastLifetime - e.age), kSlideDur) / kSlideDur;
        float slide    = std::min(slideIn, slideOut);
        float alpha    = std::clamp(slide, 0.0f, 1.0f);

        float xFull  = screenW - 14.0f - toastW;
        float xStart = screenW + 10.0f;
        float toastX = xStart + (xFull - xStart) * slide;
        float toastY = baseY - i * (toastH + padY);

        ImVec2 tl(toastX, toastY);
        ImVec2 br(toastX + toastW, toastY + toastH);

        // Background + gold border (quest completion)
        draw->AddRectFilled(tl, br, IM_COL32(20, 18, 8, static_cast<int>(alpha * 210)), 5.0f);
        draw->AddRect(tl, br, IM_COL32(220, 180, 30, static_cast<int>(alpha * 230)), 5.0f, 0, 1.5f);

        // Scroll icon placeholder (gold diamond)
        float iconCx = tl.x + 18.0f;
        float iconCy = tl.y + toastH * 0.5f;
        draw->AddCircleFilled(ImVec2(iconCx, iconCy), 7.0f, IM_COL32(210, 170, 20, static_cast<int>(alpha * 230)));
        draw->AddCircle      (ImVec2(iconCx, iconCy), 7.0f, IM_COL32(255, 220, 50, static_cast<int>(alpha * 200)));

        // "Quest Complete" header in gold
        const char* header = "Quest Complete";
        draw->AddText(font, fontSize * 0.78f,
                      ImVec2(tl.x + 34.0f, tl.y + 4.0f),
                      IM_COL32(240, 200, 40, static_cast<int>(alpha * 240)), header);

        // Quest title in off-white
        const char* titleStr = e.title.empty() ? "Unknown Quest" : e.title.c_str();
        draw->AddText(font, fontSize * 0.82f,
                      ImVec2(tl.x + 34.0f, tl.y + toastH * 0.5f + 1.0f),
                      IM_COL32(220, 215, 195, static_cast<int>(alpha * 220)), titleStr);
    }
}

// ============================================================
// Zone Entry Toast
// ============================================================

void ToastManager::renderZoneToasts(float deltaTime) {
    for (auto& e : zoneToasts_) e.age += deltaTime;
    zoneToasts_.erase(
        std::remove_if(zoneToasts_.begin(), zoneToasts_.end(),
            [](const ZoneToastEntry& e) { return e.age >= kZoneToastLifetime; }),
        zoneToasts_.end());

    // Suppress toasts while the zone text overlay is showing the same zone -
    // avoids duplicate "Entering: Stormwind City" messages.
    if (zoneTextTimer_ > 0.0f) {
        zoneToasts_.erase(
            std::remove_if(zoneToasts_.begin(), zoneToasts_.end(),
                [this](const ZoneToastEntry& e) { return e.zoneName == zoneTextName_; }),
            zoneToasts_.end());
    }

    if (zoneToasts_.empty()) return;

    auto* window = services_.window;
    float screenW = window ? static_cast<float>(window->getWidth()) : 1280.0f;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();

    for (int i = 0; i < static_cast<int>(zoneToasts_.size()); ++i) {
        const auto& e = zoneToasts_[i];
        constexpr float kSlideDur = 0.35f;
        float slideIn  = std::min(e.age, kSlideDur) / kSlideDur;
        float slideOut = std::min(std::max(0.0f, kZoneToastLifetime - e.age), kSlideDur) / kSlideDur;
        float slide    = std::min(slideIn, slideOut);
        float alpha    = std::clamp(slide, 0.0f, 1.0f);

        // Measure text to size the toast
        ImVec2 nameSz = font->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, e.zoneName.c_str());
        const char* header = "Entering:";
        ImVec2 hdrSz = font->CalcTextSizeA(11.0f, FLT_MAX, 0.0f, header);

        float toastW = std::max(nameSz.x, hdrSz.x) + 28.0f;
        float toastH = 42.0f;

        // Center the toast horizontally, appear just below the zone name area (top-center)
        float toastX = (screenW - toastW) * 0.5f;
        float toastY = 56.0f + i * (toastH + 4.0f);
        // Slide down from above
        float offY = (1.0f - slide) * (-toastH - 10.0f);
        toastY += offY;

        ImVec2 tl(toastX, toastY);
        ImVec2 br(toastX + toastW, toastY + toastH);

        draw->AddRectFilled(tl, br, IM_COL32(10, 10, 16, static_cast<int>(alpha * 200)), 6.0f);
        draw->AddRect(tl, br, IM_COL32(160, 140, 80, static_cast<int>(alpha * 220)), 6.0f, 0, 1.2f);

        float cx = tl.x + toastW * 0.5f;
        draw->AddText(font, 11.0f,
            ImVec2(cx - hdrSz.x * 0.5f, tl.y + 5.0f),
            IM_COL32(180, 170, 120, static_cast<int>(alpha * 200)), header);
        draw->AddText(font, 14.0f,
            ImVec2(cx - nameSz.x * 0.5f, tl.y + toastH * 0.5f + 1.0f),
            IM_COL32(255, 230, 140, static_cast<int>(alpha * 240)), e.zoneName.c_str());
    }
}

// ─── Area Trigger Message Toasts ─────────────────────────────────────────────
void ToastManager::renderAreaTriggerToasts(float deltaTime, game::GameHandler& gameHandler) {
    // Drain any pending messages from GameHandler
    while (gameHandler.hasAreaTriggerMsg()) {
        AreaTriggerToast t;
        t.text = gameHandler.popAreaTriggerMsg();
        t.age  = 0.0f;
        areaTriggerToasts_.push_back(std::move(t));
        if (areaTriggerToasts_.size() > 4)
            areaTriggerToasts_.erase(areaTriggerToasts_.begin());
    }

    // Age and prune
    constexpr float kLifetime = 4.5f;
    for (auto& t : areaTriggerToasts_) t.age += deltaTime;
    areaTriggerToasts_.erase(
        std::remove_if(areaTriggerToasts_.begin(), areaTriggerToasts_.end(),
                       [](const AreaTriggerToast& t) { return t.age >= kLifetime; }),
        areaTriggerToasts_.end());
    if (areaTriggerToasts_.empty()) return;

    auto* window = services_.window;
    float screenW = window ? static_cast<float>(window->getWidth())  : 1280.0f;
    float screenH = window ? static_cast<float>(window->getHeight()) :  720.0f;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    constexpr float kSlideDur = 0.35f;

    for (int i = 0; i < static_cast<int>(areaTriggerToasts_.size()); ++i) {
        const auto& t = areaTriggerToasts_[i];

        float slideIn  = std::min(t.age, kSlideDur) / kSlideDur;
        float slideOut = std::min(std::max(0.0f, kLifetime - t.age), kSlideDur) / kSlideDur;
        float alpha    = std::clamp(std::min(slideIn, slideOut), 0.0f, 1.0f);

        // Measure text
        ImVec2 txtSz = font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, t.text.c_str());
        float toastW = txtSz.x + 30.0f;
        float toastH = 30.0f;

        // Center horizontally, place below zone text (center of lower-third)
        float toastX = (screenW - toastW) * 0.5f;
        float toastY = screenH * 0.62f + i * (toastH + 3.0f);
        // Slide up from below
        float offY = (1.0f - std::min(slideIn, slideOut)) * (toastH + 12.0f);
        toastY += offY;

        ImVec2 tl(toastX, toastY);
        ImVec2 br(toastX + toastW, toastY + toastH);

        draw->AddRectFilled(tl, br, IM_COL32(8, 12, 22, static_cast<int>(alpha * 190)), 5.0f);
        draw->AddRect(tl, br, IM_COL32(100, 160, 220, static_cast<int>(alpha * 200)), 5.0f, 0, 1.0f);

        float cx = tl.x + toastW * 0.5f;
        // Shadow
        draw->AddText(font, 13.0f,
            ImVec2(cx - txtSz.x * 0.5f + 1, tl.y + (toastH - txtSz.y) * 0.5f + 1),
            IM_COL32(0, 0, 0, static_cast<int>(alpha * 180)), t.text.c_str());
        // Text in light blue
        draw->AddText(font, 13.0f,
            ImVec2(cx - txtSz.x * 0.5f, tl.y + (toastH - txtSz.y) * 0.5f),
            IM_COL32(180, 220, 255, static_cast<int>(alpha * 240)), t.text.c_str());
    }
}

// ============================================================
// Level-Up Ding Animation
// ============================================================

void ToastManager::triggerDing(uint32_t newLevel, uint32_t hpDelta, uint32_t manaDelta,
                              uint32_t str, uint32_t agi, uint32_t sta,
                              uint32_t intel, uint32_t spi) {
    // Set golden burst overlay state (consumed by GameScreen)
    levelUpFlashAlpha = 1.0f;
    levelUpDisplayLevel = newLevel;

    dingTimer_     = DING_DURATION;
    dingLevel_     = newLevel;
    dingHpDelta_   = hpDelta;
    dingManaDelta_ = manaDelta;
    dingStats_[0]  = str;
    dingStats_[1]  = agi;
    dingStats_[2]  = sta;
    dingStats_[3]  = intel;
    dingStats_[4]  = spi;

    auto* ac = services_.audioCoordinator;
    if (ac) {
        if (auto* sfx = ac->getUiSoundManager()) {
            sfx->playLevelUp();
        }
    }
    if (auto* renderer = services_.renderer) {
        if (auto* ac = renderer->getAnimationController()) ac->playEmote("cheer");
    }
}

void ToastManager::renderDingEffect() {
    if (dingTimer_ <= 0.0f) return;

    float dt = ImGui::GetIO().DeltaTime;
    dingTimer_ -= dt;
    if (dingTimer_ < 0.0f) dingTimer_ = 0.0f;

    // Show "You have reached level X!" for the first 2.5s, fade out over last 0.5s.
    // The 3D visual effect is handled by Renderer::triggerLevelUpEffect (LevelUp.m2).
    constexpr float kFadeTime = 0.5f;
    float alpha = dingTimer_ < kFadeTime ? (dingTimer_ / kFadeTime) : 1.0f;
    if (alpha <= 0.0f) return;

    ImGuiIO& io = ImGui::GetIO();
    float cx = io.DisplaySize.x * 0.5f;
    float cy = io.DisplaySize.y * 0.38f;  // Upper-center, like WoW

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    float baseSize = ImGui::GetFontSize();
    float fontSize = baseSize * 1.8f;

    char buf[64];
    snprintf(buf, sizeof(buf), "You have reached level %u!", dingLevel_);

    ImVec2 sz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, buf);
    float tx = cx - sz.x * 0.5f;
    float ty = cy - sz.y * 0.5f;

    // Slight black outline for readability
    draw->AddText(font, fontSize, ImVec2(tx + 2, ty + 2),
                  IM_COL32(0, 0, 0, static_cast<int>(alpha * 180)), buf);
    // Gold text
    draw->AddText(font, fontSize, ImVec2(tx, ty),
                  IM_COL32(255, 210, 0, static_cast<int>(alpha * 255)), buf);

    // Stat gains below the main text (shown only if server sent deltas)
    bool hasStatGains = (dingHpDelta_ > 0 || dingManaDelta_ > 0 ||
                         dingStats_[0] || dingStats_[1] || dingStats_[2] ||
                         dingStats_[3] || dingStats_[4]);
    if (hasStatGains) {
        float smallSize = baseSize * 0.95f;
        float yOff = ty + sz.y + 6.0f;

        // Build stat delta string: "+150 HP  +80 Mana  +2 Str  +2 Agi ..."
        static constexpr const char* kStatLabels[] = { "Str", "Agi", "Sta", "Int", "Spi" };
        char statBuf[128];
        int written = 0;
        if (dingHpDelta_ > 0)
            written += snprintf(statBuf + written, sizeof(statBuf) - written,
                                "+%u HP  ", dingHpDelta_);
        if (dingManaDelta_ > 0)
            written += snprintf(statBuf + written, sizeof(statBuf) - written,
                                "+%u Mana  ", dingManaDelta_);
        for (int i = 0; i < 5 && written < static_cast<int>(sizeof(statBuf)) - 1; ++i) {
            if (dingStats_[i] > 0)
                written += snprintf(statBuf + written, sizeof(statBuf) - written,
                                    "+%u %s  ", dingStats_[i], kStatLabels[i]);
        }
        // Trim trailing spaces
        while (written > 0 && statBuf[written - 1] == ' ') --written;
        statBuf[written] = '\0';

        if (written > 0) {
            ImVec2 ssz = font->CalcTextSizeA(smallSize, FLT_MAX, 0.0f, statBuf);
            float stx = cx - ssz.x * 0.5f;
            draw->AddText(font, smallSize, ImVec2(stx + 1, yOff + 1),
                          IM_COL32(0, 0, 0, static_cast<int>(alpha * 160)), statBuf);
            draw->AddText(font, smallSize, ImVec2(stx, yOff),
                          IM_COL32(100, 220, 100, static_cast<int>(alpha * 230)), statBuf);
        }
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

// ---------------------------------------------------------------------------
// Area discovery toast - "Discovered: <AreaName>! (+XP XP)" centered on screen
// ---------------------------------------------------------------------------

void ToastManager::renderDiscoveryToast() {
    if (discoveryToastTimer_ <= 0.0f) return;

    float dt = ImGui::GetIO().DeltaTime;
    discoveryToastTimer_ -= dt;
    if (discoveryToastTimer_ < 0.0f) discoveryToastTimer_ = 0.0f;

    // Fade: ramp up in first 0.4s, hold, fade out in last 1.0s
    float alpha;
    if (discoveryToastTimer_ > DISCOVERY_TOAST_DURATION - 0.4f)
        alpha = 1.0f - (discoveryToastTimer_ - (DISCOVERY_TOAST_DURATION - 0.4f)) / 0.4f;
    else if (discoveryToastTimer_ < 1.0f)
        alpha = discoveryToastTimer_;
    else
        alpha = 1.0f;
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    auto* window = services_.window;
    float screenW = window ? static_cast<float>(window->getWidth())  : 1280.0f;
    float screenH = window ? static_cast<float>(window->getHeight()) :  720.0f;

    ImFont* font = ImGui::GetFont();
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    const char* header   = "Discovered!";
    float headerSize     = 16.0f;
    float nameSize       = 28.0f;
    float xpSize         = 14.0f;

    ImVec2 headerDim = font->CalcTextSizeA(headerSize, FLT_MAX, 0.0f, header);
    ImVec2 nameDim   = font->CalcTextSizeA(nameSize,   FLT_MAX, 0.0f, discoveryToastName_.c_str());

    char xpBuf[48];
    if (discoveryToastXP_ > 0)
        snprintf(xpBuf, sizeof(xpBuf), "+%u XP", discoveryToastXP_);
    else
        xpBuf[0] = '\0';
    ImVec2 xpDim = font->CalcTextSizeA(xpSize, FLT_MAX, 0.0f, xpBuf);

    // Position slightly below zone text (at 37% down screen)
    float centreY = screenH * 0.37f;
    float headerX = (screenW - headerDim.x) * 0.5f;
    float nameX   = (screenW - nameDim.x)   * 0.5f;
    float xpX     = (screenW - xpDim.x)     * 0.5f;
    float headerY = centreY;
    float nameY   = centreY + headerDim.y + 4.0f;
    float xpY     = nameY + nameDim.y + 4.0f;

    // "Discovered!" in gold
    draw->AddText(font, headerSize, ImVec2(headerX + 1, headerY + 1),
                  IM_COL32(0, 0, 0, static_cast<int>(alpha * 160)), header);
    draw->AddText(font, headerSize, ImVec2(headerX, headerY),
                  IM_COL32(255, 215, 0, static_cast<int>(alpha * 255)), header);

    // Area name in white
    draw->AddText(font, nameSize, ImVec2(nameX + 1, nameY + 1),
                  IM_COL32(0, 0, 0, static_cast<int>(alpha * 160)), discoveryToastName_.c_str());
    draw->AddText(font, nameSize, ImVec2(nameX, nameY),
                  IM_COL32(255, 255, 255, static_cast<int>(alpha * 255)), discoveryToastName_.c_str());

    // XP gain in light green (if any)
    if (xpBuf[0] != '\0') {
        draw->AddText(font, xpSize, ImVec2(xpX + 1, xpY + 1),
                      IM_COL32(0, 0, 0, static_cast<int>(alpha * 140)), xpBuf);
        draw->AddText(font, xpSize, ImVec2(xpX, xpY),
                      IM_COL32(100, 220, 100, static_cast<int>(alpha * 230)), xpBuf);
    }
}

// ---------------------------------------------------------------------------
// Resurrection flash - brief screen brightening + "You have been resurrected!"
// banner when the player transitions from ghost back to alive.
// ---------------------------------------------------------------------------

void ToastManager::renderResurrectFlash() {
    if (resurrectFlashTimer_ <= 0.0f) return;

    float dt = ImGui::GetIO().DeltaTime;
    resurrectFlashTimer_ -= dt;
    if (resurrectFlashTimer_ <= 0.0f) {
        resurrectFlashTimer_ = 0.0f;
        return;
    }

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    float screenW = displaySize.x > 0.0f ? displaySize.x : 1280.0f;
    float screenH = displaySize.y > 0.0f ? displaySize.y : 720.0f;

    // Normalised age in [0, 1] (0 = just fired, 1 = fully elapsed)
    float t = 1.0f - resurrectFlashTimer_ / kResurrectFlashDuration;

    // Alpha envelope: fast fade-in (first 0.15s), hold, then fade-out (last 0.8s)
    float alpha;
    const float fadeIn  = 0.15f / kResurrectFlashDuration;   // ~5% of lifetime
    const float fadeOut = 0.8f  / kResurrectFlashDuration;   // ~27% of lifetime
    if (t < fadeIn)
        alpha = t / fadeIn;
    else if (t < 1.0f - fadeOut)
        alpha = 1.0f;
    else
        alpha = (1.0f - t) / fadeOut;
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    ImDrawList* bg = ImGui::GetBackgroundDrawList();

    // Soft golden/white vignette - brightening instead of darkening
    uint8_t vigA = static_cast<uint8_t>(50 * alpha);
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(screenW, screenH),
                      IM_COL32(200, 230, 255, vigA));

    // Centered banner panel
    constexpr float PANEL_W = 360.0f;
    constexpr float PANEL_H = 52.0f;
    float px = (screenW - PANEL_W) * 0.5f;
    float py = screenH * 0.34f;

    uint8_t bgA     = static_cast<uint8_t>(210 * alpha);
    uint8_t borderA = static_cast<uint8_t>(255 * alpha);
    uint8_t textA   = static_cast<uint8_t>(255 * alpha);

    // Background: deep blue-black
    bg->AddRectFilled(ImVec2(px, py), ImVec2(px + PANEL_W, py + PANEL_H),
                      IM_COL32(10, 18, 40, bgA), 8.0f);

    // Border glow: bright holy gold
    bg->AddRect(ImVec2(px, py), ImVec2(px + PANEL_W, py + PANEL_H),
                IM_COL32(200, 230, 100, borderA), 8.0f, 0, 2.0f);
    // Inner halo line
    bg->AddRect(ImVec2(px + 3.0f, py + 3.0f), ImVec2(px + PANEL_W - 3.0f, py + PANEL_H - 3.0f),
                IM_COL32(255, 255, 180, static_cast<uint8_t>(80 * alpha)), 6.0f, 0, 1.0f);

    // Resurrection message centered without decorative punctuation.
    const char* banner = "You have been resurrected";
    ImFont* font = ImGui::GetFont();
    float fontSize = ImGui::GetFontSize();
    ImVec2 textSz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, banner);
    float tx = px + (PANEL_W - textSz.x) * 0.5f;
    float ty = py + (PANEL_H - textSz.y) * 0.5f;

    // Drop shadow
    bg->AddText(font, fontSize, ImVec2(tx + 1.0f, ty + 1.0f),
                IM_COL32(0, 0, 0, static_cast<uint8_t>(180 * alpha)), banner);
    // Main text in warm gold
    bg->AddText(font, fontSize, ImVec2(tx, ty),
                IM_COL32(255, 240, 120, textA), banner);
}

// Zone discovery text - "Entering: <ZoneName>" fades in/out at screen centre
// ---------------------------------------------------------------------------

void ToastManager::renderZoneText(game::GameHandler& gameHandler) {
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

    // Drawn only if FrameXML is not, but tracked either way: the zone toasts
    // above are suppressed while this timer is running, so a plain early
    // return at the top of this function would swap one duplicate for
    // another - the banner would go and "Entering: Stormwind City" would come
    // back beside FrameXML's.
    if (frameXmlOwns(UiElement::ZoneText)) return;

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
