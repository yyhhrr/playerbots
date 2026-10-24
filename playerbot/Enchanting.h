#pragma once

#include "Common.h"

class Item;
class Player;
class PlayerbotAI;
struct SpellEntry;

namespace ai::enchanting
{
    bool IsPermanentEnchantmentSpell(const SpellEntry* spell);
    uint32 GetEnchantmentId(const SpellEntry* spell);
    // Zero means an item-based enhancement; UINT32_MAX means an unknown
    // profession requirement. Both are rejected below the allowed level.
    uint32 GetRequiredEnchantingSkill(uint32 spellId);
    // Apply the same limits to the caster and the equipment's owner.
    // Non-permanent spells are unaffected.
    bool CanApplyEnchantment(Player* caster, Item* item, const SpellEntry* spell);
    bool EnchantItem(PlayerbotAI* ai, Item* item, bool replaceExisting = false, bool enforceMinLevel = true);
}
