// CMake extracts the coordinate constants and the actual production method bodies
// into this build-only include. These tests execute those bodies rather than a
// separately maintained copy of the Warsong route selection logic.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

using uint32 = std::uint32_t;

struct Position
{
    float x, y, z, o;
};

struct BattleGround
{
    uint32 GetMapId() const { return 489; }
};

struct Player
{
    Position position{};
    BattleGround* battleground = nullptr;
    bool inCombat = false;

    BattleGround* GetBattleGround() const { return battleground; }
    float GetPositionX() const { return position.x; }
    float GetPositionY() const { return position.y; }
    float GetPositionZ() const { return position.z; }
    bool IsInCombat() const { return inCombat; }
};

namespace ai
{
    struct PositionEntry
    {
        float x = 0, y = 0, z = 0;
    };

    using PositionMap = std::map<std::string, PositionEntry>;

    template <typename T> struct Value
    {
        T data;
        T Get() { return data; }
    };

    struct Context
    {
        PositionMap positions;
        Value<PositionMap&> positionValue{ positions };
        Value<uint32> roleValue{ 0 };

        template <typename T> Value<T>* GetValue(const char*)
        {
            if constexpr (std::is_same_v<T, PositionMap&>)
                return &positionValue;
            else
            {
                static_assert(std::is_same_v<T, uint32>);
                return &roleValue;
            }
        }
    };

    struct Movement
    {
        uint32 map;
        float x, y, z;
        bool idle, react, noPath;
    };

    class BGTactics
    {
    public:
        Player* bot;
        Context* context;
        bool movementResult = true;
        std::vector<Movement> moves;

        BGTactics(Player* player, Context* values) : bot(player), context(values) {}

        bool wsgPaths();
        bool wsgRoofJump();

        bool MoveTo(uint32 map, float x, float y, float z,
            bool idle = false, bool react = false, bool noPath = false)
        {
            moves.push_back({ map, x, y, z, idle, react, noPath });
            return movementResult;
        }

        // Keep the route test independent of motion generators and combat state.
        // The production wrapper is additionally checked by the core build.
        bool moveToWarsongPoint(uint32 map, float x, float y, float z,
            bool idle = false, bool react = false, bool noPath = false)
        {
            return MoveTo(map, x, y, z, idle, react, noPath);
        }
    };
}

namespace
{
    unsigned checks = 0;
    unsigned randomIntegerCalls = 0;

    uint32 urand(uint32 low, uint32)
    {
        ++randomIntegerCalls;
        return low;
    }

    float frand(float low, float high)
    {
        return (low + high) / 2.0f;
    }

    void Check(bool condition, const std::string& description)
    {
        ++checks;
        if (!condition)
        {
            std::cerr << "FAILED: " << description << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    bool Near(float actual, float expected)
    {
        return std::fabs(actual - expected) < 0.002f;
    }
}

using namespace ai;
#include "warsong_movement_source.inc"

namespace
{
    struct Scenario
    {
        BattleGround battleground;
        Player player;
        Context values;
        BGTactics action{ &player, &values };

        Scenario(Position start, Position destination, uint32 role = 0)
        {
            player.position = start;
            player.battleground = &battleground;
            values.positions["bg objective"] = { destination.x, destination.y, destination.z };
            values.roleValue.data = role;
        }
    };

    void CheckMove(const Scenario& scenario, Position target, bool noPath,
        const std::string& description)
    {
        Check(scenario.action.moves.size() == 1, description + ": one movement order");
        const Movement& move = scenario.action.moves.front();
        Check(move.map == 489, description + ": Warsong map");
        Check(Near(move.x, target.x) && Near(move.y, target.y) && Near(move.z, target.z),
            description + ": expected waypoint");
        Check(move.noPath == noPath, description + ": correct drop movement mode");
    }

    void PathCase(Position start, Position destination, uint32 role, Position target,
        const std::string& description, bool noPath = false)
    {
        for (bool moveResult : { false, true })
        {
            Scenario scenario(start, destination, role);
            scenario.action.movementResult = moveResult;
            Check(scenario.action.wsgPaths() == moveResult,
                description + ": propagate movement success and failure");
            CheckMove(scenario, target, noPath, description);
        }
    }

    void RoofCase(Position start, Position destination, Position target, bool noPath,
        const std::string& description, bool inCombat = false)
    {
        for (bool moveResult : { false, true })
        {
            Scenario scenario(start, destination);
            scenario.player.inCombat = inCombat;
            scenario.action.movementResult = moveResult;
            Check(scenario.action.wsgRoofJump() == moveResult,
                description + ": propagate movement success and failure");
            CheckMove(scenario, target, noPath, description);
        }
    }
}

int main()
{
    const Position hordeBase{ 919.0f, 1434.0f, 346.0f, 0 };
    const Position allianceBase{ 1538.0f, 1481.0f, 352.0f, 0 };
    {
        Scenario scenario({ 1400, 1460, 330, 0 }, hordeBase);
        scenario.player.battleground = nullptr;
        Check(!scenario.action.wsgPaths(), "missing battleground cannot select a route");
        Check(!scenario.action.wsgRoofJump(), "missing battleground cannot jump");
        Check(scenario.action.moves.empty(), "missing battleground produces no movement");
    }

    PathCase({ 1445, 1470, 342, 0 }, hordeBase, 0,
        { 1443.761963f, 1459.581909f, 342.115417f, 0 },
        "alliance tunnel doorway keeps its intermediate waypoint");
    PathCase({ 1400, 1460, 335, 0 }, hordeBase, 0,
        { 1380.761963f, 1457.581909f, 329.115417f, 0 },
        "alliance tunnel interior keeps its intermediate waypoint");
    {
        // Simulate arriving at successive points. The doorway must not be
        // overwritten by a midfield move in the same AI update.
        Scenario scenario({ 1445, 1470, 342, 0 }, hordeBase, 0);
        for (Position target : {
            Position{ 1443.761963f, 1459.581909f, 342.115417f, 0 },
            Position{ 1380.761963f, 1457.581909f, 329.115417f, 0 },
            Position{ 1351.759155f, 1462.368042f, 323.673737f, 0 } })
        {
            scenario.action.moves.clear();
            Check(scenario.action.wsgPaths(), "tunnel sequence advances after arrival");
            CheckMove(scenario, target, false, "tunnel sequence");
            scenario.player.position = target;
        }
    }

    PathCase({ 1360, 1460, 325, 0 }, hordeBase, 0,
        { 1351.759155f, 1462.368042f, 323.673737f, 0 },
        "alliance tunnel exit uses the nearby entrance rather than the opposite base");
    PathCase({ 1360, 1510, 325, 0 }, hordeBase, 0,
        { 1351.759155f, 1510, 323.673737f, 0 },
        "alliance tunnel northern exit retains its lateral position");

    PathCase({ 950, 1430, 346, 0 }, allianceBase, 0,
        { 1006.590210f, 1450.435059f, 335.721283f, 0 }, "horde tunnel exit");
    PathCase({ 1050, 1390, 320, 0 }, allianceBase, 0,
        { 1125.778076f, 1390, 316.567047f, 0 }, "horde tunnel southern exit");
    PathCase({ 1200, 1460, 310, 0 }, allianceBase, 0,
        { 1227.446289f, 1476.235718f, 307.484589f, 0 }, "midfield toward alliance");
    PathCase({ 1300, 1460, 320, 0 }, allianceBase, 0,
        { 1352.759155f, 1462.368042f, 323.673737f, 0 }, "alliance tunnel entrance");
    PathCase({ 1400, 1460, 330, 0 }, allianceBase, 0,
        { 1449.574219f, 1470.698608f, 342.675476f, 0 }, "alliance tunnel ascent");
    PathCase({ 1500, 1460, 350, 0 }, allianceBase, 0,
        { 1538.387207f, 1480.903198f, 352.576385f, 0 }, "alliance flag room arrival");
    PathCase({ 1000, 1460, 345, 0 }, allianceBase, 8,
        { 1031.764282f, 1454.516235f, 343.337860f, 0 }, "horde upper ramp exit");
    PathCase({ 1050, 1390, 340, 0 }, allianceBase, 8,
        { 1055.182251f, 1396.967529f, 339.361511f, 0 }, "horde graveyard exit is stable");
    PathCase({ 1405, 1540, 345, 0 }, hordeBase, 8,
        { 1422.106201f, 1529.851196f, 342.0f, 0 }, "alliance graveyard exit is stable");
    PathCase({ 1200, 1460, 315, 0 }, hordeBase, 8,
        { 1127.778076f, 1462.059937f, 315.698883f, 0 }, "horde tunnel entrance");

    RoofCase({ 1525, 1467.9f, 362, 0 }, hordeBase,
        WS_FLAG_ALLIANCE_FLOOR_JUMP_UPPER, false, "alliance second floor approaches edge");
    RoofCase({ 1529, 1468, 362, 0 }, hordeBase,
        WS_FLAG_ALLIANCE_FLOOR_JUMP_LOWER, true, "alliance second floor actually drops");
    RoofCase({ 1529, 1469.9f, 362, 0 }, hordeBase,
        WS_FLAG_ALLIANCE_FLOOR_JUMP_LOWER, true, "alliance second floor lower branch remains reachable");
    RoofCase({ 1529, 1468, 362, 0 }, hordeBase,
        WS_FLAG_ALLIANCE_FLOOR_JUMP_LOWER, true, "alliance second floor escapes combat toward field", true);
    {
        Scenario scenario({ 1529, 1470, 362, 0 }, hordeBase);
        Check(!scenario.action.wsgRoofJump(), "outside alliance second-floor edge has no forced drop");
        Check(scenario.action.moves.empty(), "second-floor edge boundary issues no invalid jump");
    }
    RoofCase({ 1500, 1480, 374, 0 }, hordeBase,
        WS_FLAG_ALLIANCE_ROOF_JUMP_UPPER, false, "alliance roof approaches edge");
    RoofCase({ 1523, 1467, 374, 0 }, hordeBase,
        WS_FLAG_ALLIANCE_ROOF_JUMP_LOWER, true, "alliance roof drops to ground");
    RoofCase({ 960, 1440, 368, 0 }, allianceBase,
        WS_FLAG_HORDE_ROOF_JUMP_UPPER, false, "horde roof approaches edge");
    RoofCase({ 931, 1448, 368, 0 }, allianceBase,
        WS_FLAG_HORDE_ROOF_JUMP_LOWER, true, "horde roof drops to ground");
    RoofCase({ 926, 1454, 355, 0 }, allianceBase,
        WS_FLAG_HORDE_FLOOR_JUMP_UPPER, false, "horde second floor approaches edge");
    RoofCase({ 926, 1451, 355, 0 }, allianceBase,
        WS_FLAG_HORDE_FLOOR_JUMP_LOWER, true, "horde second floor drops to ground");
    {
        Scenario scenario({ 931, 1448, 368, 0 }, { 930, 1440, 368, 0 });
        scenario.player.inCombat = true;
        Check(!scenario.action.wsgRoofJump(), "combat objective on same roof does not force a jump");
        Check(scenario.action.moves.empty(), "same-roof combat keeps current position");
    }

    // Broad route regressions: a single update must never replace a waypoint
    // with a second one, and a rejected movement must never report success.
    for (uint32 role = 0; role != 10; ++role)
        for (float x : { 919.f, 980.f, 1000.f, 1035.f, 1055.f, 1075.f, 1100.f,
            1130.f, 1200.f, 1250.f, 1300.f, 1365.f, 1390.f, 1400.f,
            1410.f, 1425.f, 1445.f, 1450.f, 1475.f, 1510.f, 1530.f })
            for (float y : { 1380.f, 1400.f, 1435.f, 1460.f, 1468.f, 1485.f, 1525.f, 1540.f })
                for (float z : { 315.f, 340.f, 352.f, 362.f, 374.f })
                    for (Position destination : { hordeBase, allianceBase })
                        for (bool movementResult : { false, true })
                        {
                            Scenario scenario({ x, y, z, 0 }, destination, role);
                            scenario.action.movementResult = movementResult;
                            const bool result = scenario.action.wsgPaths();
                            Check(scenario.action.moves.size() <= 1, "route update issues at most one move");
                            Check(result == (!scenario.action.moves.empty() && movementResult),
                                "route result reflects whether movement was accepted");
                        }

    Check(randomIntegerCalls == 0, "graveyard path choice cannot change randomly between updates");
    std::cout << checks << " Warsong movement checks passed\n";
    return EXIT_SUCCESS;
}
