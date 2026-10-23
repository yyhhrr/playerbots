#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace ai
{
    namespace warsong
    {
        constexpr uint32_t CarrierHoldMs = 25000;
        constexpr float CarrierSwitchMargin = 65.0f;
        constexpr float CarrierEmergencyMargin = 140.0f;

        enum class CarrierClass { Druid, FrostMage, Rogue, Warrior, Paladin, Other };

        struct CarrierCandidate
        {
            uint64_t guid = 0;
            CarrierClass carrierClass = CarrierClass::Other;
            bool alive = true;
            bool isBot = true;
            bool eligible = true;
            uint32_t health = 0;
            uint32_t maxHealth = 0;
            float manaPercent = 100.0f;
            float distance = 0.0f;
            bool mobilityReady = false;
            bool defenseReady = false;
            bool healer = false;
            bool soleHealer = false;
            bool underPressure = false;
            bool hasHealerSupport = false;
            bool hasShield = false;
        };

        inline bool IsCarrierEligible(const CarrierCandidate& candidate)
        {
            return candidate.guid && candidate.alive && candidate.isBot && candidate.eligible &&
                candidate.health && candidate.maxHealth && std::isfinite(candidate.distance) && candidate.distance >= 0.0f;
        }

        // Scores use only the supplied snapshot: no hidden enemy information or
        // class assumptions about cooldowns/spells a low-level bot does not know.
        inline float CarrierScore(const CarrierCandidate& candidate)
        {
            if (!IsCarrierEligible(candidate))
                return -std::numeric_limits<float>::infinity();

            float score = 90.0f;
            switch (candidate.carrierClass)
            {
                case CarrierClass::Druid: score = 320.0f; break;
                case CarrierClass::FrostMage: score = 210.0f; break;
                case CarrierClass::Rogue: score = 205.0f; break;
                case CarrierClass::Warrior: score = 150.0f; break;
                case CarrierClass::Paladin: score = 160.0f; break;
                default: break;
            }

            const float health = std::min(1.0f, float(candidate.health) / float(candidate.maxHealth));
            const float mana = std::isfinite(candidate.manaPercent) ?
                std::clamp(candidate.manaPercent, 0.0f, 100.0f) : 0.0f;
            score += health * 120.0f;
            score += std::min(80.0f, std::sqrt(float(candidate.maxHealth)) * 0.75f);
            if (health < 0.55f)
                score -= (0.55f - health) / 0.55f * 300.0f;
            // A druid needs resources for shifting/healing; mana matters less
            // for the mage's short escape and the paladin's defensive backup.
            if (candidate.carrierClass == CarrierClass::Druid && mana < 25.0f)
                score -= (25.0f - mana) / 25.0f * 180.0f;
            else if ((candidate.carrierClass == CarrierClass::FrostMage ||
                candidate.carrierClass == CarrierClass::Paladin) && mana < 15.0f)
                score -= (15.0f - mana) / 15.0f * 70.0f;

            // Travel cost can outweigh class preference. The cap also prevents
            // float overflow from malformed or extremely distant snapshots.
            score -= std::min(candidate.distance, 2000.0f) * 0.65f;
            score += candidate.mobilityReady ? 25.0f : 0.0f;
            score += candidate.defenseReady ? 20.0f : 0.0f;
            score -= candidate.underPressure ? 35.0f : 0.0f;
            score -= candidate.soleHealer ? 150.0f : 0.0f;
            if (candidate.carrierClass == CarrierClass::Warrior || candidate.carrierClass == CarrierClass::Paladin)
            {
                score += candidate.hasShield ? 25.0f : 0.0f;
                score += candidate.hasHealerSupport ? 75.0f : 0.0f;
            }
            return score;
        }

        struct CarrierSelection
        {
            uint64_t primary = 0;
            uint64_t backup = 0;
            uint64_t carrier = 0;
        };

        class CarrierPlanner
        {
        public:
            const CarrierSelection& Update(const std::vector<CarrierCandidate>& candidates,
                uint32_t now, uint64_t currentCarrier = 0)
            {
                if (currentCarrier)
                {
                    // The holder is authoritative, including a human or a bot
                    // absent from this snapshot. Never request a handover.
                    SetPrimary(currentCarrier, now);
                    selection.carrier = currentCarrier;
                }
                else
                {
                    selection.carrier = 0;
                    const CarrierCandidate* incumbent = Find(candidates, selection.primary);
                    const CarrierCandidate* best = Best(candidates, 0);
                    if (!incumbent)
                    {
                        const CarrierCandidate* replacement = Find(candidates, selection.backup);
                        if (!replacement || (best && CarrierScore(*best) >= CarrierScore(*replacement) + CarrierSwitchMargin))
                            replacement = best;
                        SetPrimary(replacement ? replacement->guid : 0, now);
                    }
                    else if (best && ShouldReplace(*incumbent, *best, uint32_t(now - primarySince)))
                        SetPrimary(best->guid, now);
                }

                const CarrierCandidate* incumbentBackup = Find(candidates, selection.backup);
                if (incumbentBackup && incumbentBackup->guid == selection.primary)
                    incumbentBackup = nullptr;
                const CarrierCandidate* bestBackup = Best(candidates, selection.primary);
                if (!incumbentBackup || (bestBackup &&
                    ShouldReplace(*incumbentBackup, *bestBackup, uint32_t(now - backupSince))))
                    SetBackup(bestBackup ? bestBackup->guid : 0, now);
                return selection;
            }

            void Clear()
            {
                selection = {};
                primarySince = backupSince = 0;
            }

        private:
            static const CarrierCandidate* Find(const std::vector<CarrierCandidate>& candidates, uint64_t guid)
            {
                if (!guid)
                    return nullptr;
                for (const CarrierCandidate& candidate : candidates)
                    if (candidate.guid == guid && IsCarrierEligible(candidate))
                        return &candidate;
                return nullptr;
            }

            static const CarrierCandidate* Best(const std::vector<CarrierCandidate>& candidates, uint64_t excluded)
            {
                const CarrierCandidate* result = nullptr;
                for (const CarrierCandidate& candidate : candidates)
                {
                    if (candidate.guid == excluded || !IsCarrierEligible(candidate))
                        continue;
                    const float score = CarrierScore(candidate);
                    if (!result || score > CarrierScore(*result) ||
                        (score == CarrierScore(*result) && candidate.guid < result->guid))
                        result = &candidate;
                }
                return result;
            }

            static bool ShouldReplace(const CarrierCandidate& incumbent, const CarrierCandidate& challenger, uint32_t elapsed)
            {
                if (incumbent.guid == challenger.guid)
                    return false;
                const float margin = CarrierScore(challenger) - CarrierScore(incumbent);
                return margin >= CarrierEmergencyMargin || (elapsed >= CarrierHoldMs && margin >= CarrierSwitchMargin);
            }

            void SetPrimary(uint64_t guid, uint32_t now)
            {
                if (selection.primary != guid)
                {
                    selection.primary = guid;
                    primarySince = now;
                }
            }

            void SetBackup(uint64_t guid, uint32_t now)
            {
                if (selection.backup != guid)
                {
                    selection.backup = guid;
                    backupSince = now;
                }
            }

            CarrierSelection selection;
            uint32_t primarySince = 0;
            uint32_t backupSince = 0;
        };

        struct PickupCandidate
        {
            uint64_t guid = 0;
            float distance = 0.0f;
            bool eligible = false;
        };

        // The runtime supplies bots that are currently able to click this flag,
        // including CC/cast/LOS checks. A far or controlled designated carrier
        // never makes the team wait. Dropped flags always go to the nearest bot.
        inline uint64_t SelectFlagPicker(const CarrierSelection& selection,
            const std::vector<PickupCandidate>& candidates, bool droppedFlag, float maximumDistance = 5.0f)
        {
            if (selection.carrier || !std::isfinite(maximumDistance) || maximumDistance < 0.0f)
                return 0;
            uint64_t nearest = 0;
            float nearestDistance = std::numeric_limits<float>::infinity();
            bool primaryPresent = false;
            bool backupPresent = false;
            for (const PickupCandidate& candidate : candidates)
            {
                if (!candidate.guid || !candidate.eligible || !std::isfinite(candidate.distance) ||
                    candidate.distance < 0.0f || candidate.distance > maximumDistance)
                    continue;
                primaryPresent = primaryPresent || candidate.guid == selection.primary;
                backupPresent = backupPresent || candidate.guid == selection.backup;
                if (!nearest || candidate.distance < nearestDistance ||
                    (candidate.distance == nearestDistance && candidate.guid < nearest))
                {
                    nearest = candidate.guid;
                    nearestDistance = candidate.distance;
                }
            }
            if (!droppedFlag && primaryPresent)
                return selection.primary;
            if (!droppedFlag && backupPresent)
                return selection.backup;
            return nearest;
        }
    }
}
