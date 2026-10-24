#pragma once
#include "Common.h"
#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

constexpr unsigned MAX_EFFECT_INDEX = 3;
constexpr uint32 SPELL_EFFECT_ENCHANT_ITEM = 53, SPELL_EFFECT_LEARN_SPELL = 36;
constexpr uint32 ITEM_CLASS_ARMOR = 4, ITEM_QUALITY_UNCOMMON = 2;
constexpr unsigned PERM_ENCHANTMENT_SLOT = 0, DEFAULT_MAX_LEVEL = 60, SKILL_ENCHANTING = 333;
struct SpellEntry
{
    uint32 Id = 0;
    std::array<uint32, 3> Effect{}, EffectApplyAuraName{}, EffectMiscValue{}, EffectTriggerSpell{};
    std::array<int, 3> EffectBasePoints{};
    int EquippedItemClass = -1;
    uint32 EquippedItemSubClassMask = 0, EquippedItemInventoryTypeMask = 0;
};
struct SpellItemEnchantmentEntry
{
    std::array<uint32, 3> type{}, spellid{}, amount{};
};
struct ItemPrototype { unsigned Quality = 2, Class = 2, SubClass = 7, InventoryType = 13; };
struct Player
{
    unsigned level = 33, classId = 8, spec = 2;
    unsigned GetLevel() const { return level; }
    unsigned getClass() const { return classId; }
};
struct Item
{
    ItemPrototype proto;
    Player* owner = nullptr;
    unsigned slot = 15, enchant = 0;
    bool equipped = true, prototypePresent = true;
    const ItemPrototype* GetProto() const { return prototypePresent ? &proto : nullptr; }
    Player* GetOwner() const { return owner; }
    unsigned GetSlot() const { return slot; }
    bool IsEquipped() const { return equipped; }
    unsigned GetEnchantmentId(unsigned) const { return enchant; }
};
struct PlayerbotAI
{
    Player* bot;
    unsigned applications = 0;
    Player* GetBot() const { return bot; }
    void EnchantItemT(uint32, uint8, Item*);
};
struct EnchantTemplate { uint8 ClassId, SpecId; uint32 SpellId; uint8 SlotId; };
struct PlayerbotAIConfig { unsigned minEnchantingBotLevel = 10; };
extern PlayerbotAIConfig sPlayerbotAIConfig;
struct AiFactory { static unsigned GetPlayerSpecTab(Player* player) { return player->spec; } };
struct ServerFacade
{
    std::map<uint32, SpellEntry> spells;
    const SpellEntry* LookupSpellInfo(uint32 id) const
    {
        const auto found = spells.find(id);
        return found == spells.end() ? nullptr : &found->second;
    }
};
extern ServerFacade sServerFacade;
struct EnchantStore
{
    std::map<uint32, SpellItemEnchantmentEntry> entries;
    const SpellItemEnchantmentEntry* LookupEntry(uint32 id) const
    {
        const auto found = entries.find(id);
        return found == entries.end() ? nullptr : &found->second;
    }
};
extern EnchantStore sSpellItemEnchantmentStore;
struct SkillLineAbilityEntry
{
    uint32 skillId = SKILL_ENCHANTING;
    uint32 req_skill_value = 1;
    uint32 learnOnGetSkill = 0;
};
struct SpellMgr
{
    SkillLineAbilityEntry enchanting;
    SkillLineAbilityEntry starter{SKILL_ENCHANTING, 1, 1};
    SkillLineAbilityEntry nonstarterAutomatic{SKILL_ENCHANTING, 2, 1};
    std::multimap<uint32, const SkillLineAbilityEntry*> abilities;
    auto GetSkillLineAbilityMapBoundsBySpellId(uint32 spell) const { return abilities.equal_range(spell); }
};
extern SpellMgr sSpellMgr;
struct Field
{
    uint32 value;
    uint32 GetUInt32() const { return value; }
    uint8 GetUInt8() const { return uint8(value); }
};
struct QueryResult
{
    std::vector<std::vector<Field>> rows;
    std::size_t position = 0;
    Field* Fetch() { return rows[position].data(); }
    bool NextRow() { return ++position < rows.size(); }
};
struct Database
{
    std::vector<std::vector<Field>> templates, trainers, recipes;
    std::unique_ptr<QueryResult> PQuery(const char* sql) const
    {
        const std::string query(sql);
        auto result = std::make_unique<QueryResult>();
        if (query.find("ai_playerbot_enchants") != std::string::npos) result->rows = templates;
        else if (query.find("npc_trainer") != std::string::npos) result->rows = trainers;
        else if (query.find("item_template") != std::string::npos) result->rows = recipes;
        return result->rows.empty() ? nullptr : std::move(result);
    }
};
extern Database WorldDatabase;
