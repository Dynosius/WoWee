// The damage meter's arithmetic, kept apart from the panel that draws it.
//
// The case that matters most is the full log: it holds the last 500 entries,
// and a meter reading it by index stopped counting once it was full, so every
// fight after the first five hundred events of a session read as nothing.
#include <catch_amalgamated.hpp>
#include "game/damage_meter.hpp"

using namespace wowee::game;
using T = CombatTextEntry;

namespace {
CombatLogEntry hit(int32_t amount, T::Type type = T::MELEE_DAMAGE, bool mine = true) {
    CombatLogEntry e;
    e.type = type;
    e.amount = amount;
    e.isPlayerSource = mine;
    return e;
}
}  // namespace

TEST_CASE("a fight's totals count only the player's own damage and healing", "[dps]") {
    DamageMeter meter;
    std::deque<CombatLogEntry> log;
    meter.update(true, 0.0f, log, 0, {});
    log.push_back(hit(100));
    log.push_back(hit(50, T::HEAL));
    log.push_back(hit(999, T::MELEE_DAMAGE, false));
    const auto r = meter.update(true, 10.0f, log, 3, {});
    CHECK(r.encounterDps == Catch::Approx(10.0f));
    CHECK(r.encounterHps == Catch::Approx(5.0f));
    CHECK(r.show);
}

TEST_CASE("a full log still counts what arrives", "[dps]") {
    DamageMeter meter;
    std::deque<CombatLogEntry> log(500, hit(1, T::MELEE_DAMAGE, false));
    uint64_t appended = 500;
    meter.update(true, 0.0f, log, appended, {});
    log.pop_front();
    log.push_back(hit(200));
    ++appended;
    const auto r = meter.update(true, 2.0f, log, appended, {});
    CHECK(r.encounterDps == Catch::Approx(100.0f));
}

TEST_CASE("a new fight starts its totals over", "[dps]") {
    DamageMeter meter;
    std::deque<CombatLogEntry> log;
    meter.update(true, 0.0f, log, 0, {});
    log.push_back(hit(100));
    meter.update(true, 1.0f, log, 1, {});
    meter.update(false, 1.0f, log, 1, {});
    log.push_back(hit(30));
    const auto r = meter.update(true, 0.0f, log, 2, {});
    CHECK(r.encounterDps == 0.0f);
}

TEST_CASE("nothing to report hides the meter", "[dps]") {
    DamageMeter meter;
    CHECK_FALSE(meter.update(false, 1.0f, {}, 0, {}).show);
}
