#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

#include "game/spell_defines.hpp"

namespace wowee {
namespace game {

/// What the damage meter shows: the player's damage and healing per second
/// over the last few seconds, and over the whole of the current or last fight.
struct DamageMeterReading {
    float dps = 0.0f;
    float hps = 0.0f;
    float encounterDps = 0.0f;
    float encounterHps = 0.0f;
    bool inCombat = false;
    /// False when there is nothing to report and the meter should hide.
    bool show = false;
};

/// The client's own damage meter, kept apart from whatever draws it.
///
/// The recent rate comes from the floating combat text, which holds the last
/// CombatTextEntry::LIFETIME seconds. The fight's totals come from the combat
/// log, read by how many entries have ever been appended rather than by index:
/// the log drops its oldest entry once it holds 500, so an index stops moving
/// and a fight after the first five hundred events counted nothing.
class DamageMeter {
public:
    DamageMeterReading update(bool inCombat, float dt,
                              const std::deque<CombatLogEntry>& log, uint64_t logAppended,
                              const std::vector<CombatTextEntry>& text) {
        if (inCombat && !wasInCombat_) {
            encounterDamage_ = 0.0f;
            encounterHeal_ = 0.0f;
            combatAge_ = 0.0f;
            seen_ = logAppended;
        }
        if (inCombat) {
            combatAge_ += dt;
            const uint64_t fresh = std::min<uint64_t>(logAppended - seen_, log.size());
            for (size_t i = log.size() - static_cast<size_t>(fresh); i < log.size(); ++i) {
                const auto& e = log[i];
                if (!e.isPlayerSource) continue;
                if (isDamage(e.type)) encounterDamage_ += static_cast<float>(e.amount);
                else if (isHeal(e.type)) encounterHeal_ += static_cast<float>(e.amount);
            }
        }
        seen_ = logAppended;
        wasInCombat_ = inCombat;

        float recentDamage = 0.0f, recentHeal = 0.0f;
        for (const auto& e : text) {
            if (!e.isPlayerSource) continue;
            if (isDamage(e.type)) recentDamage += static_cast<float>(e.amount);
            else if (isHeal(e.type)) recentHeal += static_cast<float>(e.amount);
        }

        DamageMeterReading out;
        out.inCombat = inCombat;
        out.show = inCombat || recentDamage >= 1.0f || recentHeal >= 1.0f ||
                   encounterDamage_ >= 1.0f || encounterHeal_ >= 1.0f;
        // Never longer than the text window, or the rate is under-counted once
        // the oldest numbers expire; never shorter than the fight, or the first
        // hit of a fight reads as a huge rate.
        const float window = std::max(std::min(combatAge_, CombatTextEntry::LIFETIME), 0.1f);
        out.dps = recentDamage / window;
        out.hps = recentHeal / window;
        if (combatAge_ > 0.1f) {
            out.encounterDps = encounterDamage_ / combatAge_;
            out.encounterHps = encounterHeal_ / combatAge_;
        }
        return out;
    }

private:
    static bool isDamage(CombatTextEntry::Type t) {
        using T = CombatTextEntry;
        return t == T::MELEE_DAMAGE || t == T::SPELL_DAMAGE || t == T::CRIT_DAMAGE ||
               t == T::PERIODIC_DAMAGE || t == T::GLANCING || t == T::CRUSHING;
    }
    static bool isHeal(CombatTextEntry::Type t) {
        using T = CombatTextEntry;
        return t == T::HEAL || t == T::CRIT_HEAL || t == T::PERIODIC_HEAL;
    }

    bool wasInCombat_ = false;
    float combatAge_ = 0.0f;
    float encounterDamage_ = 0.0f;
    float encounterHeal_ = 0.0f;
    uint64_t seen_ = 0;
};

}  // namespace game
}  // namespace wowee
