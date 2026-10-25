#pragma once

class PlayerbotAI;
class Unit;

namespace ai::perception
{
    bool CanUseTarget(PlayerbotAI* ai, Unit* target);
    bool ReleaseLostTargets(PlayerbotAI* ai);
}
