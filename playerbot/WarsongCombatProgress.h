#pragma once

class PlayerbotAI;
class Unit;

namespace ai::warsong
{
    bool CanFightFromHere(PlayerbotAI* ai, Unit* target);
    bool IsCombatTargetSuppressed(PlayerbotAI* ai, Unit* target);
    bool UpdateCombatProgress(PlayerbotAI* ai);
}
