#pragma once

namespace wowee {
namespace ui {

/**
 * Chat appearance and auto-join settings.
 *
 * Extracted from ChatPanel (Phase 1.1 of chat_panel_ref.md).
 * Pure data, read and written through SettingsPanel; no dependency on
 * GameHandler or network.
 */
struct ChatSettings {
    // Appearance
    bool showTimestamps  = false;
    int  fontSize        = 1;   // 0=small, 1=medium, 2=large

    // Auto-join channels
    bool autoJoinGeneral      = true;
    bool autoJoinTrade        = true;
    bool autoJoinLocalDefense = true;
    bool autoJoinLFG          = true;
    bool autoJoinLocal        = true;

    /// Speech bubbles over heads, for what is said and yelled nearby; and
    /// party and raid chat as well, which wants both. The interface's own
    /// Social page had the two boxes and the client drew the bubbles; now
    /// both are here, where the chat panel's other settings are.
    bool showBubbles  = true;
    bool partyBubbles = false;

    // No window state here. This client drew its own chat window once and kept
    // three fields for it - whether it was locked, and the size a resize left
    // it. FrameXML draws chat now, chat_panel.cpp sets no window size at all,
    // and nothing had read any of the three for a long time: the width and
    // height were written to the config every run and read back into fields
    // nobody asked. The comment on them described capturing a resize, which
    // nothing did.
};

} // namespace ui
} // namespace wowee
