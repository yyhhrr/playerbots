#include "playerbot/WarsongCombat.h"

#include <cstdlib>
#include <iostream>
#include <limits>
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
    using namespace ai::warsong;

    Check(CanPursueEnemyFlagCarrier(false, true, true, 35.0f), "ordinary recoverer attacks a valid enemy carrier without carrying a flag itself");
    Check(CanPursueEnemyFlagCarrier(false, true, true, EnemyCarrierEngageRange), "carrier response includes the 50-yard boundary");
    Check(!CanPursueEnemyFlagCarrier(false, true, true, EnemyCarrierEngageRange + 0.01f), "dedicated carrier attack does not extend past its objective range");
    Check(!CanPursueEnemyFlagCarrier(true, true, true, 1.0f), "our carrier never starts the dedicated pursuit action");
    Check(!CanPursueEnemyFlagCarrier(false, false, true, 1.0f), "dead friendly hidden or unattackable cached carrier is rejected");
    Check(!CanPursueEnemyFlagCarrier(false, true, false, 1.0f), "a stale flag aura or cached former carrier does not authorize pursuit");
    Check(CanPursueEnemyFlagCarrier(false, true, true, 0.0f), "an overlapping valid carrier can be engaged");
    Check(!CanPursueEnemyFlagCarrier(false, true, true, -1.0f), "negative pursuit distance is invalid");
    Check(!CanPursueEnemyFlagCarrier(false, true, true, std::numeric_limits<float>::infinity()), "infinite pursuit distance is invalid");
    Check(!CanPursueEnemyFlagCarrier(false, true, true, std::numeric_limits<float>::quiet_NaN()), "unknown pursuit distance is invalid");

    Check(CanEngageAsCarrier(false, 60.0f, false), "noncarriers retain ordinary combat selection");
    Check(CanEngageAsCarrier(true, 0.0f, true), "a carrier can defend against a direct attacker at its feet");
    Check(CanEngageAsCarrier(true, CarrierSelfDefenseRange, true), "self-defense includes the 12-yard boundary");
    Check(!CanEngageAsCarrier(true, CarrierSelfDefenseRange + 0.01f, true), "a retreating attacker cannot keep a carrier pursuing");
    Check(!CanEngageAsCarrier(true, 1.0f, false), "a nearby enemy not directly attacking the carrier is not a reason to stop escaping");
    Check(!CanEngageAsCarrier(true, 40.0f, true), "a ranged caster cannot pull the carrier away from the escape route");
    Check(CanEngageAsCarrier(true, 3.0f, true), "a pet directly biting the carrier can be fought at close range");
    Check(!CanEngageAsCarrier(true, 45.0f, false), "a distant owner gains no exemption from the owner's pet attacking");
    Check(!CanEngageAsCarrier(true, 5.0f, false), "targeting the carrier's pet or teammate is not direct self-defense");
    Check(!CanEngageAsCarrier(true, -1.0f, true), "negative self-defense distance is invalid");
    Check(!CanEngageAsCarrier(true, std::numeric_limits<float>::infinity(), true), "infinite self-defense distance is invalid");
    Check(!CanEngageAsCarrier(true, std::numeric_limits<float>::quiet_NaN(), true), "unknown self-defense distance is invalid");

    for (float distance : { 0.0f, 6.0f, 12.0f, 12.1f, 30.0f, 50.0f, 75.0f })
    {
        Check(CanEngageAsCarrier(false, distance, false), "flag drop immediately restores ordinary combat selection");
        Check(!CanPursueEnemyFlagCarrier(true, true, true, distance), "flag pickup immediately cancels every dedicated enemy-carrier pursuit distance");
        Check(!CanEngageAsCarrier(true, distance, false), "cached old threat alone never grants carrier self-defense");
    }

    Check(CanEngageAsEscort(false, true, 90.0f, 50.0f, false), "recoverers and attackers retain ordinary combat range");
    Check(CanEngageAsEscort(true, false, 90.0f, 50.0f, false), "a missing or stale carrier objective cannot suppress ordinary combat");
    Check(CanEngageAsEscort(true, true, 25.0f, 20.0f, false), "escort can intercept an enemy threatening its carrier");
    Check(CanEngageAsEscort(true, true, EscortDefenseRange, 20.0f, false), "escort defense includes the 30-yard boundary");
    Check(!CanEngageAsEscort(true, true, EscortDefenseRange + 0.01f, 5.0f, false), "a fleeing decoy cannot pull the escort outside the carrier's defensive area");
    Check(CanEngageAsEscort(true, true, 50.0f, CarrierSelfDefenseRange, true), "escort can fight a direct nearby attacker while regrouping");
    Check(!CanEngageAsEscort(true, true, 50.0f, CarrierSelfDefenseRange + 0.01f, true), "self-defense cannot turn into pursuit away from the carrier");
    Check(!CanEngageAsEscort(true, true, 50.0f, 5.0f, false), "a nearby enemy targeting an escort's pet is not a self-defense exemption");
    Check(CanEngageAsEscort(true, true, 50.0f, 3.0f, true), "a pet directly attacking an escort can be fought while regrouping");
    Check(!CanEngageAsEscort(true, true, 70.0f, 50.0f, false), "a pet owner cannot inherit the pet's close self-defense exemption");
    Check(!CanEngageAsEscort(true, true, -1.0f, 50.0f, false), "invalid carrier-relative distance does not authorize a chase");
    Check(!CanEngageAsEscort(true, true, std::numeric_limits<float>::infinity(), 50.0f, false), "infinite carrier-relative distance is rejected");
    Check(!CanEngageAsEscort(true, true, std::numeric_limits<float>::quiet_NaN(), 50.0f, false), "unknown carrier-relative distance is rejected");
    Check(CanEngageAsEscort(true, true, std::numeric_limits<float>::quiet_NaN(), 3.0f, true), "invalid objective distance still permits immediate direct self-defense");

    std::cout << checks << " Warsong combat policy checks passed\n";
    return EXIT_SUCCESS;
}
