#pragma once

// The combat log as 1.12 shows it: a line of chat.
//
// Classic and Turtle have no combat log event. Their client formats each hit
// itself from a GlobalStrings pattern - "Your %s hits %s for %d." - and sends
// the sentence as one of fifty-odd CHAT_MSG_COMBAT_* and CHAT_MSG_SPELL_*
// events, chosen by who did it to whom. ChatFrame2, the Combat Log tab, is
// registered for those events, and so are the damage meters written for 1.12,
// which parse the sentence back apart with the same patterns. An event that
// is named wrongly or a pattern given its arguments in the wrong order shows a
// line in the log and is silently ignored by every one of them.
//
// What is decided here is the event, the GlobalStrings name and the
// arguments. The pattern is read from the interface at the moment it is
// formatted, so a localized client reads its own language.

#include <cstdint>
#include <string>
#include <vector>

#include "game/spell_defines.hpp"

namespace wowee {
namespace game {

/// Who a combatant is to the player, in the terms 1.12's events are split by.
enum class CombatRelation : uint8_t {
    Self, Pet, Party, FriendlyPlayer, HostilePlayer, Creature
};

struct ClassicCombatInput {
    CombatTextEntry::Type type = CombatTextEntry::MELEE_DAMAGE;
    int32_t amount = 0;
    uint8_t powerType = 0;       ///< power for energize and drain; kind for environmental
    uint32_t schoolMask = 0;     ///< the spell's, 0 when unknown
    std::string spellName;       ///< empty for a swing
    std::string srcName;
    std::string dstName;
    CombatRelation src = CombatRelation::Creature;
    CombatRelation dst = CombatRelation::Creature;
};

struct ClassicCombatMessage {
    std::string event;               ///< "CHAT_MSG_..."; empty means nothing is sent
    std::string pattern;             ///< GlobalStrings name
    std::vector<std::string> args;   ///< in the pattern's order
};

namespace classic_combat {

/// The event the game side sends a decided line on, ahead of formatting: the
/// chat event, the pattern's name, then the pattern's arguments. Caught by the
/// Lua engine before dispatch, so no frame ever sees it.
inline constexpr const char* kLineEvent = "__WOWEE_CLASSIC_COMBAT_LINE";

/// Marks an argument that is itself a GlobalStrings name - a school or a
/// power - so it is read in the client's language rather than written here in
/// English. No name, spell or number begins with this byte.
inline constexpr char kGlobalArg = '\x01';

inline std::string global(const char* name) { return std::string(1, kGlobalArg) + name; }

/// SPELL_SCHOOLn_CAP for a school mask's lowest school, or empty for physical
/// and unknown, which 1.12 writes without a school at all.
inline std::string schoolArg(uint32_t mask) {
    if (mask <= 1) return {};
    for (int i = 1; i < 7; ++i) {
        if (mask & (1u << i)) {
            return global(("SPELL_SCHOOL" + std::to_string(i) + "_CAP").c_str());
        }
    }
    return {};
}

inline std::string powerArg(uint8_t powerType) {
    switch (powerType) {
        case 0: return global("MANA");
        case 1: return global("RAGE");
        case 2: return global("FOCUS");
        case 3: return global("ENERGY");
        case 4: return global("HAPPINESS");
        default: return global("MANA");
    }
}

/// The word the event names use for a side: SELF, PET, PARTY and so on.
/// Party covers the pet's events from a creature's side - 1.12 has no
/// CREATURE_VS_PET - and periodic events, which have no PET of their own.
inline const char* actorWord(CombatRelation r) {
    switch (r) {
        case CombatRelation::Self:           return "SELF";
        case CombatRelation::Pet:            return "PET";
        case CombatRelation::Party:          return "PARTY";
        case CombatRelation::FriendlyPlayer: return "FRIENDLYPLAYER";
        case CombatRelation::HostilePlayer:  return "HOSTILEPLAYER";
        case CombatRelation::Creature:       return "CREATURE";
    }
    return "CREATURE";
}

/// The event for something the source did: "COMBAT_SELF_HITS",
/// "SPELL_CREATURE_VS_PARTY_DAMAGE". `family` is COMBAT or SPELL, `kind` the
/// suffix (HITS, MISSES, DAMAGE, BUFF).
inline std::string byActor(const char* family, CombatRelation src, CombatRelation dst,
                           const char* kind) {
    std::string e = std::string("CHAT_MSG_") + family + "_";
    if (src == CombatRelation::Creature) {
        const char* vs = dst == CombatRelation::Self ? "SELF"
                       : (dst == CombatRelation::Pet || dst == CombatRelation::Party) ? "PARTY"
                       : "CREATURE";
        return e + "CREATURE_VS_" + vs + "_" + kind;
    }
    return e + actorWord(src) + "_" + kind;
}

/// The event for something that happened to the target over time:
/// SPELL_PERIODIC_SELF_DAMAGE, SPELL_PERIODIC_PARTY_BUFFS.
inline std::string periodic(CombatRelation dst, const char* kind) {
    const char* who = dst == CombatRelation::Pet ? "PARTY" : actorWord(dst);
    return std::string("CHAT_MSG_SPELL_PERIODIC_") + who + "_" + kind;
}

inline const char* side(CombatRelation r) {
    return r == CombatRelation::Self ? "SELF" : "OTHER";
}

}  // namespace classic_combat

/// The chat line 1.12 writes for one combat event. An event 1.12 does not log
/// - experience and honour, which already arrive as chat of their own, and the
/// procs and steals it has no sentence for - answers an empty event.
inline ClassicCombatMessage buildClassicCombatMessage(const ClassicCombatInput& in) {
    using namespace classic_combat;
    using T = CombatTextEntry;
    ClassicCombatMessage m;
    const bool srcMe = in.src == CombatRelation::Self;
    const bool dstMe = in.dst == CombatRelation::Self;
    const std::string pair = std::string(side(in.src)) + side(in.dst);
    const std::string amount = std::to_string(in.amount);
    const std::string spell = in.spellName.empty() ? global("UNKNOWN") : in.spellName;
    const bool swing = in.spellName.empty();

    // Who-did-what-to-whom arguments, in the order every SELF/OTHER family
    // shares: the source unless it is "you", then the target unless it is.
    auto actors = [&](std::vector<std::string>& a) {
        if (!srcMe) a.push_back(in.srcName);
        if (!dstMe) a.push_back(in.dstName);
    };

    switch (in.type) {
        case T::MELEE_DAMAGE: case T::CRIT_DAMAGE:
        case T::GLANCING: case T::CRUSHING: case T::SPELL_DAMAGE: {
            const bool crit = in.type == T::CRIT_DAMAGE;
            const std::string school = schoolArg(in.schoolMask);
            if (swing && in.type != T::SPELL_DAMAGE) {
                if (srcMe && dstMe) return m;   // there is no sentence for it
                m.event = byActor("COMBAT", in.src, in.dst, "HITS");
                m.pattern = std::string("COMBATHIT") + (crit ? "CRIT" : "") +
                            (school.empty() ? "" : "SCHOOL") + pair;
                actors(m.args);
            } else {
                m.event = byActor("SPELL", in.src, in.dst, "DAMAGE");
                m.pattern = std::string("SPELLLOG") + (crit ? "CRIT" : "") +
                            (school.empty() ? "" : "SCHOOL") + pair;
                if (!srcMe) m.args.push_back(in.srcName);
                m.args.push_back(spell);
                if (!dstMe) m.args.push_back(in.dstName);
            }
            m.args.push_back(amount);
            if (!school.empty()) m.args.push_back(school);
            return m;
        }
        case T::PERIODIC_DAMAGE: {
            std::string school = schoolArg(in.schoolMask);
            if (school.empty()) school = global("SPELL_SCHOOL0_CAP");
            m.event = periodic(in.dst, "DAMAGE");
            m.pattern = "PERIODICAURADAMAGE" + pair;
            // "%s suffers %d %s damage from %s's %s." - the target leads.
            if (!dstMe) m.args.push_back(in.dstName);
            m.args.push_back(amount);
            m.args.push_back(school);
            if (!srcMe) m.args.push_back(in.srcName);
            m.args.push_back(spell);
            return m;
        }
        case T::HEAL: case T::CRIT_HEAL: {
            m.event = byActor("SPELL", in.src, in.dst, "BUFF");
            m.pattern = std::string("HEALED") + (in.type == T::CRIT_HEAL ? "CRIT" : "") + pair;
            if (!srcMe) m.args.push_back(in.srcName);
            m.args.push_back(spell);
            if (!dstMe) m.args.push_back(in.dstName);
            m.args.push_back(amount);
            return m;
        }
        case T::PERIODIC_HEAL: {
            m.event = periodic(in.dst, "BUFFS");
            m.pattern = "PERIODICAURAHEAL" + pair;
            // SELFSELF is "You gain %d health from %s." - no "your".
            if (!dstMe) m.args.push_back(in.dstName);
            m.args.push_back(amount);
            if (!srcMe) m.args.push_back(in.srcName);
            m.args.push_back(spell);
            return m;
        }
        case T::MISS: case T::DODGE: case T::PARRY: case T::BLOCK: case T::EVADE:
        case T::IMMUNE: case T::DEFLECT: case T::ABSORB: case T::RESIST: {
            if (swing) {
                if (srcMe && dstMe) return m;
                static constexpr const char* kVs[] = {
                    "MISSED", "VSDODGE", "VSPARRY", "VSBLOCK", "VSEVADE",
                    "VSIMMUNE", "VSDEFLECT", "VSABSORB", "VSRESIST"};
                const int i = in.type == T::MISS ? 0 : in.type == T::DODGE ? 1
                            : in.type == T::PARRY ? 2 : in.type == T::BLOCK ? 3
                            : in.type == T::EVADE ? 4 : in.type == T::IMMUNE ? 5
                            : in.type == T::DEFLECT ? 6 : in.type == T::ABSORB ? 7 : 8;
                m.event = byActor("COMBAT", in.src, in.dst, "MISSES");
                m.pattern = std::string(kVs[i]) + pair;
                actors(m.args);
                return m;
            }
            static constexpr const char* kSpell[] = {
                "SPELLMISS", "SPELLDODGED", "SPELLPARRIED", "SPELLBLOCKED",
                "SPELLEVADED", "SPELLIMMUNE", "SPELLDEFLECTED", "SPELLLOGABSORB",
                "SPELLRESIST"};
            const int i = in.type == T::MISS ? 0 : in.type == T::DODGE ? 1
                        : in.type == T::PARRY ? 2 : in.type == T::BLOCK ? 3
                        : in.type == T::EVADE ? 4 : in.type == T::IMMUNE ? 5
                        : in.type == T::DEFLECT ? 6 : in.type == T::ABSORB ? 7 : 8;
            m.event = byActor("SPELL", in.src, in.dst, "DAMAGE");
            m.pattern = std::string(kSpell[i]) + pair;
            if (!srcMe) m.args.push_back(in.srcName);
            m.args.push_back(spell);
            // The OTHERSELF forms end at the spell - "Bob's Fireball was
            // dodged." - and the immune one names the target last.
            if (!dstMe) m.args.push_back(in.dstName);
            return m;
        }
        case T::REFLECT: {
            m.event = byActor("SPELL", in.src, in.dst, "DAMAGE");
            m.pattern = "SPELLREFLECT" + pair;
            if (!srcMe) m.args.push_back(in.srcName);
            m.args.push_back(spell);
            if (!dstMe) m.args.push_back(in.dstName);
            return m;
        }
        case T::ENERGIZE: {
            // "Bob gains 5 Mana from Mana Spring." is SELFOTHER: the "your"
            // is left unsaid, and the line is the player's own buff event.
            m.event = byActor("SPELL", in.src, in.dst, "BUFF");
            m.pattern = "POWERGAIN" + pair;
            if (!dstMe) m.args.push_back(in.dstName);
            m.args.push_back(amount);
            m.args.push_back(powerArg(in.powerType));
            if (!srcMe) m.args.push_back(in.srcName);
            m.args.push_back(spell);
            return m;
        }
        case T::POWER_DRAIN: {
            // A drain is the drainer's gain, and 1.12 files it with the buffs.
            m.event = byActor("SPELL", in.src, in.dst, "BUFF");
            m.pattern = "SPELLPOWERDRAIN" + pair;
            if (!srcMe) m.args.push_back(in.srcName);
            m.args.push_back(spell);
            m.args.push_back(amount);
            m.args.push_back(powerArg(in.powerType));
            if (!dstMe) m.args.push_back(in.dstName);
            return m;
        }
        case T::ENVIRONMENTAL: {
            static constexpr const char* kKinds[] = {
                "FATIGUE", "DROWNING", "FALLING", "LAVA", "SLIME", "FIRE"};
            const char* kind = in.powerType < 6 ? kKinds[in.powerType] : "FALLING";
            // Filed as the sufferer's own hits: "You fall and lose 40 health."
            // arrives with the player's swings.
            m.event = byActor("COMBAT", in.dst, CombatRelation::Creature, "HITS");
            m.pattern = std::string("VSENVIRONMENTALDAMAGE_") + kind + (dstMe ? "_SELF" : "_OTHER");
            if (!dstMe) m.args.push_back(in.dstName);
            m.args.push_back(amount);
            return m;
        }
        case T::INTERRUPT: {
            if (dstMe && srcMe) return m;
            m.event = byActor("SPELL", in.src, in.dst, "DAMAGE");
            m.pattern = "SPELLINTERRUPT" + pair;
            actors(m.args);
            m.args.push_back(spell);
            return m;
        }
        case T::INSTAKILL: {
            m.event = byActor("SPELL", in.src, in.dst, "DAMAGE");
            m.pattern = dstMe ? "INSTAKILLSELF" : "INSTAKILLOTHER";
            if (!dstMe) m.args.push_back(in.dstName);
            m.args.push_back(swing ? in.srcName : spell);
            return m;
        }
        case T::DISPEL: {
            m.event = "CHAT_MSG_SPELL_BREAK_AURA";
            m.pattern = dstMe ? "AURADISPELSELF" : "AURADISPELOTHER";
            if (!dstMe) m.args.push_back(in.dstName);
            m.args.push_back(spell);
            return m;
        }
        case T::XP_GAIN: case T::HONOR_GAIN: case T::PROC_TRIGGER: case T::STEAL:
            return m;
    }
    return m;
}

}  // namespace game
}  // namespace wowee
