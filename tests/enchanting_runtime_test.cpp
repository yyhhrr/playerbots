#include "playerbot/playerbot.h"
#include "Enchanting.h"
#include "EnchantingPolicy.h"

#include <cstdlib>
#include <iostream>

PlayerbotAIConfig sPlayerbotAIConfig;
ServerFacade sServerFacade;
EnchantStore sSpellItemEnchantmentStore;
SpellMgr sSpellMgr;
Database WorldDatabase;

void PlayerbotAI::EnchantItemT(uint32 spellId, uint8 slot, Item* item)
{
    ++applications;
    if (item->slot == slot)
        item->enchant = ai::enchanting::GetEnchantmentId(sServerFacade.LookupSpellInfo(spellId));
}

namespace
{
    unsigned checks = 0;
    void Check(bool passed, const std::string& reason)
    {
        ++checks;
        if (!passed) { std::cerr << "FAILED: " << reason << '\n'; std::exit(EXIT_FAILURE); }
    }
    void Spell(uint32 spellId, uint32 enchantId, int itemClass = 2, uint32 subclassMask = 1u << 7, uint32 inventoryMask = 0)
    {
        auto& spell = sServerFacade.spells[spellId];
        spell.Id = spellId; spell.Effect[0] = SPELL_EFFECT_ENCHANT_ITEM; spell.EffectMiscValue[0] = enchantId;
        spell.EquippedItemClass = itemClass; spell.EquippedItemSubClassMask = subclassMask;
        spell.EquippedItemInventoryTypeMask = inventoryMask;
        sSpellMgr.abilities.emplace(spellId, &sSpellMgr.enchanting);
    }
    void Stat(uint32 spellId, uint32 enchantId, uint32 amount, uint32 required, uint32 stat = 5)
    {
        Spell(spellId, enchantId);
        auto& enchant = sSpellItemEnchantmentStore.entries[enchantId];
        enchant.type[0] = 5; enchant.spellid[0] = stat; enchant.amount[0] = amount;
        WorldDatabase.trainers.push_back({{spellId}, {required}});
    }
    void Initialize()
    {
        WorldDatabase.templates = {{{8}, {82}, {5000}, {15}}, {{1}, {11}, {20034}, {15}}, {{8}, {82}, {7000}, {8}}};
        Stat(5000, 1000, 30, 300);
        Stat(5001, 1001, 10, 150);
        Stat(5002, 1002, 20, 200);
        Stat(5003, 1003, 16, 160);
        Stat(5004, 1004, 99, 100, 6); // An unrelated attribute must never become the fallback.
        Stat(5005, 1005, 35, 160); // A weaker template must not grant a stronger enchant.
        Spell(5006, 1006); // Profession recipe with missing learning metadata.
        auto& wrapper = sServerFacade.spells[9000];
        wrapper.Id = 9000; wrapper.Effect[0] = SPELL_EFFECT_LEARN_SPELL; wrapper.EffectTriggerSpell[0] = 5006;
        WorldDatabase.recipes.push_back({{165}, {9000}, {0}, {0}, {0}, {0}});
        sSpellItemEnchantmentStore.entries[1006] = {{5, 0, 0}, {5, 0, 0}, {18, 0, 0}};
        Spell(5007, 1007);
        WorldDatabase.trainers.push_back({{5007}, {0}}); // First trainer recipe normalizes zero to one.
        sSpellItemEnchantmentStore.entries[1007] = {{5, 0, 0}, {5, 0, 0}, {1, 0, 0}};
        Spell(5010, 1010); // No trainer or recipe entry: remain unknown below sixty.
        // Classic's automatically learned first bracer enchant has no trainer
        // row. Non-starter DBC requirement=1 is not authoritative metadata.
        Spell(7418, 41, 4, 0, 1u << 9);
        sSpellMgr.abilities.erase(7418);
        sSpellMgr.abilities.emplace(7418, &sSpellMgr.starter);
        Spell(7419, 42, 4, 0, 1u << 9);
        sSpellMgr.abilities.erase(7419);
        sSpellMgr.abilities.emplace(7419, &sSpellMgr.nonstarterAutomatic);

        Spell(20034, 1900); // A proc family has no valid lower-rank recipe in this fixture.
        WorldDatabase.trainers.push_back({{20034}, {300}});
        sSpellItemEnchantmentStore.entries[1900] = {{1, 0, 0}, {20007, 0, 0}, {100, 0, 0}};
        Spell(20035, 1901);
        WorldDatabase.trainers.push_back({{20035}, {75}});
        sSpellItemEnchantmentStore.entries[1901] = {{1, 0, 0}, {20008, 0, 0}, {100, 0, 0}};

        for (auto [spellId, enchantId, passiveId, power, required, aura] : {
            std::array<uint32, 6>{7000, 2100, 7100, 30, 300, 13},
            std::array<uint32, 6>{7001, 2101, 7101, 10, 150, 13},
            std::array<uint32, 6>{7002, 2102, 7102, 50, 100, 14}})
        {
            Spell(spellId, enchantId, 4, 0, 1u << 9);
            WorldDatabase.trainers.push_back({{spellId}, {required}});
            sSpellItemEnchantmentStore.entries[enchantId] = {{3, 0, 0}, {passiveId, 0, 0}, {0, 0, 0}};
            auto& passive = sServerFacade.spells[passiveId];
            passive.Id = passiveId; passive.Effect[0] = 6; passive.EffectApplyAuraName[0] = aura;
            passive.EffectMiscValue[0] = 1; passive.EffectBasePoints[0] = int(power) - 1;
        }
        Spell(6000, 2200, 4, 0, 0); // Item enhancement rather than an enchanting recipe.
        sSpellMgr.abilities.erase(6000);
        sSpellItemEnchantmentStore.entries[2200] = {{5, 0, 0}, {5, 0, 0}, {5, 0, 0}};
        auto& secondEffect = sServerFacade.spells[6001];
        secondEffect = sServerFacade.spells.at(6000); secondEffect.Id = 6001;
        secondEffect.Effect[0] = 0; secondEffect.EffectMiscValue[0] = 0;
        secondEffect.Effect[1] = SPELL_EFFECT_ENCHANT_ITEM; secondEffect.EffectMiscValue[1] = 2200;
        auto& temporary = sServerFacade.spells[8000];
        temporary.Id = 8000; temporary.Effect[0] = 54;
    }
}

int main()
{
    using namespace ai::enchanting;
    Initialize();
    Player mage;
    PlayerbotAI ai{&mage};
    Item sword; sword.owner = &mage;
    Check(GetRequiredEnchantingSkill(5000) == 300, "the true trainer skill requirement is preserved");
    Check(GetRequiredEnchantingSkill(5006) == 165, "recipe learning wrappers provide their real skill requirement");
    Check(GetRequiredEnchantingSkill(5007) == 1, "a zero trainer requirement denotes the first recipe");
    Check(GetRequiredEnchantingSkill(5010) == UnknownRequiredSkill,
        "a non-automatic DBC requirement of one does not waive missing profession metadata");
    Check(GetRequiredEnchantingSkill(7418) == 1,
        "the actual automatically learned starter enchant is usable without a trainer or recipe row");
    Check(GetRequiredEnchantingSkill(7419) == UnknownRequiredSkill,
        "an automatic ability with a requirement above one is not mistaken for the first enchanting recipe");
    Player novice; novice.level = 1;
    Item noviceBracers; noviceBracers.owner = &novice; noviceBracers.proto = {2, 4, 1, 9}; noviceBracers.slot = 8;
    Check(CanApplyEnchantment(&novice, &noviceBracers, sServerFacade.LookupSpellInfo(7418)),
        "a level-one recipient with green bracers can receive the explicitly known skill-one starter enchant");
    Check(GetRequiredEnchantingSkill(6000) == 0, "an item enhancement is distinguished from a profession recipe");
    Check(!CanApplyEnchantment(&mage, &sword, sServerFacade.LookupSpellInfo(5000)), "the 300-skill preferred enchant is unavailable to a level thirty-three mage");
    Check(CanApplyEnchantment(&mage, &sword, sServerFacade.LookupSpellInfo(5003)), "a zero inventory mask permits the appropriate sword enchant");
    Check(EnchantItem(&ai, &sword) && sword.enchant == 1006, "automatic fallback chooses the strongest allowed same-family recipe");
    Check(ai.applications == 1, "only the chosen fallback is applied");
    Check(!EnchantItem(&ai, &sword) && sword.enchant == 1006 && ai.applications == 1,
        "automatic enchanting preserves an existing permanent enchant");
    mage.level = 60;
    Check(EnchantItem(&ai, &sword, true) && sword.enchant == 1000,
        "an explicit template refresh can replace the fallback with the preferred enchant at sixty");

    sword.enchant = 0; sword.proto.Quality = 1;
    Check(!EnchantItem(&ai, &sword) && !CanApplyEnchantment(&mage, &sword, sServerFacade.LookupSpellInfo(5000)),
        "both automatic selection and direct application reject white items");
    sword.proto.Quality = 2; sword.proto.Class = 4;
    Check(!CanApplyEnchantment(&mage, &sword, sServerFacade.LookupSpellInfo(5000)), "the zero inventory mask cannot bypass the weapon item class");
    sword.proto.Class = 2; sword.proto.SubClass = 8;
    Check(!CanApplyEnchantment(&mage, &sword, sServerFacade.LookupSpellInfo(5000)), "the weapon subclass restriction remains effective");
    sword.proto.SubClass = 7;

    Player owner;
    sword.owner = &owner;
    Check(!CanApplyEnchantment(&mage, &sword, sServerFacade.LookupSpellInfo(5000)), "a high-level caster cannot bypass a lower-level recipient skill ceiling");
    owner.level = 60; mage.level = 33;
    Check(!CanApplyEnchantment(&mage, &sword, sServerFacade.LookupSpellInfo(5000)), "a high-level recipient cannot bypass a lower-level caster skill ceiling");
    Check(!EnchantItem(&ai, &sword), "automatic template application cannot modify another owner's equipment");
    sword.owner = &mage;
    Check(!CanApplyEnchantment(nullptr, &sword, sServerFacade.LookupSpellInfo(5003)), "a missing permanent-enchant caster is rejected");
    Check(!CanApplyEnchantment(&mage, nullptr, sServerFacade.LookupSpellInfo(5003)), "a missing permanent-enchant item target is rejected");
    Check(CanApplyEnchantment(nullptr, nullptr, sServerFacade.LookupSpellInfo(8000)), "temporary weapon enhancements are unaffected");
    Check(IsPermanentEnchantmentSpell(sServerFacade.LookupSpellInfo(6001)) && GetEnchantmentId(sServerFacade.LookupSpellInfo(6001)) == 2200,
        "permanent enchantments in a later spell effect are detected");

    Item armor; armor.owner = &mage; armor.proto = {2, 4, 1, 1}; armor.slot = 27;
    mage.level = 59;
    for (unsigned inventoryType : {1u, 3u, 7u})
    {
        armor.proto.InventoryType = inventoryType;
        Check(!CanApplyEnchantment(&mage, &armor, sServerFacade.LookupSpellInfo(6000)),
            "an enhancement in a backpack still obeys head/shoulder/leg maximum-level restrictions");
    }
    mage.level = 60;
    Check(CanApplyEnchantment(&mage, &armor, sServerFacade.LookupSpellInfo(6000)), "item enhancements become legal at maximum level");
    mage.level = 33; armor.proto.InventoryType = 9; armor.slot = 8;
    Check(EnchantItem(&ai, &armor) && armor.enchant == 2101, "lower-rank passive effects match their actual aura family rather than an unrelated healing bonus");

    Player warrior; warrior.classId = 1; warrior.spec = 1;
    PlayerbotAI warriorAI{&warrior};
    Item warriorSword; warriorSword.owner = &warrior;
    Check(!EnchantItem(&warriorAI, &warriorSword) && warriorAI.applications == 0,
        "Crusader cannot downgrade into an unrelated low-skill proc");
    warrior.level = 60;
    Check(EnchantItem(&warriorAI, &warriorSword) && warriorSword.enchant == 1900, "the exact Crusader template works at maximum level");

    mage.level = 9; sword.enchant = 0;
    Check(!EnchantItem(&ai, &sword), "automatic template enchanting honors the configured minimum level");
    sword.equipped = false;
    Check(!EnchantItem(&ai, &sword, false, false), "ignoring the minimum level cannot enchant an unequipped item");

    std::cout << "Enchanting runtime: " << checks << " checks passed\n";
    return EXIT_SUCCESS;
}
