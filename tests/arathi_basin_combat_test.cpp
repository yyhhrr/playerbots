#include "playerbot/ArathiBasinCombat.h"

#include <cstdlib>
#include <iostream>
#include <string>

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
}

int main()
{
    using namespace ai;

    for (unsigned node = 0; node != 5; ++node)
    {
        Check(CanAssaultArathiBanner(node, 0, 0), "alliance can claim each neutral node");
        Check(CanAssaultArathiBanner(node, 0, 1), "horde can claim each neutral node");
        Check(!CanAssaultArathiBanner(node, 1, 0), "alliance cannot recapture its pending flag");
        Check(!CanAssaultArathiBanner(node, 3, 0), "alliance cannot recapture its occupied flag");
        Check(CanAssaultArathiBanner(node, 2, 0), "alliance can counter a horde assault");
        Check(CanAssaultArathiBanner(node, 4, 0), "alliance can attack a horde base");
        Check(CanAssaultArathiBanner(node, 1, 1), "horde can counter an alliance assault");
        Check(CanAssaultArathiBanner(node, 3, 1), "horde can attack an alliance base");
        Check(!CanAssaultArathiBanner(node, 2, 1), "horde cannot recapture its pending flag");
        Check(!CanAssaultArathiBanner(node, 4, 1), "horde cannot recapture its occupied flag");
    }
    Check(!CanAssaultArathiBanner(5, 0, 0), "a non-node event is not a flag capture");
    Check(!CanAssaultArathiBanner(255, 3, 1), "an unregistered object event is not a flag capture");
    Check(!CanAssaultArathiBanner(0, 5, 0), "an invalid node state is not a flag capture");
    Check(!CanAssaultArathiBanner(0, 0, 2), "an invalid team cannot capture");

    Check(IsArathiDefenderTargetAllowed(false, true, 80.0f, 5.0f, false), "attackers retain normal pursuit");
    Check(IsArathiDefenderTargetAllowed(true, false, 80.0f, 5.0f, false), "missing or stale-map objective cannot suppress combat");
    Check(IsArathiDefenderTargetAllowed(true, true, 39.0f, 30.0f, false), "defender can intercept enemies inside the node");
    Check(IsArathiDefenderTargetAllowed(true, true, 40.0f, 30.0f, false), "defensive radius includes its boundary");
    Check(!IsArathiDefenderTargetAllowed(true, true, 40.1f, 5.0f, false), "nearby fleeing decoy outside the node is not pursued");
    Check(!IsArathiDefenderTargetAllowed(true, true, 80.0f, 40.0f, true), "distant attacker cannot pull a defender off its node");
    Check(!IsArathiDefenderTargetAllowed(true, true, 45.0f, 10.1f, true), "direct attackers beyond self-defense reach remain excluded");
    Check(IsArathiDefenderTargetAllowed(true, true, 45.0f, 10.0f, true), "defender can fight a direct nearby attacker while returning");
    Check(!IsArathiDefenderTargetAllowed(true, true, 45.0f, 10.0f, false), "mere nearby presence does not grant a self-defense exemption");
    Check(!IsArathiDefenderTargetAllowed(true, true, 60.0f, 25.0f, true, true), "a player-controlled pet cannot keep its defender chasing outside the node");
    Check(IsArathiDefenderTargetAllowed(true, true, 45.0f, 6.0f, true, true), "a pet biting the defender can be fought while returning to the flag");
    Check(IsArathiDefenderTargetAllowed(true, true, 60.0f, 25.0f, true, false), "ordinary NPC combat keeps its existing pursuit rules");

    const ArathiCombatCandidate capper{ true, 25.0f, 4000 };
    const ArathiCombatCandidate decoy{ false, 3.0f, 20 };
    Check(IsBetterArathiCombatTarget(capper, decoy, true), "melee prefers an active capture over a close decoy");
    Check(IsBetterArathiCombatTarget(capper, decoy, false), "ranged prefers an active capture over a nearly dead decoy");
    Check(!IsBetterArathiCombatTarget(decoy, capper, true), "ordinary melee target cannot displace a capper");
    Check(!IsBetterArathiCombatTarget(decoy, capper, false), "ordinary ranged target cannot displace a capper");
    Check(IsBetterArathiCombatTarget({ true, 10.0f, 8000 }, capper, true), "melee selects the closest simultaneous capture");
    Check(IsBetterArathiCombatTarget({ true, 10.0f, 8000 }, capper, false), "ranged also selects the closest simultaneous capture");
    Check(!IsBetterArathiCombatTarget({ true, 30.0f, 1 }, capper, false), "low health does not override capture response distance");
    Check(IsBetterArathiCombatTarget({ false, 10.0f, 1000 }, { false, 20.0f, 100 }, true), "normal melee targeting retains nearest-first behavior");
    Check(IsBetterArathiCombatTarget({ false, 20.0f, 100 }, { false, 10.0f, 1000 }, false), "normal ranged targeting retains lowest-health behavior");
    Check(!IsBetterArathiCombatTarget(capper, capper, true), "equal capture candidates do not cause arbitrary switching");

    std::cout << checks << " Arathi combat policy checks passed\n";
    return EXIT_SUCCESS;
}
