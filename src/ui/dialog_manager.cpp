#include "ui/dialog_manager.hpp"
#include "game/item_text.hpp"
#include "game/inventory_slots.hpp"
#include "ui/framexml_takeover.hpp"
#include "ui/inventory_screen.hpp"
#include "ui/chat_panel.hpp"
#include "ui/chat/chat_utils.hpp"
#include "ui/ui_colors.hpp"
#include "game/game_handler.hpp"
#include "core/application.hpp"

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <chrono>

namespace wowee { namespace ui {

namespace {
    using namespace wowee::ui::colors;
} // namespace


// ---------------------------------------------------------------------------
// Render early dialogs (group invite through LFG role check)
// ---------------------------------------------------------------------------
void DialogManager::renderDialogs(game::GameHandler& gameHandler) {
    // The prompts uiparent.lua raises with a StaticPopup of its own, on events
    // this client fires, are gone from here: the group invite, the duel, the
    // trade request, the summon, the shared quest, the guild invite and both
    // battleground invitations. Every one of them asked the same question
    // twice while dialogs were handed over, which they are by default.
    //
    // What is left is what FrameXML has nothing for.

    // No counterpart: FrameXML has the duel request popup and nothing that
    // counts down to the start.
    renderDuelCountdown(gameHandler);

}

// ============================================================
// Group Invite Popup
// ============================================================

void DialogManager::renderDuelCountdown(game::GameHandler& gameHandler) {
    float remaining = gameHandler.getDuelCountdownRemaining();
    if (remaining <= 0.0f) return;

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    float screenW = displaySize.x > 0.0f ? displaySize.x : 1280.0f;
    float screenH = displaySize.y > 0.0f ? displaySize.y : 720.0f;

    auto* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    float fontSize = ImGui::GetFontSize();

    // Show integer countdown or "Fight!" when under 0.5s
    char buf[32];
    if (remaining > 0.5f) {
        snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::ceil(remaining)));
    } else {
        snprintf(buf, sizeof(buf), "Fight!");
    }

    // Large font by scaling - use 4x font size for dramatic effect
    float scale = 4.0f;
    float scaledSize = fontSize * scale;
    ImVec2 textSz = font->CalcTextSizeA(scaledSize, FLT_MAX, 0.0f, buf);
    float tx = (screenW - textSz.x) * 0.5f;
    float ty = screenH * 0.35f - textSz.y * 0.5f;

    // Pulsing alpha: fades in and out per second
    float pulse = 0.75f + 0.25f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.28f);
    uint8_t alpha = static_cast<uint8_t>(255 * pulse);

    // Color: golden countdown, red "Fight!"
    ImU32 color = (remaining > 0.5f)
        ? IM_COL32(255, 200, 50, alpha)
        : IM_COL32(255, 60, 60, alpha);

    // Drop shadow
    dl->AddText(font, scaledSize, ImVec2(tx + 2.0f, ty + 2.0f), IM_COL32(0, 0, 0, alpha / 2), buf);
    dl->AddText(font, scaledSize, ImVec2(tx, ty), color, buf);
}

} // namespace ui
} // namespace wowee
