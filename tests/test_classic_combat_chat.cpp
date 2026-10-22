// The chat lines 1.12 writes for combat.
//
// The Combat Log tab shows whatever arrives on the events it is registered
// for, but the damage meters written for 1.12 parse each line back apart by
// matching it against the GlobalStrings pattern expected on that event. A
// line on the wrong event, or with its arguments in another order, reads
// fine in the tab and counts for nothing in any of them. These cases pin the
// event, the pattern and the order against 1.12.1's GlobalStrings.
#include <catch_amalgamated.hpp>
#include "game/classic_combat_chat.hpp"

using namespace wowee::game;
using T = CombatTextEntry;
using R = CombatRelation;

namespace {
ClassicCombatInput hit(T::Type type, R src, R dst, const char* spell = "") {
    ClassicCombatInput in;
    in.type = type;
    in.amount = 42;
    in.spellName = spell;
    in.srcName = "Alpha";
    in.dstName = "Orc";
    in.src = src;
    in.dst = dst;
    return in;
}
const std::string kSchoolFire = std::string(1, classic_combat::kGlobalArg) + "SPELL_SCHOOL2_CAP";
}  // namespace

TEST_CASE("the player's swing is a self hit naming only the target", "[classiccombat]") {
    // COMBATHITSELFOTHER = "You hit %s for %d."
    const auto m = buildClassicCombatMessage(hit(T::MELEE_DAMAGE, R::Self, R::Creature));
    CHECK(m.event == "CHAT_MSG_COMBAT_SELF_HITS");
    CHECK(m.pattern == "COMBATHITSELFOTHER");
    CHECK(m.args == std::vector<std::string>{"Orc", "42"});
}

TEST_CASE("a creature's swing at the player is filed by its target", "[classiccombat]") {
    // COMBATHITCRITOTHERSELF = "%s crits you for %d."
    const auto m = buildClassicCombatMessage(hit(T::CRIT_DAMAGE, R::Creature, R::Self));
    CHECK(m.event == "CHAT_MSG_COMBAT_CREATURE_VS_SELF_HITS");
    CHECK(m.pattern == "COMBATHITCRITOTHERSELF");
    CHECK(m.args == std::vector<std::string>{"Alpha", "42"});

    const auto pet = buildClassicCombatMessage(hit(T::MELEE_DAMAGE, R::Creature, R::Pet));
    CHECK(pet.event == "CHAT_MSG_COMBAT_CREATURE_VS_PARTY_HITS");
    CHECK(pet.pattern == "COMBATHITOTHEROTHER");
}

TEST_CASE("a spell with a school names it after the amount", "[classiccombat]") {
    // SPELLLOGSCHOOLSELFOTHER = "Your %s hits %s for %d %s damage."
    auto in = hit(T::SPELL_DAMAGE, R::Self, R::Creature, "Fireball");
    in.schoolMask = 0x4;
    const auto m = buildClassicCombatMessage(in);
    CHECK(m.event == "CHAT_MSG_SPELL_SELF_DAMAGE");
    CHECK(m.pattern == "SPELLLOGSCHOOLSELFOTHER");
    CHECK(m.args == std::vector<std::string>{"Fireball", "Orc", "42", kSchoolFire});
}

TEST_CASE("a physical spell leaves the school out", "[classiccombat]") {
    // SPELLLOGCRITOTHEROTHER = "%s's %s crits %s for %d."
    auto in = hit(T::CRIT_DAMAGE, R::Party, R::Creature, "Heroic Strike");
    in.schoolMask = 0x1;
    const auto m = buildClassicCombatMessage(in);
    CHECK(m.event == "CHAT_MSG_SPELL_PARTY_DAMAGE");
    CHECK(m.pattern == "SPELLLOGCRITOTHEROTHER");
    CHECK(m.args == std::vector<std::string>{"Alpha", "Heroic Strike", "Orc", "42"});
}

TEST_CASE("periodic damage leads with the sufferer and is filed by them", "[classiccombat]") {
    // PERIODICAURADAMAGESELFOTHER = "%s suffers %d %s damage from your %s."
    auto in = hit(T::PERIODIC_DAMAGE, R::Self, R::Creature, "Immolate");
    in.schoolMask = 0x4;
    const auto m = buildClassicCombatMessage(in);
    CHECK(m.event == "CHAT_MSG_SPELL_PERIODIC_CREATURE_DAMAGE");
    CHECK(m.pattern == "PERIODICAURADAMAGESELFOTHER");
    CHECK(m.args == std::vector<std::string>{"Orc", "42", kSchoolFire, "Immolate"});

    // PERIODICAURADAMAGEOTHERSELF = "You suffer %d %s damage from %s's %s."
    auto onMe = hit(T::PERIODIC_DAMAGE, R::Creature, R::Self, "Immolate");
    onMe.schoolMask = 0x4;
    const auto s = buildClassicCombatMessage(onMe);
    CHECK(s.event == "CHAT_MSG_SPELL_PERIODIC_SELF_DAMAGE");
    CHECK(s.args == std::vector<std::string>{"42", kSchoolFire, "Alpha", "Immolate"});
}

TEST_CASE("heals go to the caster's buff event", "[classiccombat]") {
    // HEALEDSELFSELF = "Your %s heals you for %d."
    const auto self = buildClassicCombatMessage(hit(T::HEAL, R::Self, R::Self, "Lesser Heal"));
    CHECK(self.event == "CHAT_MSG_SPELL_SELF_BUFF");
    CHECK(self.pattern == "HEALEDSELFSELF");
    CHECK(self.args == std::vector<std::string>{"Lesser Heal", "42"});

    // HEALEDCRITOTHERSELF = "%s's %s critically heals you for %d."
    const auto other = buildClassicCombatMessage(
        hit(T::CRIT_HEAL, R::FriendlyPlayer, R::Self, "Flash Heal"));
    CHECK(other.event == "CHAT_MSG_SPELL_FRIENDLYPLAYER_BUFF");
    CHECK(other.pattern == "HEALEDCRITOTHERSELF");
    CHECK(other.args == std::vector<std::string>{"Alpha", "Flash Heal", "42"});
}

TEST_CASE("a heal over time on the player drops the your", "[classiccombat]") {
    // PERIODICAURAHEALSELFSELF = "You gain %d health from %s."
    const auto m = buildClassicCombatMessage(
        hit(T::PERIODIC_HEAL, R::Self, R::Self, "Renew"));
    CHECK(m.event == "CHAT_MSG_SPELL_PERIODIC_SELF_BUFFS");
    CHECK(m.pattern == "PERIODICAURAHEALSELFSELF");
    CHECK(m.args == std::vector<std::string>{"42", "Renew"});
}

TEST_CASE("a swing that does not land is a miss event", "[classiccombat]") {
    // VSDODGESELFOTHER = "You attack. %s dodges."
    const auto m = buildClassicCombatMessage(hit(T::DODGE, R::Self, R::Creature));
    CHECK(m.event == "CHAT_MSG_COMBAT_SELF_MISSES");
    CHECK(m.pattern == "VSDODGESELFOTHER");
    CHECK(m.args == std::vector<std::string>{"Orc"});

    // MISSEDOTHERSELF = "%s misses you."
    const auto miss = buildClassicCombatMessage(hit(T::MISS, R::Creature, R::Self));
    CHECK(miss.event == "CHAT_MSG_COMBAT_CREATURE_VS_SELF_MISSES");
    CHECK(miss.pattern == "MISSEDOTHERSELF");
    CHECK(miss.args == std::vector<std::string>{"Alpha"});
}

TEST_CASE("a spell that does not land stays on the damage event", "[classiccombat]") {
    // SPELLRESISTOTHERSELF = "%s's %s was resisted."
    const auto m = buildClassicCombatMessage(
        hit(T::RESIST, R::Creature, R::Self, "Frostbolt"));
    CHECK(m.event == "CHAT_MSG_SPELL_CREATURE_VS_SELF_DAMAGE");
    CHECK(m.pattern == "SPELLRESISTOTHERSELF");
    CHECK(m.args == std::vector<std::string>{"Alpha", "Frostbolt"});

    // SPELLIMMUNESELFOTHER = "Your %s failed. %s is immune."
    const auto imm = buildClassicCombatMessage(hit(T::IMMUNE, R::Self, R::Creature, "Fear"));
    CHECK(imm.pattern == "SPELLIMMUNESELFOTHER");
    CHECK(imm.args == std::vector<std::string>{"Fear", "Orc"});
}

TEST_CASE("power gained and drained are buffs, with the power by name", "[classiccombat]") {
    const std::string rage = std::string(1, classic_combat::kGlobalArg) + "RAGE";
    // POWERGAINSELFSELF = "You gain %d %s from %s."
    auto gain = hit(T::ENERGIZE, R::Self, R::Self, "Bloodrage");
    gain.powerType = 1;
    const auto g = buildClassicCombatMessage(gain);
    CHECK(g.event == "CHAT_MSG_SPELL_SELF_BUFF");
    CHECK(g.pattern == "POWERGAINSELFSELF");
    CHECK(g.args == std::vector<std::string>{"42", rage, "Bloodrage"});

    // SPELLPOWERDRAINOTHERSELF = "%s's %s drains %d %s from you."
    auto drain = hit(T::POWER_DRAIN, R::Creature, R::Self, "Mana Burn");
    const auto d = buildClassicCombatMessage(drain);
    CHECK(d.event == "CHAT_MSG_SPELL_CREATURE_VS_SELF_BUFF");
    CHECK(d.pattern == "SPELLPOWERDRAINOTHERSELF");
    CHECK(d.args.size() == 4);
    CHECK(d.args[0] == "Alpha");
    CHECK(d.args[2] == "42");
}

TEST_CASE("falling is the sufferer's own hit", "[classiccombat]") {
    // VSENVIRONMENTALDAMAGE_FALLING_SELF = "You fall and lose %d health."
    auto in = hit(T::ENVIRONMENTAL, R::Creature, R::Self);
    in.powerType = 2;
    const auto m = buildClassicCombatMessage(in);
    CHECK(m.event == "CHAT_MSG_COMBAT_SELF_HITS");
    CHECK(m.pattern == "VSENVIRONMENTALDAMAGE_FALLING_SELF");
    CHECK(m.args == std::vector<std::string>{"42"});
}

TEST_CASE("experience, honour and procs are not combat lines", "[classiccombat]") {
    CHECK(buildClassicCombatMessage(hit(T::XP_GAIN, R::Self, R::Self)).event.empty());
    CHECK(buildClassicCombatMessage(hit(T::HONOR_GAIN, R::Self, R::Self)).event.empty());
    CHECK(buildClassicCombatMessage(hit(T::PROC_TRIGGER, R::Self, R::Self, "x")).event.empty());
}
