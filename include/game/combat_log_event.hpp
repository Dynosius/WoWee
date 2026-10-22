#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "game/handler_types.hpp"
#include "game/spell_defines.hpp"

namespace wowee {
namespace game {

/// Which interface the arguments are for. 2.4.3's combat log addon reads damage
/// without an overkill and a heal without overhealing or absorbed, and 3.3.5's
/// reads both; a layout for one handed to the other is read a field out.
/// 1.12 has no combat log event at all.
enum class CombatLogLayout { Tbc, Wotlk };

/// What the client knows about one combat event, before it is shaped for Lua.
struct CombatLogEventInput {
    CombatLogLayout layout = CombatLogLayout::Wotlk;
    CombatTextEntry::Type type = CombatTextEntry::MELEE_DAMAGE;
    int32_t amount = 0;
    uint32_t spellId = 0;
    uint8_t powerType = 0;
    uint64_t srcGuid = 0;
    uint64_t dstGuid = 0;
    uint64_t playerGuid = 0;
    uint64_t petGuid = 0;
    std::string srcName;
    std::string dstName;
    std::string spellName;
    int64_t timestamp = 0;
};

namespace combat_log {

/// COMBATLOG_OBJECT_* bits, which the interface reads to colour a name and to
/// decide whether an event is the player's own.
inline constexpr uint32_t kMine = 0x1, kOutsider = 0x8;
inline constexpr uint32_t kFriendly = 0x10, kHostile = 0x40;
inline constexpr uint32_t kControlPlayer = 0x100, kControlNpc = 0x200;
inline constexpr uint32_t kTypePlayer = 0x400, kTypeNpc = 0x800, kTypePet = 0x1000;

/// The unit flags for a guid. The client does not track reaction, so a creature
/// is taken as hostile and another player as friendly, which is right for the
/// fights this log is read for.
inline uint32_t unitFlags(uint64_t guid, uint64_t playerGuid, uint64_t petGuid) {
    if (guid == 0) return 0;
    if (guid == playerGuid) return kMine | kFriendly | kControlPlayer | kTypePlayer;
    if (guid == petGuid)    return kMine | kFriendly | kControlPlayer | kTypePet;
    if ((guid >> 48) == 0)  return kOutsider | kFriendly | kControlPlayer | kTypePlayer;
    return kOutsider | kHostile | kControlNpc | kTypeNpc;
}

inline std::string guidText(uint64_t guid) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(guid));
    return buf;
}

inline std::string textOrNil(const std::string& text) {
    return text.empty() ? std::string(kEventNil) : text;
}

inline const char* environmentName(uint8_t type) {
    switch (type) {
        case 0: return "Fatigue";
        case 1: return "Drowning";
        case 2: return "Falling";
        case 3: return "Lava";
        case 4: return "Slime";
        case 5: return "Fire";
        default: return "Falling";
    }
}

}  // namespace combat_log

/// The arguments of one combat event as Blizzard_CombatLog parses them in this
/// client: the timestamp, the subevent, the source's guid, name and flags, the
/// destination's, then the prefix and suffix that subevent carries. There is no
/// hideCaster and there are no raid flags; those arrived with Cataclysm, and a
/// parser that does not expect them reads every later field one place off. Empty when the event has no counterpart in the real
/// client's log - experience, honour and the partial absorb and resist lines
/// that ride along with a hit, which the hit's own event already accounts for.
///
/// The parser does arithmetic on fields past the destination, so a layout that
/// is merely close takes the whole handler down: SWING_DAMAGE subtracts the
/// overkill from the amount, and a missing overkill is a nil in that sum.
inline std::vector<std::string> buildCombatLogEvent(const CombatLogEventInput& in) {
    using T = CombatTextEntry;
    using namespace combat_log;

    const bool hasSpell = in.spellId != 0;
    const std::string amount = std::to_string(in.amount);
    const std::string physical = "1";
    const std::string nil = kEventNil;

    // Prefix: a spell's id, name and school. A swing has none.
    auto spellPrefix = [&] {
        return std::vector<std::string>{std::to_string(in.spellId),
                                        in.spellName.empty() ? "Spell " + std::to_string(in.spellId)
                                                             : in.spellName,
                                        physical};
    };
    const bool wotlk = in.layout == CombatLogLayout::Wotlk;
    // Suffix of a damage event: amount, [overkill,] school, resisted, blocked,
    // absorbed, critical, glancing, crushing.
    auto damageSuffix = [&](bool crit, bool glancing, bool crushing) {
        std::vector<std::string> out{amount};
        if (wotlk) out.push_back("0");   // overkill
        out.insert(out.end(), {physical, nil, nil, nil, std::string(eventBool(crit)),
                               std::string(eventBool(glancing)), std::string(eventBool(crushing))});
        return out;
    };
    auto healSuffix = [&](bool crit) {
        if (!wotlk) return std::vector<std::string>{amount, eventBool(crit)};
        return std::vector<std::string>{amount, "0", nil, eventBool(crit)};
    };

    std::string event;
    std::vector<std::string> prefix;
    std::vector<std::string> suffix;
    bool environmental = false;

    switch (in.type) {
        case T::MELEE_DAMAGE: case T::CRIT_DAMAGE: case T::GLANCING: case T::CRUSHING:
        case T::SPELL_DAMAGE: case T::PERIODIC_DAMAGE: {
            const bool swing = !hasSpell && (in.type == T::MELEE_DAMAGE || in.type == T::CRIT_DAMAGE ||
                                              in.type == T::GLANCING || in.type == T::CRUSHING);
            const bool periodic = in.type == T::PERIODIC_DAMAGE;
            event = swing ? "SWING_DAMAGE" : (periodic ? "SPELL_PERIODIC_DAMAGE" : "SPELL_DAMAGE");
            if (!swing) prefix = spellPrefix();
            suffix = damageSuffix(in.type == T::CRIT_DAMAGE, in.type == T::GLANCING,
                                  in.type == T::CRUSHING);
            break;
        }
        case T::HEAL: case T::CRIT_HEAL: case T::PERIODIC_HEAL:
            event = in.type == T::PERIODIC_HEAL ? "SPELL_PERIODIC_HEAL" : "SPELL_HEAL";
            prefix = spellPrefix();
            suffix = healSuffix(in.type == T::CRIT_HEAL);
            break;
        case T::ENVIRONMENTAL:
            event = "ENVIRONMENTAL_DAMAGE";
            environmental = true;
            prefix = {environmentName(in.powerType)};
            suffix = damageSuffix(false, false, false);
            break;
        case T::ENERGIZE:
            event = "SPELL_ENERGIZE";
            prefix = spellPrefix();
            suffix = {amount, std::to_string(in.powerType)};
            break;
        case T::POWER_DRAIN:
            event = "SPELL_DRAIN";
            prefix = spellPrefix();
            suffix = {amount, std::to_string(in.powerType), "0"};
            break;
        case T::MISS: case T::DODGE: case T::PARRY: case T::BLOCK: case T::EVADE:
        case T::IMMUNE: case T::DEFLECT: case T::REFLECT: {
            event = hasSpell ? "SPELL_MISSED" : "SWING_MISSED";
            if (hasSpell) prefix = spellPrefix();
            const char* missType =
                in.type == T::DODGE ? "DODGE" : in.type == T::PARRY ? "PARRY" :
                in.type == T::BLOCK ? "BLOCK" : in.type == T::EVADE ? "EVADE" :
                in.type == T::IMMUNE ? "IMMUNE" : in.type == T::DEFLECT ? "DEFLECT" :
                in.type == T::REFLECT ? "REFLECT" : "MISS";
            suffix = {missType};
            // The parser formats these three with the amount turned aside.
            if (wotlk && in.type == T::BLOCK) suffix.push_back(amount);
            break;
        }
        case T::RESIST: case T::ABSORB:
            // With an amount these are the part of a hit that did not land, and
            // the hit's own event carries that. With none the whole thing was
            // turned aside, which is a miss.
            if (in.amount > 0) return {};
            event = hasSpell ? "SPELL_MISSED" : "SWING_MISSED";
            if (hasSpell) prefix = spellPrefix();
            suffix = {in.type == T::RESIST ? "RESIST" : "ABSORB"};
            if (wotlk) suffix.push_back("0");
            break;
        default:
            return {};
    }

    std::vector<std::string> out;
    out.reserve(11 + prefix.size() + suffix.size());
    out.push_back(std::to_string(in.timestamp));
    out.push_back(event);
    out.push_back(environmental || in.srcGuid == 0 ? guidText(0) : guidText(in.srcGuid));
    out.push_back(textOrNil(in.srcName));
    out.push_back(std::to_string(unitFlags(in.srcGuid, in.playerGuid, in.petGuid)));
    out.push_back(guidText(in.dstGuid));
    out.push_back(textOrNil(in.dstName));
    out.push_back(std::to_string(unitFlags(in.dstGuid, in.playerGuid, in.petGuid)));
    out.insert(out.end(), prefix.begin(), prefix.end());
    out.insert(out.end(), suffix.begin(), suffix.end());
    return out;
}

}  // namespace game
}  // namespace wowee
