#pragma once

class PlayerbotAI;
class Player;
class Unit;

namespace ai::alterac
{
    bool CanFightFromHere(PlayerbotAI* ai, Unit* target);
    bool IsCombatTargetSuppressed(PlayerbotAI* ai, Unit* target);
    bool UpdateCombatProgress(PlayerbotAI* ai);
    bool IsCapturingEnemy(Player* observer, Unit* target);
}
