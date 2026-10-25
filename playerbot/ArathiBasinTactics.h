#pragma once

class PlayerbotAI;
class Player;
class GameObject;
class Unit;
struct WorldLocation;

namespace ai
{
    namespace arathi
    {
        // Refresh this bot's assignment and its position/manual-bool context values.
        bool SelectObjective(PlayerbotAI* ai, WorldLocation& objective);
        bool CanCapture(PlayerbotAI* ai, GameObject* flag);
        bool IsCapturing(Player* player);
        void RecordCaptureStart(PlayerbotAI* ai);
        Unit* GetImmediateCaptureThreat(PlayerbotAI* ai);
        bool UpdateCaptureResponse(PlayerbotAI* ai);
        bool IsCoveringCapture(PlayerbotAI* ai);
    }
}
