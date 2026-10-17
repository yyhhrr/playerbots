#pragma once

#include "QuestItemExchange.h"

namespace ai
{
    struct QuestItemExchangeRecipe
    {
        uint32 questId = 0;
        uint32 requiredClasses = 0;
        uint32 requiredRaces = 0;
        std::vector<QuestItemExchangeItem> keys;
        std::vector<QuestItemExchangeItem> fixedRewards;
        std::vector<QuestItemExchangeItem> choiceRewards;
    };

    bool IsQuestExchangeKeyPrototype(const ItemPrototype* proto);
    // Automatic exchange requires every input to be a recognized, non-wearable key.
    bool IsAutomaticQuestItemExchangeRecipe(const QuestItemExchangeRecipe& recipe);
    const std::vector<QuestItemExchangeRecipe>& GetQuestItemExchangeRecipes();
}
