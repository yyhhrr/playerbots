#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <list>
#include <string>

using ObjectGuid = std::uint32_t;
constexpr unsigned SPELL_AURA_MOD_ROOT = 26;
constexpr float ACTION_EMERGENCY = 90.0f, ACTION_HIGH = 20.0f;
enum class BotState { BOT_STATE_COMBAT };

struct Unit
{
    float x = 5, y = 0;
    bool player = true, alive = true, rooted = true, safe = true, hostile = true, visible = true;
    bool IsPlayer() const { return player; }
    bool IsAlive() const { return alive; }
    bool HasAuraType(unsigned type) const { return type == SPELL_AURA_MOD_ROOT && rooted; }
    bool IsWithinLOSInMap(const Unit* target) const { return target && target->visible; }
};
struct Player : Unit {};
struct PlayerbotAI
{
    Player bot;
    Unit* target = nullptr;
    bool pvp = true, canMove = true, stay = false;
    PlayerbotAI() { bot.x = 0; }
    bool IsInPvp() const { return pvp; }
    bool CanMove() const { return canMove; }
    bool HasStrategy(const std::string& name, BotState) const { return name == "stay" && stay; }
    bool IsSafe(const Unit* unit) const { return unit && unit->safe; }
    Unit* GetUnit(ObjectGuid) const { return target; }
    template<class T> T Value(const char*) const { return T(1); }
};
struct ServerFacade
{
    bool IsHostileTo(const Unit*, const Unit* target) const { return target && target->hostile; }
    float GetDistance2d(const Unit* bot, const Unit* target) const
    {
        return std::hypot(bot->x - target->x, bot->y - target->y);
    }
} sServerFacade;

struct MagePvpRetreatTrigger
{
    PlayerbotAI* ai;
    Player* bot;
    explicit MagePvpRetreatTrigger(PlayerbotAI* ai) : ai(ai), bot(&ai->bot) {}
    bool IsActive();
};
#define AI_VALUE(type, name) ai->Value<type>(name)
#include "mage_pvp_retreat_trigger.inc"
#undef AI_VALUE

struct NextAction
{
    std::string name;
    float priority;
    NextAction(const char* name, float priority) : name(name), priority(priority) {}
    static NextAction** array(int, NextAction* action, void*) { return new NextAction*[]{action, nullptr}; }
};
struct TriggerNode
{
    std::string name;
    NextAction** actions;
    TriggerNode(const char* name, NextAction** actions) : name(name), actions(actions) {}
    ~TriggerNode()
    {
        for (unsigned i = 0; actions[i]; ++i) delete actions[i];
        delete[] actions;
    }
};
struct ClassPvpStrategy
{
    bool baseInitialized = false;
    void InitCombatTriggers(std::list<TriggerNode*>&) { baseInitialized = true; }
};
struct MagePvpStrategy : ClassPvpStrategy
{
    void InitCombatTriggers(std::list<TriggerNode*>&);
};
#include "mage_pvp_retreat_registration.inc"

namespace
{
    unsigned checks = 0;
    void Check(bool condition, const std::string& description)
    {
        ++checks;
        if (!condition) { std::cerr << "FAILED: " << description << '\n'; std::exit(EXIT_FAILURE); }
    }
}

int main()
{
    PlayerbotAI ai;
    Unit enemy;
    ai.target = &enemy;
    MagePvpRetreatTrigger trigger(&ai);
    Check(trigger.IsActive(), "a mage creates casting distance after rooting a nearby PvP opponent");
    enemy.x = 11.99f;
    Check(trigger.IsActive(), "retreat remains active while the rooted opponent is inside the casting-space threshold");
    enemy.x = 12.0f;
    Check(!trigger.IsActive(), "reaching safe casting distance ends the retreat instead of fleeing forever");
    enemy.x = 5;
    enemy.rooted = false;
    Check(!trigger.IsActive(), "the special retreat requires a genuinely rooted opponent");
    enemy.rooted = true; ai.pvp = false;
    Check(!trigger.IsActive(), "PvE root and AOE commitment behavior remains unchanged");
    ai.pvp = true; ai.canMove = false;
    Check(!trigger.IsActive(), "a rooted mage does not enqueue impossible ordinary movement");
    ai.canMove = true; ai.stay = true;
    Check(!trigger.IsActive(), "an explicit stay strategy is respected");
    ai.stay = false; enemy.player = false;
    Check(!trigger.IsActive(), "a rooted PvE creature does not trigger the player-specific kiting rule");
    enemy.player = true; enemy.alive = false;
    Check(!trigger.IsActive(), "a dead cached opponent does not trigger retreat");
    enemy.alive = true; enemy.safe = false;
    Check(!trigger.IsActive(), "an invalid cached opponent is rejected before use");
    enemy.safe = true; enemy.hostile = false;
    Check(!trigger.IsActive(), "a friendly rooted player is not treated as a pursuer");
    enemy.hostile = true; enemy.visible = false;
    Check(!trigger.IsActive(), "the special retreat requires line of sight to the opponent");
    enemy.visible = true; ai.target = nullptr;
    Check(!trigger.IsActive(), "missing combat targets do not fabricate a retreat destination");

    MagePvpStrategy strategy;
    std::list<TriggerNode*> triggers;
    strategy.InitCombatTriggers(triggers);
    Check(strategy.baseInitialized, "the real mage PvP registration retains its base combat triggers");
    unsigned retreats = 0;
    for (TriggerNode* node : triggers)
    {
        if (node->name == "mage pvp retreat")
        {
            ++retreats;
            Check(node->actions[0] && node->actions[0]->name == "flee",
                "the root opportunity schedules the actual movement action");
            Check(node->actions[0]->priority == 89.0f,
                "root kiting outranks ordinary damage while yielding to emergency defenses");
        }
        delete node;
    }
    Check(retreats == 1, "the real classic PvP strategy registers exactly one root-kiting response");
    std::cout << checks << " mage PvP retreat checks passed\n";
    return EXIT_SUCCESS;
}
