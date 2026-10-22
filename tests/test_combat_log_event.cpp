// The arguments Blizzard_CombatLog is handed for a combat event.
//
// That addon indexes them by position and does arithmetic on some: SWING_DAMAGE
// subtracts the overkill from the amount, so a layout that is merely close is
// not a wrong line in the log, it is an error that takes the handler down and
// leaves the window empty. These cases pin the positions.
#include <catch_amalgamated.hpp>
#include "game/combat_log_event.hpp"

using namespace wowee::game;
using T = CombatTextEntry;

namespace {
constexpr uint64_t kPlayer = 0x1;
constexpr uint64_t kCreature = 0xF130000000000002ULL;

CombatLogEventInput hit(T::Type type, uint32_t spell = 0) {
    CombatLogEventInput in;
    in.type = type;
    in.amount = 42;
    in.spellId = spell;
    in.srcGuid = kPlayer;
    in.dstGuid = kCreature;
    in.playerGuid = kPlayer;
    in.srcName = "Alpha";
    in.dstName = "Orc";
    in.spellName = spell ? "Fireball" : "";
    in.timestamp = 1700000000;
    return in;
}
}  // namespace

TEST_CASE("a swing carries the source, the destination and a full damage suffix", "[combatlog]") {
    const auto a = buildCombatLogEvent(hit(T::MELEE_DAMAGE));
    REQUIRE(a.size() == 17);
    CHECK(a[0] == "1700000000");
    CHECK(a[1] == "SWING_DAMAGE");
    CHECK(a[2] == "0x0000000000000001");
    CHECK(a[3] == "Alpha");
    CHECK(a[5] == "0xF130000000000002");
    CHECK(a[6] == "Orc");
    CHECK(a[8] == "42");   // amount
    CHECK(a[9] == "0");    // overkill, which the parser subtracts
}

TEST_CASE("a spell hit puts its prefix before the damage", "[combatlog]") {
    const auto a = buildCombatLogEvent(hit(T::SPELL_DAMAGE, 133));
    REQUIRE(a.size() == 20);
    CHECK(a[1] == "SPELL_DAMAGE");
    CHECK(a[8] == "133");
    CHECK(a[9] == "Fireball");
    CHECK(a[11] == "42");
}

TEST_CASE("a crit is flagged and an ordinary hit is not", "[combatlog]") {
    CHECK(buildCombatLogEvent(hit(T::CRIT_DAMAGE, 133))[17] == "1");
    CHECK(buildCombatLogEvent(hit(T::SPELL_DAMAGE, 133))[17] == kEventNil);
}

TEST_CASE("heals, periodic ticks and energize are the events the parser knows", "[combatlog]") {
    CHECK(buildCombatLogEvent(hit(T::HEAL, 1))[1] == "SPELL_HEAL");
    CHECK(buildCombatLogEvent(hit(T::PERIODIC_HEAL, 1))[1] == "SPELL_PERIODIC_HEAL");
    CHECK(buildCombatLogEvent(hit(T::PERIODIC_DAMAGE, 1))[1] == "SPELL_PERIODIC_DAMAGE");
    auto energize = hit(T::ENERGIZE, 1);
    energize.powerType = 3;
    const auto a = buildCombatLogEvent(energize);
    CHECK(a[1] == "SPELL_ENERGIZE");
    CHECK(a[11] == "42");   // amount
    CHECK(a[12] == "3");    // power type
}

TEST_CASE("a miss names how it missed, and a block says how much", "[combatlog]") {
    const auto dodge = buildCombatLogEvent(hit(T::DODGE));
    CHECK(dodge[1] == "SWING_MISSED");
    CHECK(dodge[8] == "DODGE");
    const auto block = buildCombatLogEvent(hit(T::BLOCK, 1));
    CHECK(block[1] == "SPELL_MISSED");
    CHECK(block.back() == "42");
}

TEST_CASE("the part of a hit that did not land is not a line of its own", "[combatlog]") {
    CHECK(buildCombatLogEvent(hit(T::ABSORB, 1)).empty());
    CHECK(buildCombatLogEvent(hit(T::RESIST, 1)).empty());
    auto whole = hit(T::RESIST, 1);
    whole.amount = 0;
    CHECK(buildCombatLogEvent(whole)[1] == "SPELL_MISSED");
}

TEST_CASE("experience and honour have no combat log line", "[combatlog]") {
    CHECK(buildCombatLogEvent(hit(T::XP_GAIN)).empty());
    CHECK(buildCombatLogEvent(hit(T::HONOR_GAIN)).empty());
}

TEST_CASE("environmental damage has no source and names its kind", "[combatlog]") {
    auto in = hit(T::ENVIRONMENTAL);
    in.srcGuid = 0;
    in.srcName.clear();
    in.powerType = 2;
    const auto a = buildCombatLogEvent(in);
    CHECK(a[1] == "ENVIRONMENTAL_DAMAGE");
    CHECK(a[2] == "0x0000000000000000");
    CHECK(a[3] == kEventNil);
    CHECK(a[8] == "Falling");
}

TEST_CASE("the player's own events are flagged as theirs", "[combatlog]") {
    const auto a = buildCombatLogEvent(hit(T::MELEE_DAMAGE));
    CHECK(a[4] == std::to_string(combat_log::kMine | combat_log::kFriendly |
                                 combat_log::kControlPlayer | combat_log::kTypePlayer));
    CHECK(a[7] == std::to_string(combat_log::kOutsider | combat_log::kHostile |
                                 combat_log::kControlNpc | combat_log::kTypeNpc));
}
