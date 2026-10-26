#pragma once

class PlayerbotAI;
class Player;
class Unit;

namespace ai::alterac
{
    bool IsCapturing(Player* player);
    Unit* GetImmediateCaptureThreat(PlayerbotAI* ai);
    bool UpdateCaptureResponse(PlayerbotAI* ai);
    bool TryCapture(PlayerbotAI* ai);
}
