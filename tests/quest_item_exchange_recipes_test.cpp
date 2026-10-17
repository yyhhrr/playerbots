#include "QuestItemExchangeRecipes.h"
#include "PlayerbotAIConfig.h"
#include <cstdlib>
#include <iostream>
#include <string>

ObjectMgr sObjectMgr;
PlayerbotAIConfig sPlayerbotAIConfig;
namespace
{
    unsigned checks = 0;
    void Check(bool condition, const std::string& reason)
    {
        ++checks;
        if (!condition) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
    }
    void Item(uint32 id, uint32 cls = 15, uint32 quality = 4, uint32 bonding = 1, uint32 inventory = 0, uint32 maxCount = 0)
    {
        sObjectMgr.items.emplace(id, ItemPrototype{id, cls, quality, bonding, inventory, maxCount});
    }
    Quest& Q(uint32 id)
    {
        auto quest = std::make_unique<Quest>(); quest->id = id;
        return *sObjectMgr.quests.emplace(id, std::move(quest)).first->second;
    }
    void Required(Quest& quest, uint32 slot, uint32 item, uint32 count = 1)
    {
        quest.ReqItemId[slot] = item; quest.ReqItemCount[slot] = count;
    }
    void Fixed(Quest& quest, uint32 slot, uint32 item, uint32 count = 1)
    {
        quest.RewItemId[slot] = item; quest.RewItemCount[slot] = count;
    }
    void Choice(Quest& quest, uint32 slot, uint32 item, uint32 count = 1)
    {
        quest.RewChoiceItemId[slot] = item; quest.RewChoiceItemCount[slot] = count;
    }
    Quest& Direct(uint32 id, uint32 key = 22349, uint32 reward = 22416)
    {
        auto& quest = Q(id); Required(quest, 0, key); Fixed(quest, 0, reward); return quest;
    }
    const ai::QuestItemExchangeRecipe* Find(uint32 id)
    {
        for (const auto& recipe : ai::GetQuestItemExchangeRecipes())
            if (recipe.questId == id) return &recipe;
        return nullptr;
    }
}
int main()
{
    Check(ai::GetQuestItemExchangeRecipes().empty(), "early empty quest store is not frozen into cache");
    Item(22349); Item(22351);
    Item(22375, 15, 3, 0); Item(22376, 15, 3, 0);
    Item(12360, 7, 2, 0); Item(20725, 7, 4, 0);
    Item(20876, 12, 3, 0); Item(20861, 12, 2, 0); Item(20865, 12, 2, 0);
    Item(20927, 12); Item(20884, 12, 3); Item(19716, 12);
    Item(18563, 15, 5); Item(18564, 15, 5); Item(18562, 7, 4, 0);
    Item(19018, 12, 5);
    Item(22737, 15, 5, 0);
    Item(5881, 12, 1, 4, 0, 1);
    Item(7001, 2, 3, 1, 26); Item(7002, 4, 3, 1, 14);
    Item(18422, 12); Item(21232); Item(21237);
    for (uint32 id : {22416u, 22496u, 21332u, 21393u, 19827u, 22107u, 22111u, 22112u,
        18406u, 18403u, 18404u, 21242u, 21272u, 21244u}) Item(id, 4, 4, 1, 5);
    Item(19019, 2, 5, 1, 13);
    Item(22632, 2, 5, 1, 17);
    for (uint32 id : {16715u, 16719u, 16718u}) Item(id, 4, 3, 2, 5);
    Item(95000, 7, 1, 0);
    sPlayerbotAIConfig.questItemExchangeKeys = {95000};
    Check(ai::IsQuestExchangeKeyPrototype(sObjectMgr.GetItemPrototype(95000)), "explicit material override is recognized");
    Check(!ai::IsQuestExchangeKeyPrototype(nullptr), "null prototype is rejected");
    Check(!ai::IsQuestExchangeKeyPrototype(sObjectMgr.GetItemPrototype(12360)), "unlisted common material is rejected");
    Check(ai::IsQuestExchangeKeyPrototype(sObjectMgr.GetItemPrototype(5881)), "actual unique common-quality bound quest head is a key");

    auto& kelrisAlliance = Q(1200); kelrisAlliance.races = 77; Required(kelrisAlliance, 0, 5881);
    Choice(kelrisAlliance, 0, 7001); Choice(kelrisAlliance, 1, 7002);
    auto& kelrisHorde = Q(6561); kelrisHorde.races = 178; Required(kelrisHorde, 0, 5881);
    Choice(kelrisHorde, 0, 7001); Choice(kelrisHorde, 1, 7002);

    auto& t3 = Direct(9034); t3.classes = 1;
    Required(t3, 1, 22375, 25); Required(t3, 2, 12360, 4); Required(t3, 3, 20725, 2);
    auto& aq40 = Direct(8560, 20927, 21332);
    Required(aq40, 1, 20876, 2); Required(aq40, 2, 20861, 5); Required(aq40, 3, 20865, 5);
    Direct(8556, 20884, 21393);
    auto& zg = Q(8053); Required(zg, 3, 19716); Fixed(zg, 0, 19827);
    auto& upgrade = Q(8951);
    Required(upgrade, 0, 16715); Required(upgrade, 1, 16719); Required(upgrade, 2, 16718);
    Fixed(upgrade, 0, 22107); Fixed(upgrade, 1, 22111); Fixed(upgrade, 2, 22112);
    // A synthetic direct multi-key route exercises extraction, not Thunderfury's scripted summoning chain.
    auto& multiKey = Q(91000);
    Required(multiKey, 0, 18563); Required(multiKey, 1, 18564); Required(multiKey, 2, 18562, 10); Fixed(multiKey, 0, 19019);
    Direct(7787, 19018, 19019);
    auto& atiesh = Direct(9269, 22737, 22632);
    atiesh.classes = 1024; atiesh.ReqCreatureOrGOCount[0] = 1;
    auto& head = Q(7490); Required(head, 0, 18422); head.nextChain = 7491; head.races = 255;
    auto& headReward = Q(7491); headReward.races = 178;
    Choice(headReward, 0, 18406); Choice(headReward, 1, 18403); Choice(headReward, 2, 18404);
    auto& imperial = Q(8789); Required(imperial, 0, 21232); Required(imperial, 1, 18562, 3);
    Choice(imperial, 0, 21242); Choice(imperial, 1, 21272); Choice(imperial, 2, 21244);

    auto& four = Q(20000); Required(four, 0, 22349); four.nextChain = 20001;
    for (uint32 id = 20001; id <= 20004; ++id) { auto& q = Q(id); q.nextChain = id + 1; if (id == 20004) Fixed(q, 0, 22416); }
    auto& five = Q(30000); Required(five, 0, 22349); five.nextChain = 30001;
    for (uint32 id = 30001; id <= 30005; ++id) { auto& q = Q(id); q.nextChain = id + 1; if (id == 30005) Fixed(q, 0, 22416); }
    auto& cycle = Q(40000); Required(cycle, 0, 22349); cycle.nextChain = 40001;
    Q(40001).nextChain = 40000;
    auto& newItems = Q(50000); Required(newItems, 0, 22349); newItems.nextChain = 50001;
    auto& newItemsReward = Direct(50001, 12360); Required(newItemsReward, 0, 12360, 1);
    auto& killing = Direct(60000); killing.ReqCreatureOrGOCount[0] = 1;
    auto& spell = Direct(60001); spell.ReqSpell[0] = 123;
    auto& eventQuest = Direct(60002); eventQuest.special = QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT;
    auto& eventStart = Q(60003); Required(eventStart, 0, 22349); eventStart.nextChain = 60004;
    auto& eventEnd = Q(60004); eventEnd.special = QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT; Fixed(eventEnd, 0, 22416);
    auto& ordinaryKill = Q(60005); ordinaryKill.ReqCreatureOrGOCount[0] = 1; Fixed(ordinaryKill, 0, 22416);
    Direct(90001).active = false;
    Direct(90002).method = 0;
    Direct(90003, 16715, 16715);
    auto& disjoint = Q(90004); disjoint.classes = 1; Required(disjoint, 0, 22349); disjoint.nextChain = 90005;
    auto& otherClass = Q(90005); otherClass.classes = 2; Fixed(otherClass, 0, 22416);
    auto& duplicateKey = Direct(90006); Required(duplicateKey, 1, 22349, 2); Fixed(duplicateKey, 1, 22416, 2);
    auto& fallback = Q(90007); Required(fallback, 0, 22349); fallback.next = 90008;
    auto& fallbackEnd = Q(90008); Fixed(fallbackEnd, 0, 22416);
    auto& negativeNext = Q(90009); Required(negativeNext, 0, 22349); negativeNext.next = -90008;
    auto& methodOne = Q(90010); Required(methodOne, 0, 22349); methodOne.method = 1; Fixed(methodOne, 0, 22416);
    Direct(90011, 95000, 22416);

    const auto* t3Recipe = Find(9034);
    Check(Find(1200) && Find(1200)->keys[0].itemId == 5881 && Find(1200)->choiceRewards.size() == 2,
        "actual Alliance Kelris head quest exposes its two equipment choices");
    Check(Find(6561) && Find(6561)->keys[0].itemId == 5881 && Find(6561)->requiredRaces == 178,
        "actual Horde Kelris head quest preserves faction restriction");
    Check(t3Recipe && t3Recipe->keys.size() == 1 && t3Recipe->keys[0].itemId == 22349, "T3 ignores scrap bars and crystals");
    Check(t3Recipe && t3Recipe->requiredClasses == 1, "initial class restriction is retained");
    Check(Find(8560) && Find(8560)->keys.size() == 1 && Find(8560)->keys[0].itemId == 20927, "AQ40 keeps token and ignores idols/scarabs");
    Check(Find(8556) && Find(8556)->keys[0].itemId == 20884, "rare AQ20 token is supported");
    Check(Find(8053) && Find(8053)->keys[0].itemId == 19716, "ZG token in fourth objective slot is supported");
    Check(Find(8951) && Find(8951)->keys.size() == 3 && Find(8951)->fixedRewards.size() == 3, "T0.5 retains all three old gear keys and fixed rewards");
    Check(Find(91000) && Find(91000)->keys.size() == 2, "a direct multi-key route retains two bound legendary keys but ignores ore");
    Check(Find(7787) && Find(7787)->keys[0].itemId == 19018, "actual dormant blade turn-in yields Thunderfury");
    Check(Find(9269) && Find(9269)->keys[0].itemId == 22737 && Find(9269)->fixedRewards[0].itemId == 22632,
        "actual Atiesh turn-in exchanges an unbound legendary key while bypassing its kill objective");
    Check(Find(9269) && Find(9269)->requiredClasses == 1024 && Find(9269)->requiredRaces == 0,
        "actual druid Atiesh class and unrestricted race mask are retained");
    Check(Find(7490) && Find(7490)->choiceRewards.size() == 3, "head turn-in discovers following equipment choices");
    Check(Find(7490) && Find(7490)->requiredRaces == 178, "chain race restrictions intersect");
    Check(Find(8789) && Find(8789)->choiceRewards.size() == 3 && Find(8789)->keys.size() == 1, "imperial choices retain their shared key only");
    Check(Find(20000), "equipment exactly four hops away is included");
    Check(Find(30000), "equipment five hops away remains supported without an arbitrary depth limit");
    Check(!Find(40000), "cyclic follow-up chain terminates without recipe");
    Check(!Find(50000), "follow-up requiring even ordinary materials is excluded");
    Check(Find(60000) && Find(60001) && Find(60002) && Find(60003), "key-backed exchanges bypass kill spell and event objectives, including item-free follow-ups");
    Check(!Find(60005), "ordinary kill quests without any key still do not produce recipes");
    Check(!Find(90001) && !Find(90002) && !Find(90010), "inactive and non-method-two initial quests are excluded");
    Check(!Find(90003), "self-rewarding equipment key cannot loop");
    Check(!Find(90004), "disjoint class restrictions are rejected");
    Check(Find(90006) && Find(90006)->keys.size() == 1 && Find(90006)->keys[0].count == 3, "duplicate key slots combine required counts");
    Check(Find(90006) && Find(90006)->fixedRewards.size() == 1 && Find(90006)->fixedRewards[0].count == 3, "duplicate fixed rewards combine counts");
    Check(Find(90007), "positive NextQuestId is used when chain field is absent");
    Check(!Find(90009), "negative NextQuestId is not followed");
    Check(Find(90011), "explicit configured key produces a real recipe");

    Check(t3Recipe && ai::IsAutomaticQuestItemExchangeRecipe(*t3Recipe), "the extracted T3 token recipe remains automatic");
    Check(Find(8556) && ai::IsAutomaticQuestItemExchangeRecipe(*Find(8556)), "the extracted AQ20 token recipe remains automatic");
    Check(Find(8053) && ai::IsAutomaticQuestItemExchangeRecipe(*Find(8053)), "the extracted ZG token recipe remains automatic");
    Check(Find(91000) && ai::IsAutomaticQuestItemExchangeRecipe(*Find(91000)), "multiple dedicated tokens can be consumed automatically together");
    Check(Find(8951) && !ai::IsAutomaticQuestItemExchangeRecipe(*Find(8951)), "the extracted multi-piece T0 equipment recipe requires manual exchange");

    ai::QuestItemExchangeRecipe mixed;
    mixed.keys = {{22349, 1}, {16715, 1}};
    Check(!ai::IsAutomaticQuestItemExchangeRecipe(mixed), "a dedicated token cannot authorize automatic consumption of another wearable input");
    mixed.keys = {{16715, 1}, {22349, 1}};
    Check(!ai::IsAutomaticQuestItemExchangeRecipe(mixed), "wearable-input protection does not depend on requirement order");

    ai::QuestItemExchangeRecipe configuredEquipment;
    configuredEquipment.keys = {{16715, 1}};
    sPlayerbotAIConfig.questItemExchangeKeys.push_back(16715);
    Check(ai::IsQuestExchangeKeyPrototype(sObjectMgr.GetItemPrototype(16715)), "configured T0 remains available for manual recipe recognition");
    Check(!ai::IsAutomaticQuestItemExchangeRecipe(configuredEquipment), "explicitly configuring T0 cannot enable automatic equipment consumption");
    Check(!ai::IsAutomaticQuestItemExchangeRecipe(mixed), "explicitly configuring T0 cannot enable a mixed automatic recipe");
    sPlayerbotAIConfig.questItemExchangeKeys.pop_back();

    ai::QuestItemExchangeRecipe invalid;
    Check(!ai::IsAutomaticQuestItemExchangeRecipe(invalid), "a recipe without key inputs is not automatic");
    invalid.keys = {{999999, 1}};
    Check(!ai::IsAutomaticQuestItemExchangeRecipe(invalid), "a missing key prototype prevents automatic exchange");
    invalid.keys = {{22349, 1}, {999999, 1}};
    Check(!ai::IsAutomaticQuestItemExchangeRecipe(invalid), "one missing prototype invalidates an otherwise valid token recipe");
    invalid.keys = {{22349, 0}};
    Check(!ai::IsAutomaticQuestItemExchangeRecipe(invalid), "a zero-quantity input cannot authorize automatic exchange");
    invalid.keys = {{12360, 1}};
    Check(!ai::IsAutomaticQuestItemExchangeRecipe(invalid), "a non-wearable ordinary material is not a dedicated key by itself");
    invalid.keys = {{95000, 1}};
    Check(ai::IsAutomaticQuestItemExchangeRecipe(invalid), "an explicitly configured non-wearable custom key remains supported");

    const auto& recipes = ai::GetQuestItemExchangeRecipes();
    for (std::size_t i = 1; i < recipes.size(); ++i) Check(recipes[i-1].questId < recipes[i].questId, "cached recipes are sorted and have unique quest ids");
    Check(&recipes == &ai::GetQuestItemExchangeRecipes(), "subsequent calls reuse cache");
    std::cout << "PASS: " << checks << " checks against byte-identical production Recipes.cpp; " << recipes.size() << " recipes\n";
}
