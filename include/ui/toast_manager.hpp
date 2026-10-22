#pragma once

#include "ui/ui_services.hpp"
#include <vulkan/vulkan.h>
#include <imgui.h>
#include <string>
#include <vector>
#include <cstdint>

namespace wowee {
namespace game { class GameHandler; }
namespace ui {

/**
 * Toast / notification overlay manager
 *
 * Owns all toast state, callbacks, and rendering:
 *   level-up ding, achievement, area discovery, whisper, quest progress,
 *   player level-up, PvP honor, item loot, reputation, quest complete,
 *   zone entry, area trigger, resurrect flash, and zone text.
 */
class ToastManager {
public:
    ToastManager() = default;

    /// Register toast-related callbacks on GameHandler (idempotent - safe every frame)
    void setupCallbacks(game::GameHandler& gameHandler);

    /// Render "early" toasts (rep, quest-complete, zone, area-trigger) - called before action bars
    void renderEarlyToasts(float deltaTime, game::GameHandler& gameHandler);

    /// Render "late" toasts (ding, achievement, discovery, whisper, quest progress,
    /// player level-up, PvP honor, item loot, resurrect flash, zone text) - called after escape menu
    void renderLateToasts(game::GameHandler& gameHandler);

    /// Fire level-up ding animation + sound
    void triggerDing(uint32_t newLevel, uint32_t hpDelta = 0, uint32_t manaDelta = 0,
                     uint32_t str = 0, uint32_t agi = 0, uint32_t sta = 0,
                     uint32_t intel = 0, uint32_t spi = 0);

    /// Fire achievement earned toast + sound
    /// The sound an achievement makes. The badge is FrameXML's.
    void playAchievementEarned();

    // UIServices injection (Phase B singleton breaking)
    void setServices(const UIServices& services) { services_ = services; }

    // --- public state consumed by GameScreen for the golden burst overlay ---
    float levelUpFlashAlpha = 0.0f;
    uint32_t levelUpDisplayLevel = 0;

private:
    // Injected UI services
    UIServices services_;

    // ---- Ding effect (own level-up) ----
    static constexpr float DING_DURATION = 4.0f;
    float dingTimer_ = 0.0f;
    uint32_t dingLevel_ = 0;
    uint32_t dingHpDelta_   = 0;
    uint32_t dingManaDelta_ = 0;
    uint32_t dingStats_[5]  = {};
    void renderDingEffect();

    // ---- Achievement toast ----
    static constexpr float ACHIEVEMENT_TOAST_DURATION = 5.0f;

    // ---- Area discovery toast ----
    static constexpr float DISCOVERY_TOAST_DURATION = 4.0f;
    float discoveryToastTimer_ = 0.0f;
    std::string discoveryToastName_;
    uint32_t discoveryToastXP_ = 0;
    bool areaDiscoveryCallbackSet_ = false;
    void renderDiscoveryToast();

    // ---- Quest completion toast ----
    struct QuestCompleteToastEntry {
        uint32_t questId = 0;
        std::string title;
        float age = 0.0f;
    };
    std::vector<QuestCompleteToastEntry> questCompleteToasts_;
    bool questCompleteCallbackSet_ = false;
    static constexpr float kQuestCompleteToastLifetime = 4.0f;
    void renderQuestCompleteToasts(float deltaTime);

    // ---- Zone entry toast ----
    struct ZoneToastEntry {
        std::string zoneName;
        float age = 0.0f;
    };
    std::vector<ZoneToastEntry> zoneToasts_;
    std::string lastKnownZone_;
    static constexpr float kZoneToastLifetime = 3.0f;
    void renderZoneToasts(float deltaTime);

    // ---- Area trigger message toast ----
    struct AreaTriggerToast {
        std::string text;
        float age = 0.0f;
    };
    std::vector<AreaTriggerToast> areaTriggerToasts_;
    void renderAreaTriggerToasts(float deltaTime, game::GameHandler& gameHandler);

    // ---- Resurrection flash ----
    float resurrectFlashTimer_ = 0.0f;
    static constexpr float kResurrectFlashDuration = 3.0f;
    bool ghostStateCallbackSet_ = false;
    void renderResurrectFlash();

    // ---- Zone discovery text ("Entering: <ZoneName>") ----
    static constexpr float ZONE_TEXT_DURATION = 5.0f;
    float zoneTextTimer_ = 0.0f;
    std::string zoneTextName_;
    std::string lastKnownZoneName_;
    uint32_t lastKnownWorldStateZoneId_ = 0;
    void renderZoneText(game::GameHandler& gameHandler);
};

} // namespace ui
} // namespace wowee
