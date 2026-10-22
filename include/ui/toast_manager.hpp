#pragma once

#include "ui/ui_services.hpp"
#include <string>
#include <cstdint>

namespace wowee {
namespace game { class GameHandler; }
namespace ui {

/**
 * What is left of the client's own notifications.
 *
 * The level-up banner, the resurrection banner and the duel countdown are
 * frames of the interface's own (lua_engine.cpp); reputation, honour, loot and
 * discovery are chat lines. This keeps the two sounds and the emote that go
 * with them, and the zone banner for when the interface did not load.
 */
class ToastManager {
public:
    ToastManager() = default;

    /// The character cheers on a level-up.
    void triggerDing(uint32_t newLevel);

    /// The sound an achievement makes. The badge is FrameXML's.
    void playAchievementEarned();

    /// "Entering: <zone>", drawn only when FrameXML's ZoneTextFrame is not.
    void renderZoneText(game::GameHandler& gameHandler);

    // UIServices injection (Phase B singleton breaking)
    void setServices(const UIServices& services) { services_ = services; }

private:
    UIServices services_;

    static constexpr float ZONE_TEXT_DURATION = 5.0f;
    float zoneTextTimer_ = 0.0f;
    std::string zoneTextName_;
    std::string lastKnownZoneName_;
    uint32_t lastKnownWorldStateZoneId_ = 0;
};

} // namespace ui
} // namespace wowee
