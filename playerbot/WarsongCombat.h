#pragma once

#include <cmath>

namespace ai
{
    namespace warsong
    {
        constexpr float EnemyCarrierEngageRange = 50.0f;
        constexpr float CarrierSelfDefenseRange = 12.0f;
        constexpr float EscortDefenseRange = 30.0f;

        inline bool CanPursueEnemyFlagCarrier(bool carryingFlag, bool validTarget,
            bool actualEnemyCarrier, float distance)
        {
            return !carryingFlag && validTarget && actualEnemyCarrier &&
                std::isfinite(distance) && distance >= 0.0f && distance <= EnemyCarrierEngageRange;
        }

        inline bool CanEngageAsCarrier(bool carryingFlag, float distance,
            bool directlyAttackingCarrier)
        {
            if (!carryingFlag)
                return true;

            // Defend against an immediate attacker while escaping. Old threat, an
            // attack on a teammate/pet, or a distant pet owner is not an exemption.
            return directlyAttackingCarrier && std::isfinite(distance) &&
                distance >= 0.0f && distance <= CarrierSelfDefenseRange;
        }

        inline bool CanEngageAsEscort(bool escorting, bool hasCarrierObjective,
            float distanceFromCarrier, float distanceFromEscort, bool directlyAttackingEscort)
        {
            if (!escorting || !hasCarrierObjective)
                return true;

            const bool nearCarrier = std::isfinite(distanceFromCarrier) &&
                distanceFromCarrier >= 0.0f && distanceFromCarrier <= EscortDefenseRange;
            return nearCarrier || CanEngageAsCarrier(true, distanceFromEscort, directlyAttackingEscort);
        }
    }
}
