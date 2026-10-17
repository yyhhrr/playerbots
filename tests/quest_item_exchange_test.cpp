#include "playerbot/QuestItemExchangePolicy.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
    unsigned checks = 0;

    void Check(bool condition, const std::string& description)
    {
        ++checks;
        if (!condition)
        {
            std::cerr << "FAILED: " << description << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    ai::quest_exchange_policy::Candidate Upgrade(std::uint32_t quest, std::uint32_t reward,
        std::uint64_t gain, std::uint32_t quality = 4, std::uint32_t level = 88)
    {
        ai::quest_exchange_policy::Candidate result;
        result.questId = quest;
        result.rewardId = reward;
        result.scoreGain = gain;
        result.quality = quality;
        result.itemLevel = level;
        result.usable = true;
        result.keysAvailable = true;
        return result;
    }

    ai::quest_exchange_policy::Candidate Best(const std::vector<ai::quest_exchange_policy::Candidate>& candidates)
    {
        ai::quest_exchange_policy::Candidate best;
        for (const auto& candidate : candidates)
            if (ai::quest_exchange_policy::IsBetter(candidate, best))
                best = candidate;
        return best;
    }
}

int main()
{
    using namespace ai::quest_exchange_policy;

    Check(IsKeyItem({12, 4, 1, 0}), "an epic bind-on-pickup T3 quest token is a key");
    Check(IsKeyItem({12, 3, 1, 0}), "a blue AQ20 quest token is a key");
    Check(IsKeyItem({15, 4, 1, 0}), "a miscellaneous epic ZG exchange token is a key");
    Check(IsKeyItem({15, 5, 0, 0}), "the unbound legendary purified Atiesh key is recognized");
    Check(IsKeyItem({12, 5, 4, 0}), "legendary quest objects do not depend on a particular binding flag");
    Check(!IsKeyItem({7, 5, 0, 0}), "legendary trade goods still remain auxiliary materials");
    // Stack size is deliberately absent from KeyInfo: stackable tokens stay valid.
    const KeyInfo highStackToken{12, 3, 1, 0};
    Check(IsKeyItem(highStackToken), "a valid token is not rejected because it may have a large stack size");
    Check(IsKeyItem({4, 3, 2, 5}), "old blue bind-on-equip armor remains a recognized manual exchange input");
    Check(IsKeyItem({4, 2, 2, 8}), "old green bind-on-equip armor remains available for manual exchange");
    Check(IsKeyItem({2, 3, 1, 17}), "an old bound weapon remains a recognized manual exchange input");
    Check(!IsKeyItem({4, 1, 2, 5}), "ordinary white equipment is not automatically an exchange key");
    Check(!IsKeyItem({4, 3, 0, 5}), "unbound equipment is not automatically an exchange key");
    Check(!IsKeyItem({4, 3, 1, 0}), "an unequippable armor record is not treated as an old equipped piece");
    Check(!IsKeyItem({7, 3, 1, 0}), "trade goods remain materials even when rare and bound");
    Check(!IsKeyItem({7, 1, 0, 0}), "ordinary ore cloth and herbs are excluded");
    Check(!IsKeyItem({5, 4, 1, 0}), "a profession reagent is not an automatic key");
    Check(!IsKeyItem({0, 3, 1, 0}), "a rare consumable is not an automatic key");
    Check(!IsKeyItem({12, 1, 4, 0}), "an ordinary quest collection item is excluded");
    Check(!IsKeyItem({12, 2, 1, 0}), "an uncommon quest fragment is excluded without an override");
    Check(!IsKeyItem({15, 3, 0, 0}), "an unbound rare miscellaneous item is excluded");
    Check(!IsKeyItem({15, 3, 2, 0}), "a bind-on-equip miscellaneous item is not a bound exchange token");
    Check(IsKeyItem({13, 1, 0, 0, true}), "an explicitly configured special key bypasses automatic classification");
    Check(IsKeyItem({12, 1, 4, 0, false, 1}), "actual unique Head of Kelris is a key despite common quality");
    Check(IsKeyItem({15, 1, 1, 0, false, 1}), "a unique bound miscellaneous quest object is a key");
    Check(IsKeyItem({12, 1, 5, 0, false, 1}), "alternate quest binding is supported for unique objects");
    Check(!IsKeyItem({12, 1, 4, 0, false, 0}), "ordinary stackable quest collection objects remain excluded");
    Check(!IsKeyItem({12, 1, 4, 0, false, 20}), "a nonunique collection limit does not identify a key");
    Check(!IsKeyItem({7, 1, 4, 0, false, 1}), "unique profession materials remain excluded");
    Check(!IsKeyItem({12, 1, 0, 0, false, 1}), "a common unbound unique object is not automatically a key");

    Check(IsAutomaticKey({12, 4, 1, 0}), "dedicated T3 tokens still exchange automatically");
    Check(IsAutomaticKey({12, 3, 1, 0}), "blue AQ20 tokens still exchange automatically");
    Check(IsAutomaticKey({15, 4, 1, 0}), "dedicated miscellaneous ZG tokens still exchange automatically");
    Check(IsAutomaticKey({15, 5, 0, 0}), "a legendary quest token remains automatic even without binding");
    Check(IsAutomaticKey({12, 1, 4, 0, false, 1}), "a recognized unique quest object remains automatic");
    Check(!IsWearableKey({12, 4, 1, 0}), "a dedicated non-equipment token is not wearable");
    Check(IsWearableKey({4, 3, 2, 5}), "T0 armor is identified as a wearable input");
    Check(!IsAutomaticKey({4, 3, 2, 5}), "T0 armor can never be automatically consumed");
    Check(!IsAutomaticKey({4, 2, 2, 8}), "green old equipment also requires an explicit manual exchange");
    Check(!IsAutomaticKey({2, 3, 1, 17}), "old weapons are never automatically consumed");
    Check(!IsAutomaticKey({4, 3, 2, 5, true}), "explicit key configuration cannot make T0 automatically consumable");
    Check(!IsAutomaticKey({2, 5, 0, 17, true}), "an explicitly configured legendary weapon still requires manual exchange");
    Check(!IsAutomaticKey({1, 1, 0, 18, true}), "an explicitly configured equipped bag is protected from automatic consumption");
    Check(IsAutomaticKey({13, 1, 0, 0, true}), "an explicitly configured non-equipment custom key remains automatic");
    Check(!IsAutomaticKey({7, 1, 0, 0}), "ordinary materials do not become automatic exchange inputs");

    const Candidate normal = Upgrade(9050, 22488, 40);
    Check(IsEligible(normal), "a usable reward with sufficient keys and positive gain is eligible");
    Check(!IsEligible({}), "an empty candidate is not eligible");

    Candidate blocked = normal;
    blocked.usable = false;
    Check(!IsEligible(blocked), "wrong-class off-spec or otherwise unusable rewards are rejected");
    blocked = normal;
    blocked.keysAvailable = false;
    Check(!IsEligible(blocked), "insufficient available keys cannot authorize an exchange");
    blocked = normal;
    blocked.duplicate = true;
    Check(!IsEligible(blocked), "an already covered equipment slot cannot produce a duplicate");
    blocked.duplicate = false;
    Check(IsEligible(blocked), "a useful second ring or dual-wield item remains eligible when not a duplicate");
    blocked = normal;
    blocked.forcedKeep = true;
    Check(!IsEligible(blocked), "a forced current item prevents an automatic replacement");
    blocked = normal;
    blocked.scoreGain = 0;
    Check(!IsEligible(blocked), "equal or lower total equipment value is represented by zero gain and rejected");
    blocked.quality = 5;
    blocked.itemLevel = 200;
    Check(!IsEligible(blocked), "rarity and item level cannot authorize a score downgrade");

    Candidate better = normal;
    better.scoreGain = normal.scoreGain + 1;
    better.quality = 3;
    better.itemLevel = 60;
    Check(IsBetter(better, normal), "greater total score gain wins before rarity or level");
    Check(!IsBetter(normal, better), "a smaller gain does not beat a larger gain");

    better = normal;
    ++better.quality;
    Check(IsBetter(better, normal), "quality breaks equal score gains");
    better = normal;
    ++better.itemLevel;
    Check(IsBetter(better, normal), "item level breaks equal gain and quality");
    better = normal;
    --better.questId;
    Check(IsBetter(better, normal), "the lower quest id deterministically breaks otherwise equal candidates");
    better = normal;
    --better.rewardId;
    Check(IsBetter(better, normal), "the lower reward id is the final deterministic tie break");
    Check(!IsBetter(normal, normal), "an identical candidate is not strictly better");
    Check(IsBetter(normal, {}), "an eligible candidate replaces the empty selection");
    Check(!IsBetter(blocked, normal), "an ineligible high-rarity reward never replaces an eligible choice");
    Check(!IsBetter({}, {}), "two ineligible candidates do not select a reward");

    Candidate largest = Upgrade(9000, 22000, std::numeric_limits<std::uint64_t>::max());
    Check(IsBetter(largest, normal), "large gains compare without subtraction or overflow");

    const Candidate alternateQuest = Upgrade(8000, 21000, 60);
    const Candidate alternateReward = Upgrade(8000, 20999, 60);
    Candidate unusableWinner = Upgrade(7000, 20000, 1000);
    unusableWinner.usable = false;
    const Candidate forward = Best({normal, alternateQuest, unusableWinner, alternateReward});
    const Candidate reverse = Best({alternateReward, unusableWinner, alternateQuest, normal});
    Check(forward.questId == 8000 && forward.rewardId == 20999, "multiple tasks and choice rewards select the best eligible equipment");
    Check(forward.questId == reverse.questId && forward.rewardId == reverse.rewardId,
        "candidate traversal order cannot change a tied selection");

    Candidate afterExchange = normal;
    afterExchange.scoreGain = 0;
    afterExchange.duplicate = true;
    Check(!IsEligible(afterExchange), "re-evaluation after equipping a reward stops repeated conversion of the same slot");
    const Candidate wholeWeaponSet = Upgrade(9001, 22001, 0);
    Check(!IsEligible(wholeWeaponSet), "a two-handed weapon with no net improvement over main hand plus off hand is rejected");

    std::cout << "Passed " << checks << " quest-item exchange policy checks.\n";
    return EXIT_SUCCESS;
}
