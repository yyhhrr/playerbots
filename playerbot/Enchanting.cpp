#include "playerbot/playerbot.h"
#include "Enchanting.h"
#include "EnchantingPolicy.h"
#include "PlayerbotFactory.h"
#include "PlayerbotAIConfig.h"
#include "AiFactory.h"
#include "ServerFacade.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace ai::enchanting
{
    namespace
    {
        bool IsPermanentEffect(uint32 effect)
        {
            return effect == SPELL_EFFECT_ENCHANT_ITEM
#ifdef MANGOSBOT_TWO
                || effect == SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC
#endif
                ;
        }

        struct Catalog
        {
            std::vector<EnchantTemplate> templates;
            std::map<uint32, uint32> requirements;

            void AddRequirement(uint32 spellId, uint32 required, std::set<uint32>& visited)
            {
                if (!spellId || visited.size() >= 16 || !visited.insert(spellId).second)
                    return;
                const SpellEntry* spell = sServerFacade.LookupSpellInfo(spellId);
                if (!spell)
                    return;
                if (IsPermanentEnchantmentSpell(spell))
                {
                    // Skill 0 in a trainer entry means the first recipe, not
                    // permission to ignore the character's skill ceiling.
                    required = std::max(uint32(1), required);
                    auto [entry, inserted] = requirements.emplace(spellId, required);
                    if (!inserted)
                        entry->second = std::min(entry->second, required);
                }
                for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                    if (spell->Effect[i] == SPELL_EFFECT_LEARN_SPELL)
                        AddRequirement(spell->EffectTriggerSpell[i], required, visited);
            }

            Catalog()
            {
                if (auto rows = WorldDatabase.PQuery("SELECT class, spec, spellid, slotid FROM ai_playerbot_enchants"))
                {
                    do
                    {
                        Field* fields = rows->Fetch();
                        templates.push_back({ fields[0].GetUInt8(), fields[1].GetUInt8(),
                            fields[2].GetUInt32(), fields[3].GetUInt8() });
                    } while (rows->NextRow());
                }

                // Classic SkillLineAbility.req_skill_value is often 1 even for
                // recipes requiring 300. Its min/max fields are skill-up
                // thresholds, so obtain actual learning requirements instead.
                if (auto rows = WorldDatabase.PQuery(
                    "SELECT spell, reqskillvalue FROM npc_trainer WHERE reqskill = 333 "
                    "UNION ALL SELECT spell, reqskillvalue FROM npc_trainer_template WHERE reqskill = 333"))
                {
                    do
                    {
                        Field* fields = rows->Fetch();
                        std::set<uint32> visited;
                        AddRequirement(fields[0].GetUInt32(), fields[1].GetUInt32(), visited);
                    } while (rows->NextRow());
                }
                if (auto rows = WorldDatabase.PQuery(
                    "SELECT RequiredSkillRank, spellid_1, spellid_2, spellid_3, spellid_4, spellid_5 "
                    "FROM item_template WHERE RequiredSkill = 333"))
                {
                    do
                    {
                        Field* fields = rows->Fetch();
                        for (uint32 i = 1; i <= 5; ++i)
                        {
                            std::set<uint32> visited;
                            AddRequirement(fields[i].GetUInt32(), fields[0].GetUInt32(), visited);
                        }
                    } while (rows->NextRow());
                }
            }
        };

        const Catalog& GetCatalog()
        {
            static const Catalog catalog;
            return catalog;
        }

        bool SamePassiveEffects(uint32 leftId, uint32 rightId)
        {
            if (leftId == rightId)
                return true;
            const SpellEntry* left = sServerFacade.LookupSpellInfo(leftId);
            const SpellEntry* right = sServerFacade.LookupSpellInfo(rightId);
            if (!left || !right)
                return false;
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (left->Effect[i] != right->Effect[i] ||
                    left->EffectApplyAuraName[i] != right->EffectApplyAuraName[i] ||
                    left->EffectMiscValue[i] != right->EffectMiscValue[i])
                    return false;
            return true;
        }

        bool SameEnchantFamily(const SpellItemEnchantmentEntry* desired,
            const SpellItemEnchantmentEntry* candidate)
        {
            if (!desired || !candidate)
                return false;
            for (uint32 i = 0; i < 3; ++i)
            {
                if (desired->type[i] != candidate->type[i])
                    return false;
                if (!desired->type[i])
                    continue;
                if (desired->type[i] == 3) // Equipped passive spell; permit its lower ranks.
                {
                    if (!SamePassiveEffects(desired->spellid[i], candidate->spellid[i]))
                        return false;
                }
                else if (desired->spellid[i] != candidate->spellid[i])
                    return false; // In particular, never replace Crusader with an unrelated proc.
            }
            return true;
        }

        uint64 EnchantStrength(const SpellItemEnchantmentEntry* enchant)
        {
            uint64 strength = 0;
            if (!enchant)
                return strength;
            for (uint32 i = 0; i < 3; ++i)
            {
                strength += enchant->amount[i];
                if (enchant->type[i] == 3)
                    if (const SpellEntry* passive = sServerFacade.LookupSpellInfo(enchant->spellid[i]))
                        for (uint32 j = 0; j < MAX_EFFECT_INDEX; ++j)
                            if (passive->Effect[j] && passive->EffectBasePoints[j] >= 0)
                                strength += uint32(passive->EffectBasePoints[j]) + 1;
            }
            return strength;
        }

        uint32 SelectSpell(Player* bot, Item* item, uint32 preferredId)
        {
            const SpellEntry* preferred = sServerFacade.LookupSpellInfo(preferredId);
            if (!preferred)
                return 0;
            if (CanApplyEnchantment(bot, item, preferred))
                return preferredId;
            if (bot->GetLevel() >= 60 || IsMaxLevelOnlySlot(item->GetProto()->InventoryType))
                return 0;

            const auto* desiredEnchant = sSpellItemEnchantmentStore.LookupEntry(GetEnchantmentId(preferred));
            const uint64 maximumStrength = EnchantStrength(desiredEnchant);
            uint32 selected = 0, selectedRequirement = 0;
            uint64 selectedStrength = 0;
            for (const auto& [spellId, requirement] : GetCatalog().requirements)
            {
                const SpellEntry* candidate = sServerFacade.LookupSpellInfo(spellId);
                if (!candidate || !CanApplyEnchantment(bot, item, candidate))
                    continue;
                const auto* candidateEnchant = sSpellItemEnchantmentStore.LookupEntry(GetEnchantmentId(candidate));
                if (!SameEnchantFamily(desiredEnchant, candidateEnchant))
                    continue;
                const uint64 strength = EnchantStrength(candidateEnchant);
                if (strength > maximumStrength ||
                    (selected && (strength < selectedStrength ||
                        (strength == selectedStrength && requirement <= selectedRequirement))))
                    continue;
                selected = spellId;
                selectedStrength = strength;
                selectedRequirement = requirement;
            }
            return selected;
        }
    }

    bool IsPermanentEnchantmentSpell(const SpellEntry* spell)
    {
        if (spell)
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (IsPermanentEffect(spell->Effect[i]))
                    return true;
        return false;
    }

    uint32 GetEnchantmentId(const SpellEntry* spell)
    {
        if (spell)
            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
                if (IsPermanentEffect(spell->Effect[i]))
                    return spell->EffectMiscValue[i];
        return 0;
    }

    uint32 GetRequiredEnchantingSkill(uint32 spellId)
    {
        const auto& requirements = GetCatalog().requirements;
        const auto found = requirements.find(spellId);
        if (found != requirements.end())
            return found->second;
        const auto bounds = sSpellMgr.GetSkillLineAbilityMapBoundsBySpellId(spellId);
        for (auto it = bounds.first; it != bounds.second; ++it)
            if (it->second && it->second->skillId == SKILL_ENCHANTING)
                return UnknownRequiredSkill;
        return 0;
    }

    bool CanApplyEnchantment(Player* caster, Item* item, const SpellEntry* spell)
    {
        if (!IsPermanentEnchantmentSpell(spell))
            return true;
        if (!caster || !item || !item->GetProto() || !item->GetOwner())
            return false;
        const ItemPrototype* proto = item->GetProto();
        const uint32 requiredSkill = GetRequiredEnchantingSkill(spell->Id);
        const auto allowedFor = [&](Player* player)
        {
            return IsEnchantmentAllowed(player->GetLevel(), DEFAULT_MAX_LEVEL, proto->Quality,
                proto->InventoryType, requiredSkill);
        };
        if (!allowedFor(caster) || !allowedFor(item->GetOwner()))
            return false;

        // Preserve the core's explicit workaround for this incorrect classic DBC entry.
        if (spell->Id == 13419 && proto->Class == ITEM_CLASS_ARMOR)
            return true;
        return MatchesItemRequirements(spell->EquippedItemClass, spell->EquippedItemSubClassMask,
            spell->EquippedItemInventoryTypeMask, proto->Class, proto->SubClass, proto->InventoryType);
    }

    bool EnchantItem(PlayerbotAI* ai, Item* item, bool replaceExisting, bool enforceMinLevel)
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        if (!bot || !item || item->GetOwner() != bot || !item->IsEquipped() ||
            item->GetProto()->Quality < ITEM_QUALITY_UNCOMMON ||
            (enforceMinLevel && bot->GetLevel() < sPlayerbotAIConfig.minEnchantingBotLevel) ||
            (!replaceExisting && item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT)))
            return false;
        const uint8 spec = uint8(bot->getClass() * 10 + AiFactory::GetPlayerSpecTab(bot));
        for (const EnchantTemplate& entry : GetCatalog().templates)
        {
            if (entry.ClassId != bot->getClass() || entry.SpecId != spec || entry.SlotId != item->GetSlot())
                continue;
            const uint32 spellId = SelectSpell(bot, item, entry.SpellId);
            if (!spellId)
                continue;
            const uint32 enchantId = GetEnchantmentId(sServerFacade.LookupSpellInfo(spellId));
            ai->EnchantItemT(spellId, entry.SlotId, item);
            return enchantId != 0 && item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT) == enchantId;
        }
        return false;
    }
}
