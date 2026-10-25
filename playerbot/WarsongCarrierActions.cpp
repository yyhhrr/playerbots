#include "playerbot/playerbot.h"
#include "WarsongCarrierActions.h"
#include "WarsongCarrierAbilities.h"
#include "WarsongTactics.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/PositionValue.h"

#include <algorithm>
#include <cmath>

namespace ai
{
namespace warsong
{
namespace
{
    uint32 KnownSpell(PlayerbotAI* ai, const char* name)
    {
        const uint32 spell = ai->GetAiObjectContext()->GetValue<uint32>("spell id", name)->Get();
        return spell && ai->GetBot()->HasSpell(spell) ? spell : 0;
    }

    bool Ready(PlayerbotAI* ai, const char* name)
    {
        Player* bot = ai->GetBot();
        const uint32 spell = KnownSpell(ai, name);
        const SpellEntry* entry = spell ? sServerFacade.LookupSpellInfo(spell) : nullptr;
        if (!entry || !bot->IsSpellReady(spell)) return false;
        const uint32 cost = Spell::CalculatePowerCost(entry, bot);
        if (entry->powerType == POWER_HEALTH) return bot->GetHealth() > cost;
        return entry->powerType < MAX_POWERS &&
            bot->GetPower(Powers(entry->powerType)) >= cost;
    }

    bool CanUse(PlayerbotAI* ai, const char* name)
    {
        return Ready(ai, name) && ai->CanCastSpell(KnownSpell(ai, name), ai->GetBot(), 0);
    }

    bool IsBear(PlayerbotAI* ai)
    {
        const ShapeshiftForm form = ai->GetBot()->GetShapeshiftForm();
        return form == FORM_DIREBEAR || form == FORM_BEAR;
    }

    const char* BearSpell(PlayerbotAI* ai)
    {
        return KnownSpell(ai, "dire bear form") ? "dire bear form" : "bear form";
    }

    CarrierKind Kind(Player* bot)
    {
        switch (bot->getClass())
        {
        case CLASS_DRUID: return CarrierKind::Druid;
        case CLASS_ROGUE: return CarrierKind::Rogue;
        case CLASS_MAGE: return CarrierKind::Mage;
        case CLASS_SHAMAN: return CarrierKind::Shaman;
        default: return CarrierKind::Other;
        }
    }

    bool HasShiftRemovableRoot(Player* bot)
    {
        // Unit::RemoveSpellsCausingAura(ROOT, except, true) preserves roots
        // without a mechanic, even though they still prevent movement.
        for (const Aura* aura : bot->GetAurasByType(SPELL_AURA_MOD_ROOT))
            if (GetAllSpellMechanicMask(aura->GetSpellProto()) != 0) return true;
        return false;
    }

    bool HasShiftRemovableSlow(Player* bot)
    {
        // Match Aura::HandleAuraModShapeshift rather than repeatedly shifting
        // against daze/encounter effects which the core deliberately preserves.
        for (const Aura* aura : bot->GetAurasByType(SPELL_AURA_MOD_DECREASE_SPEED))
        {
            const SpellEntry* entry = aura->GetSpellProto();
            const uint32 mechanics = GetAllSpellMechanicMask(entry);
            if (ShapeshiftRemovesSlow(mechanics != 0,
                (mechanics & MECHANIC_NOT_REMOVED_BY_SHAPESHIFT) != 0,
                entry->SpellIconID == 15 && entry->Dispel == 0 &&
                    (mechanics & convertEnumToFlag(MECHANIC_SNARE)) == 0)) return true;
        }
        return false;
    }

    bool BlinkDestination(PlayerbotAI* ai, float& angle)
    {
        Player* bot = ai->GetBot();
        const PositionEntry goal = ai->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get()["bg objective"];
        if (!goal.valueSet || goal.mapId != bot->GetMapId()) return false;
        const float distance = bot->GetDistance2d(goal.x, goal.y);
        angle = bot->GetAngle(goal.x, goal.y);
        const float x = bot->GetPositionX() + std::cos(angle) * 20.0f;
        const float y = bot->GetPositionY() + std::sin(angle) * 20.0f;
        float z = bot->GetPositionZ();
        bot->UpdateAllowedPositionZ(x, y, z);
        return SafeCarrierBlink(distance, z - bot->GetPositionZ(),
            bot->IsWithinLOS(x, y, z + bot->GetCollisionHeight(), true),
            !bot->IsFalling() && !ai->IsJumping() && !bot->IsInWater());
    }

    CarrierAbilitySituation Situation(PlayerbotAI* ai, bool formChangeAllowed)
    {
        CarrierAbilitySituation state;
        Player* bot = ai->GetBot();
        state.activeCarrier = IsCarrier(bot);
        if (!state.activeCarrier || bot->IsNonMeleeSpellCasted(false)) return {};
        state.kind = Kind(bot);
        state.formChangeAllowed = formChangeAllowed;
        const PositionEntry goal = ai->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get()["bg objective"];
        if (goal.valueSet && goal.mapId == bot->GetMapId())
            state.objectiveDistance = bot->GetDistance2d(goal.x, goal.y);
        state.healthPercent = bot->GetMaxHealth() ? 100.0f * bot->GetHealth() / bot->GetMaxHealth() : 0.0f;
        state.rooted = bot->hasUnitState(UNIT_STAT_ROOT) || bot->HasAuraType(SPELL_AURA_MOD_ROOT);
        state.rootCanBeShifted = HasShiftRemovableRoot(bot);
        state.slowed = HasShiftRemovableSlow(bot);
        for (Unit* attacker : bot->getAttackers())
        {
            if (!attacker || !attacker->IsAlive() || attacker->GetVictim() != bot ||
                !sServerFacade.IsWithinLOSInMap(bot, attacker)) continue;
            const float distance = bot->GetDistance(attacker);
            if (distance <= 35.0f) state.pursued = true;
            if (distance <= 15.0f) state.pressured = true;
        }
        state.outdoors = bot->GetTerrain()->IsOutdoors(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        switch (state.kind)
        {
        case CarrierKind::Druid:
            // The native form field is authoritative even if an aura-name
            // lookup is stale or localized. Never request the active form again.
            state.inCat = bot->GetShapeshiftForm() == FORM_CAT;
            state.inBear = IsBear(ai);
            state.inTravel = bot->GetShapeshiftForm() == FORM_TRAVEL;
            state.dashActive = ai->HasAura("dash", bot);
            state.barkskinActive = ai->HasAura("barkskin", bot);
            state.catReady = CanUse(ai, "cat form");
            state.travelReady = state.outdoors && CanUse(ai, "travel form");
            state.bearReady = CanUse(ai, BearSpell(ai));
            state.dashReady = state.inCat ? CanUse(ai, "dash") : Ready(ai, "dash");
            state.barkskinReady = CanUse(ai, "barkskin");
            break;
        case CarrierKind::Rogue:
            state.sprintReady = CanUse(ai, "sprint");
            state.sprintActive = ai->HasAura("sprint", bot);
            break;
        case CarrierKind::Mage:
        {
            float angle = 0.0f;
            state.blinkReady = CanUse(ai, "blink");
            state.safeBlink = BlinkDestination(ai, angle);
            break;
        }
        case CarrierKind::Shaman:
        {
            state.inWolf = bot->GetShapeshiftForm() == FORM_GHOSTWOLF;
            state.wolfReady = CanUse(ai, "ghost wolf");
            const SpellEntry* entry = sServerFacade.LookupSpellInfo(KnownSpell(ai, "ghost wolf"));
            state.wolfInstant = entry && GetSpellCastTime(entry, bot) == 0;
            break;
        }
        default:
            break;
        }
        return state;
    }

    const char* SpellName(CarrierAbility ability, PlayerbotAI* ai)
    {
        switch (ability)
        {
        case CarrierAbility::TravelForm: return "travel form";
        case CarrierAbility::CatForm: return "cat form";
        case CarrierAbility::BearForm: return BearSpell(ai);
        case CarrierAbility::Dash: return "dash";
        case CarrierAbility::Barkskin: return "barkskin";
        case CarrierAbility::Sprint: return "sprint";
        case CarrierAbility::Blink: return "blink";
        case CarrierAbility::GhostWolf: return "ghost wolf";
        default: return nullptr;
        }
    }
}

CarrierAbilityState InspectCarrierAbilities(PlayerbotAI* ai)
{
    CarrierAbilityState state;
    if (!ai || !ai->GetBot()) return state;
    auto hasShield = [ai]()
    {
        const Item* shield = ai->GetBot()->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
        return shield && shield->GetProto()->InventoryType == INVTYPE_SHIELD;
    };
    switch (ai->GetBot()->getClass())
    {
    case CLASS_DRUID:
        state.mobilityReady = Ready(ai, "travel form") || (Ready(ai, "cat form") && Ready(ai, "dash"));
        state.defenseReady = Ready(ai, BearSpell(ai)) || Ready(ai, "barkskin");
        break;
    case CLASS_ROGUE:
        state.mobilityReady = Ready(ai, "sprint");
        state.defenseReady = Ready(ai, "evasion");
        break;
    case CLASS_MAGE:
        state.mobilityReady = Ready(ai, "blink");
        state.defenseReady = Ready(ai, "ice barrier") || Ready(ai, "frost nova");
        break;
    case CLASS_SHAMAN:
        state.mobilityReady = Ready(ai, "ghost wolf");
        break;
    case CLASS_WARRIOR:
        state.defenseReady = Ready(ai, "last stand") || (hasShield() && Ready(ai, "shield wall"));
        break;
    case CLASS_PALADIN:
        // Immunity cooldowns deliberately do not contribute: their use can
        // drop the flag. Holy Shield also needs an actually equipped shield.
        state.defenseReady = Ready(ai, "lay on hands") || (hasShield() && Ready(ai, "holy shield"));
        break;
    default:
        break;
    }
    return state;
}
}

bool WarsongCarrierMobilityAction::isUseful()
{
    const bool allowShift = !changedForm || uint32(WorldTimer::getMSTime() - lastFormChange) >= 4000;
    return warsong::ChooseCarrierAbility(warsong::Situation(ai, allowShift)) != warsong::CarrierAbility::None;
}

bool WarsongCarrierMobilityAction::Execute(Event& event)
{
    const bool allowShift = !changedForm || uint32(WorldTimer::getMSTime() - lastFormChange) >= 4000;
    const warsong::CarrierAbility choice = warsong::ChooseCarrierAbility(warsong::Situation(ai, allowShift));
    const char* name = warsong::SpellName(choice, ai);
    if (!name || !warsong::CanUse(ai, name)) return false;
    if (choice == warsong::CarrierAbility::Blink)
    {
        float angle = 0.0f;
        if (!warsong::BlinkDestination(ai, angle)) return false;
        sServerFacade.SetFacingTo(bot, angle, true);
    }
    const uint32 id = warsong::KnownSpell(ai, name);
    const SpellEntry* entry = sServerFacade.LookupSpellInfo(id);
    if (!entry) return false;
    const uint32 castTime = GetSpellCastTime(entry, bot);
    if (castTime > 0) ai->StopMoving();
    uint32 spellDuration = 0;
    const bool result = ai->CastSpell(id, bot, nullptr, false, &spellDuration);
    if (!result) return false;
    const bool formChange = choice == warsong::CarrierAbility::TravelForm || choice == warsong::CarrierAbility::CatForm ||
        choice == warsong::CarrierAbility::BearForm;
    if (formChange && castTime == 0)
    {
        // Instant self-shapeshifts apply their native form during SpellStart.
        // A failed/no-op cast must not consume the carrier's movement update.
        const ShapeshiftForm form = bot->GetShapeshiftForm();
        const bool applied = (choice == warsong::CarrierAbility::TravelForm && form == FORM_TRAVEL) ||
            (choice == warsong::CarrierAbility::CatForm && form == FORM_CAT) ||
            (choice == warsong::CarrierAbility::BearForm && (form == FORM_BEAR || form == FORM_DIREBEAR));
        if (!applied) return false;
    }
    SetDuration(std::max(spellDuration, uint32(sPlayerbotAIConfig.reactDelay)));
    if (formChange)
    {
        changedForm = true;
        lastFormChange = WorldTimer::getMSTime();
    }
    return true;
}
}
