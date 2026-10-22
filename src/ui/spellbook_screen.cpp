#include "ui/spellbook_screen.hpp"
#include "ui/ui_texture_load.hpp"
#include "ui/ui_upload_budget.hpp"
#include "ui/ui_colors.hpp"
#include "ui/keybinding_manager.hpp"
#include "core/input.hpp"
#include "core/application.hpp"
#include "rendering/vk_context.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/dbc_loader.hpp"
#include "pipeline/dbc_layout.hpp"
#include "core/logger.hpp"
#include "pipeline/spell_icon_paths.hpp"
#include <algorithm>
#include <map>
#include <cctype>

namespace wowee { namespace ui {

void SpellbookScreen::loadSpellDBC(pipeline::AssetManager* assetManager) {
    if (dbcLoadAttempted) return;
    dbcLoadAttempted = true;

    if (!assetManager || !assetManager->isInitialized()) return;

    auto dbc = assetManager->loadDBC("Spell.dbc");
    if (!dbc || !dbc->isLoaded()) {
        LOG_WARNING("Spellbook: Could not load Spell.dbc");
        return;
    }

    uint32_t fieldCount = dbc->getFieldCount();
    // Classic 1.12 Spell.dbc has 148 fields (Tooltip at index 147), TBC has ~220+ (SchoolMask at 215), WotLK has 234.
    // Require at least 148 fields so all expansions can load spell names/icons via the DBC layout.
    if (fieldCount < 148) {
        LOG_WARNING("Spellbook: Spell.dbc has ", fieldCount, " fields, too few to load");
        return;
    }

    const auto* spellL = pipeline::getActiveDBCLayout() ? pipeline::getActiveDBCLayout()->getLayout("Spell") : nullptr;

    // Load SpellCastTimes.dbc: field 0=ID, field 1=Base(ms), field 2=PerLevel, field 3=Minimum
    std::unordered_map<uint32_t, uint32_t> castTimeMap;  // index → base ms
    auto castTimeDbc = assetManager->loadDBC("SpellCastTimes.dbc");
    if (castTimeDbc && castTimeDbc->isLoaded()) {
        for (uint32_t i = 0; i < castTimeDbc->getRecordCount(); ++i) {
            uint32_t id   = castTimeDbc->getUInt32(i, 0);
            int32_t  base = static_cast<int32_t>(castTimeDbc->getUInt32(i, 1));
            if (id > 0 && base > 0)
                castTimeMap[id] = static_cast<uint32_t>(base);
        }
    }

    // Load SpellRange.dbc.  Field layout differs by expansion:
    //   Classic 1.12:  0=ID, 1=MinRange, 2=MaxRange, 3=Flags, 4+=strings
    //   TBC / WotLK:   0=ID, 1=MinRangeFriendly, 2=MinRangeHostile,
    //                  3=MaxRangeFriendly, 4=MaxRangeHostile, 5=Flags, 6+=strings
    // The correct field is declared in each expansion's dbc_layouts.json.
    uint32_t spellRangeMaxField = 4;  // WotLK / TBC default: MaxRangeHostile
    const auto* spellRangeL = pipeline::getActiveDBCLayout()
                              ? pipeline::getActiveDBCLayout()->getLayout("SpellRange")
                              : nullptr;
    if (spellRangeL) {
        try { spellRangeMaxField = (*spellRangeL)["MaxRange"]; } catch (...) {}
    }
    std::unordered_map<uint32_t, float> rangeMap;  // index → max yards
    auto rangeDbc = assetManager->loadDBC("SpellRange.dbc");
    if (rangeDbc && rangeDbc->isLoaded()) {
        uint32_t rangeFieldCount = rangeDbc->getFieldCount();
        if (rangeFieldCount > spellRangeMaxField) {
            for (uint32_t i = 0; i < rangeDbc->getRecordCount(); ++i) {
                uint32_t id = rangeDbc->getUInt32(i, 0);
                float maxRange = rangeDbc->getFloat(i, spellRangeMaxField);
                if (id > 0 && maxRange > 0.0f)
                    rangeMap[id] = maxRange;
            }
        }
    }

    // schoolField / isSchoolEnum are declared before the lambda so the WotLK fallback path
    // can override them before the second tryLoad call.
    uint32_t schoolField_  = UINT32_MAX;
    bool     isSchoolEnum_ = false;

    auto tryLoad = [&](uint32_t idField, uint32_t attrField, uint32_t iconField,
                       uint32_t nameField, uint32_t rankField, uint32_t tooltipField,
                       uint32_t descriptionField,
                       uint32_t powerTypeField, uint32_t manaCostField,
                       uint32_t castTimeIndexField, uint32_t rangeIndexField,
                       uint32_t casterAuraStateField, uint32_t casterAuraStateNotField,
                       const char* label) {
        spellData.clear();
        uint32_t count = dbc->getRecordCount();
        const uint32_t fc = dbc->getFieldCount();
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t spellId = dbc->getUInt32(i, idField);
            if (spellId == 0) continue;

            SpellInfo info;
            info.spellId = spellId;
            info.attributes = dbc->getUInt32(i, attrField);
            info.iconId = dbc->getUInt32(i, iconField);
            info.name = dbc->getString(i, nameField);
            if (rankField < fc)    info.rank = dbc->getString(i, rankField);
            // Prefer the fuller Description (mentions e.g. the "well fed" food buff) and
            // fall back to the short Tooltip; $-tokens are resolved at render time.
            if (descriptionField < fc) info.description = dbc->getString(i, descriptionField);
            if (info.description.empty() && tooltipField < fc)
                info.description = dbc->getString(i, tooltipField);
            // Optional fields: only read if field index is valid for this DBC version
            if (powerTypeField < fc)   info.powerType = dbc->getUInt32(i, powerTypeField);
            if (manaCostField  < fc)   info.manaCost  = dbc->getUInt32(i, manaCostField);
            if (casterAuraStateField < fc)
                info.casterAuraState = dbc->getUInt32(i, casterAuraStateField);
            if (casterAuraStateNotField < fc)
                info.casterAuraStateNot = dbc->getUInt32(i, casterAuraStateNotField);
            if (castTimeIndexField < fc) {
                uint32_t ctIdx = dbc->getUInt32(i, castTimeIndexField);
                if (ctIdx > 0) {
                    auto ctIt = castTimeMap.find(ctIdx);
                    if (ctIt != castTimeMap.end()) info.castTimeMs = ctIt->second;
                }
            }
            if (rangeIndexField < fc) {
                uint32_t rangeIdx = dbc->getUInt32(i, rangeIndexField);
                if (rangeIdx > 0) {
                    auto rangeIt = rangeMap.find(rangeIdx);
                    if (rangeIt != rangeMap.end()) info.rangeIndex = static_cast<uint32_t>(rangeIt->second);
                }
            }
            if (schoolField_ < fc) {
                uint32_t raw = dbc->getUInt32(i, schoolField_);
                // Classic/Turtle use a 0-6 school enum; TBC/WotLK use a bitmask.
                // enum→mask: schoolEnum N maps to bit (1u << N), e.g. 0→1 (physical), 4→16 (frost).
                info.schoolMask = isSchoolEnum_ ? (raw <= 6 ? (1u << raw) : 0u) : raw;
            }

            if (!info.name.empty()) {
                spellData[spellId] = std::move(info);
            }
        }
        LOG_INFO("Spellbook: Loaded ", spellData.size(), " spells from Spell.dbc (", label, ")");
    };

    if (spellL) {
        // Default to UINT32_MAX for optional fields; tryLoad will skip them if >= fieldCount.
        // Avoids reading wrong data from expansion DBCs that lack these fields (e.g. Classic/TBC).
        uint32_t tooltipField      = UINT32_MAX;
        uint32_t descriptionField  = UINT32_MAX;
        uint32_t powerTypeField    = UINT32_MAX;
        uint32_t manaCostField     = UINT32_MAX;
        uint32_t castTimeIdxField  = UINT32_MAX;
        uint32_t rangeIdxField     = UINT32_MAX;
        uint32_t casterAuraStateField = UINT32_MAX;
        uint32_t casterAuraStateNotField = UINT32_MAX;
        try { tooltipField     = (*spellL)["Tooltip"]; } catch (...) {}
        try { descriptionField = (*spellL)["Description"]; } catch (...) {}
        try { powerTypeField   = (*spellL)["PowerType"]; } catch (...) {}
        try { manaCostField    = (*spellL)["ManaCost"]; } catch (...) {}
        // From the file's own shape rather than the expansion's name for it:
        // Classic and Turtle named this 15, which is RequiresSpellFocus.
        castTimeIdxField = pipeline::detectSpellTimingFields(dbc.get(), spellL).castingTimeIndex;
        try { rangeIdxField    = (*spellL)["RangeIndex"]; } catch (...) {}
        try { casterAuraStateField = (*spellL)["CasterAuraState"]; } catch (...) {}
        try { casterAuraStateNotField = (*spellL)["CasterAuraStateNot"]; } catch (...) {}
        // Try SchoolMask (TBC/WotLK bitmask) then SchoolEnum (Classic/Turtle 0-6 value)
        //
        // Asked for rather than read, so a 1.12 layout is not reported as
        // missing SchoolMask: it never had the column, and the warning that
        // came of it told people with a perfectly good extraction to go and
        // re-extract. Neither name is a real gap and still says so.
        schoolField_  = spellL->tryField("SchoolMask");
        isSchoolEnum_ = false;
        if (schoolField_ == UINT32_MAX) {
            schoolField_ = spellL->tryField("SchoolEnum");
            isSchoolEnum_ = schoolField_ != UINT32_MAX;
        }
        if (schoolField_ == UINT32_MAX) {
            pipeline::noteMissingLayoutField("Spell", "SchoolMask or SchoolEnum");
        }
        tryLoad((*spellL)["ID"], (*spellL)["Attributes"], (*spellL)["IconID"],
                (*spellL)["Name"], (*spellL)["Rank"], tooltipField, descriptionField,
                powerTypeField, manaCostField, castTimeIdxField, rangeIdxField,
                casterAuraStateField, casterAuraStateNotField,
                "expansion layout");
    }

    // If dbc_layouts.json was missing or its field names didn't match, retry with
    // hard-coded WotLK field indices as a safety net. fieldCount >= 200 distinguishes
    // WotLK (234 fields) from Classic (148) to avoid misreading shorter DBCs.
    if (spellData.empty() && fieldCount >= 200) {
        LOG_INFO("Spellbook: Retrying with WotLK field indices (DBC has ", fieldCount, " fields)");
        schoolField_  = 225;
        isSchoolEnum_ = false;
        tryLoad(0, 4, 133, 136, 153, 187, 170, 41, 42, 28, 46, 20, 22, "WotLK fallback");
    }

    dbcLoaded = !spellData.empty();
}

uint32_t SpellbookScreen::getSpellMaxRange(uint32_t spellId, pipeline::AssetManager* assetManager) {
    if (!dbcLoadAttempted) {
        loadSpellDBC(assetManager);
    }
    auto it = spellData.find(spellId);
    if (it != spellData.end()) return it->second.rangeIndex;
    return 0;
}

void SpellbookScreen::getSpellPowerInfo(uint32_t spellId, pipeline::AssetManager* assetManager,
                                        uint32_t& outCost, uint32_t& outPowerType) {
    outCost = 0;
    outPowerType = 0;
    if (!dbcLoadAttempted) {
        loadSpellDBC(assetManager);
    }
    auto it = spellData.find(spellId);
    if (it != spellData.end()) {
        outCost = it->second.manaCost;
        outPowerType = it->second.powerType;
    }
}

void SpellbookScreen::getSpellAuraStateInfo(uint32_t spellId, pipeline::AssetManager* assetManager,
                                             uint32_t& outRequired, uint32_t& outForbidden) {
    outRequired = 0;
    outForbidden = 0;
    if (!dbcLoadAttempted) {
        loadSpellDBC(assetManager);
    }
    auto it = spellData.find(spellId);
    if (it != spellData.end()) {
        outRequired = it->second.casterAuraState;
        outForbidden = it->second.casterAuraStateNot;
    }
}

}} // namespace wowee::ui
