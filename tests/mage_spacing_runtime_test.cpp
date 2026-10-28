#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <list>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using ObjectGuid = std::uint64_t;
constexpr uint8 CLASS_MAGE = 8;
constexpr uint32 SPELL_AURA_MOD_ROOT = 1;
constexpr float ATTACK_DISTANCE = 5;
constexpr float ACTION_IDLE = 1, ACTION_NORMAL = 10, ACTION_HIGH = 20,
    ACTION_MOVE = 30, ACTION_INTERRUPT = 40, ACTION_LIGHT_HEAL = 60, ACTION_EMERGENCY = 90;
enum class BotState { BOT_STATE_COMBAT, BOT_STATE_REACTION, BOT_STATE_NON_COMBAT };
enum CurrentSpellTypes { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL };
enum SpellState { SPELL_STATE_CASTING, SPELL_STATE_CHANNELING, SPELL_STATE_FINISHED };
struct SpellEntry { uint32 Id; };
struct Spell
{
    const SpellEntry* m_spellInfo;
    SpellState state = SPELL_STATE_CASTING;
    SpellState getState() const { return state; }
};
struct SpellMgr
{
    uint32 GetFirstSpellInChain(uint32 id) const { return id == 12826 ? 118 : id; }
} sSpellMgr;

struct Config { bool fleeingEnabled = true; } sPlayerbotAIConfig;
struct WorldTimer
{
    static inline uint32 now = 0;
    static uint32 getMSTime() { return now; }
};
struct PlayerbotAI;
struct Unit
{
    ObjectGuid guid = 2;
    float x = 5, y = 0, z = 0, speed = 7;
    uint32 map = 1, instance = 1;
    bool alive = true, inWorld = true, player = false, safe = true, hostile = true;
    bool perceived = true, visible = true, breakableCC = false, rooted = false, taxi = false;
    Unit* victim = nullptr;
    virtual ~Unit() = default;
    ObjectGuid GetObjectGuid() const { return guid; }
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    float GetPositionZ() const { return z; }
    uint32 GetMapId() const { return map; }
    uint32 GetInstanceId() const { return instance; }
    bool IsAlive() const { return alive; }
    bool IsInWorld() const { return inWorld; }
    bool IsPlayer() const { return player; }
    bool IsCreature() const { return !player; }
    bool IsTaxiFlying() const { return taxi; }
    bool HasBreakableByDamageCrowdControlAura() const { return breakableCC; }
    bool HasAuraType(uint32 type) const { return type == SPELL_AURA_MOD_ROOT && rooted; }
    Unit* GetVictim() const { return victim; }
    Unit* GetTarget() const { return victim; }
    float GetSpeedInMotion() const { return speed; }
    bool IsWithinLOSInMap(const Unit* other, bool = true) const { return other && other->visible; }
    bool IsWithinDistInMap(const Unit* other, float distance) const
    {
        return other && map == other->map && instance == other->instance &&
            std::hypot(std::hypot(x - other->x, y - other->y), z - other->z) <= distance;
    }
};
struct Creature : Unit
{
    bool totem = false;
    bool IsTotem() const { return totem; }
};
struct Player : Unit
{
    uint8 playerClass = CLASS_MAGE;
    bool teleported = false;
    std::array<Spell*, 2> currentSpells{};
    Spell* GetCurrentSpell(CurrentSpellTypes slot) const { return currentSpells[slot]; }
    Player() { guid = 1; x = 0; player = true; }
    uint8 getClass() const { return playerClass; }
    bool IsBeingTeleported() const { return teleported; }
    bool CanAttack(const Unit* other) const { return other && other->hostile; }
};
template<class T> struct Value { T value{}; T Get() const { return value; } };
struct AiObjectContext
{
    std::map<std::string, Value<ObjectGuid>> guids;
    std::map<std::string, Value<std::list<ObjectGuid>>> lists;
    template<class T> Value<T>* GetValue(const std::string& key)
    {
        if constexpr (std::is_same_v<T, ObjectGuid>) return &guids[key];
        else if constexpr (std::is_same_v<T, std::list<ObjectGuid>>) return &lists[key];
        else static_assert(!sizeof(T), "Unexpected value type in the real spacing selector");
    }
};
struct PlayerbotAI
{
    Player bot;
    AiObjectContext context;
    std::map<ObjectGuid, Unit*> units;
    bool pvp = false, canMove = true, realPlayer = false;
    std::set<std::string> strategies;
    float fleeRange = 10, spellRange = 25;
    Player* GetBot() { return &bot; }
    AiObjectContext* GetAiObjectContext() { return &context; }
    bool IsInPvp() const { return pvp; }
    bool CanMove() const { return canMove; }
    bool IsRealPlayer() const { return realPlayer; }
    bool HasStrategy(const std::string& name, BotState) const { return strategies.contains(name); }
    float GetRange(const std::string& name) const { return name == "flee" ? fleeRange : spellRange; }
    bool IsSafe(const Unit* other) const { return other && other->safe; }
    Unit* GetUnit(ObjectGuid guid) const
    {
        auto found = units.find(guid);
        return found == units.end() ? nullptr : found->second;
    }
};
struct ServerFacade
{
    bool IsHostileTo(const Unit*, const Unit* target) const { return target && target->hostile; }
    float GetDistance2d(const Unit* bot, const Unit* target) const
    {
        return std::hypot(bot->x - target->x, bot->y - target->y);
    }
} sServerFacade;
struct PossibleTargetsValue
{
    static bool CanPerceive(const Unit* target, const Player*) { return target && target->perceived; }
};
namespace ai
{
    Unit* FindMagePveRetreatTarget(PlayerbotAI* ai);
}
#include "mage_spacing_selector.inc"

struct Event {};
struct Trigger
{
    PlayerbotAI* ai;
    Player* bot;
    AiObjectContext* context;
    std::string name;
    Trigger(PlayerbotAI* ai, std::string name) : ai(ai), bot(ai->GetBot()), context(ai->GetAiObjectContext()), name(std::move(name)) {}
    virtual ~Trigger() = default;
};
struct MagePveRetreatTrigger : Trigger
{
    explicit MagePveRetreatTrigger(PlayerbotAI* ai) : Trigger(ai, "mage pve retreat") {}
    bool IsActive();
};
struct Action
{
    PlayerbotAI* ai;
    Player* bot;
    std::string name;
    Action(PlayerbotAI* ai, std::string name) : ai(ai), bot(ai->GetBot()), name(std::move(name)) {}
    virtual ~Action() = default;
    virtual bool Execute(Event&) { return false; }
    virtual bool isUseful() { return true; }
    virtual bool ShouldReactionInterruptCast() const { return false; }
    virtual bool ShouldReactionInterruptMovement() const { return false; }
};
struct MovementAction : Action
{
    Unit* fledFrom = nullptr;
    bool movementPossible = true;
    bool baseUseful = true;
    unsigned requests = 0;
    using Action::Action;
    bool Flee(Unit* target) { ++requests; fledFrom = target; return movementPossible; }
    bool isUseful() override { return baseUseful; }
#include "mage_spacing_base_reaction_flags.inc"
};
#include "mage_spacing_action_class.inc"
using namespace ai;
#include "mage_spacing_trigger.inc"
#include "mage_spacing_action.inc"

struct NextAction
{
    std::string name;
    float priority;
    NextAction(const char* name, float priority) : name(name), priority(priority) {}
    template<class T> static NextAction* Ptr(T value)
    {
        if constexpr (std::is_integral_v<T>) { if (value) std::abort(); return nullptr; }
        else return value;
    }
    template<class... T> static NextAction** array(int, T... args)
    {
        return new NextAction*[]{Ptr(args)...};
    }
};
struct TriggerNode
{
    std::string name;
    NextAction** actions;
    TriggerNode(std::string name, NextAction** actions) : name(std::move(name)), actions(actions) {}
    ~TriggerNode() { for (unsigned i = 0; actions[i]; ++i) delete actions[i]; delete[] actions; }
};
struct ClassStrategy
{
    bool baseInitialized = false, baseReactionInitialized = false;
    void InitCombatTriggers(std::list<TriggerNode*>&) { baseInitialized = true; }
    void InitReactionTriggers(std::list<TriggerNode*>&) { baseReactionInitialized = true; }
};
struct AoeStrategy : ClassStrategy {};
struct CcStrategy : ClassStrategy {};
namespace classic
{
    struct MageStrategy : ClassStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); void InitReactionTriggers(std::list<TriggerNode*>&); };
#include "mage_spacing_registration_0.inc"
#include "mage_spacing_reaction_registration_0.inc"
    struct MageAoeStrategy : ::AoeStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); void InitReactionTriggers(std::list<TriggerNode*>&); };
#include "mage_spacing_baseaoe_0.inc"
#include "mage_spacing_aoe_reaction.inc"
    struct ArcaneMageAoeStrategy : MageAoeStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); };
    struct FireMageAoeStrategy : MageAoeStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); };
    struct FrostMageAoeStrategy : MageAoeStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); };
    struct MageCcStrategy : ::CcStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); };
#include "mage_spacing_arcane_0.inc"
#include "mage_spacing_fire_0.inc"
#include "mage_spacing_frost_0.inc"
#include "mage_spacing_cc_0.inc"
}
namespace tbc
{
    struct MageStrategy : ClassStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); void InitReactionTriggers(std::list<TriggerNode*>&); };
#include "mage_spacing_registration_1.inc"
#include "mage_spacing_reaction_registration_1.inc"
    struct MageAoeStrategy : ::AoeStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); void InitReactionTriggers(std::list<TriggerNode*>&); };
#include "mage_spacing_baseaoe_1.inc"
#include "mage_spacing_aoe_reaction.inc"
    struct ArcaneMageAoeStrategy : MageAoeStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); };
    struct FireMageAoeStrategy : MageAoeStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); };
    struct FrostMageAoeStrategy : MageAoeStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); };
    struct MageCcStrategy : ::CcStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); };
#include "mage_spacing_arcane_1.inc"
#include "mage_spacing_fire_1.inc"
#include "mage_spacing_frost_1.inc"
#include "mage_spacing_cc_1.inc"
}
namespace wotlk
{
    struct MageStrategy : ClassStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); void InitReactionTriggers(std::list<TriggerNode*>&); };
#include "mage_spacing_registration_2.inc"
#include "mage_spacing_reaction_registration_2.inc"
    struct MageAoeStrategy : ::AoeStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); void InitReactionTriggers(std::list<TriggerNode*>&); };
#include "mage_spacing_baseaoe_2.inc"
#include "mage_spacing_aoe_reaction.inc"
    struct ArcaneMageAoeStrategy : MageAoeStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); };
    struct FireMageAoeStrategy : MageAoeStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); };
    struct FrostMageAoeStrategy : MageAoeStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); };
    struct MageCcStrategy : ::CcStrategy { void InitCombatTriggers(std::list<TriggerNode*>&); };
#include "mage_spacing_arcane_2.inc"
#include "mage_spacing_fire_2.inc"
#include "mage_spacing_frost_2.inc"
#include "mage_spacing_cc_2.inc"
}
struct SpacingTriggerContext
{
    std::map<std::string, std::function<Trigger*(PlayerbotAI*)>> creators;
    SpacingTriggerContext()
    {
#include "mage_spacing_trigger_factory.inc"
    }
};
struct SpacingActionContext
{
    std::map<std::string, std::function<Action*(PlayerbotAI*)>> creators;
    SpacingActionContext()
    {
#include "mage_spacing_action_factory.inc"
    }
};

namespace
{
    unsigned checks = 0;
    void Check(bool condition, const std::string& message)
    {
        ++checks;
        if (!condition) { std::cerr << "FAILED: " << message << '\n'; std::exit(EXIT_FAILURE); }
    }
    struct Fixture
    {
        PlayerbotAI ai;
        Creature enemy;
        Fixture()
        {
            ai.units[enemy.guid] = &enemy;
            ai.context.guids["current target"].value = enemy.guid;
            enemy.victim = &ai.bot;
        }
        Unit* Target() { return FindMagePveRetreatTarget(&ai); }
    };
    template<class T> void CheckAoePriorities(const std::string& specialization)
    {
        T strategy;
        std::list<TriggerNode*> triggers;
        strategy.InitCombatTriggers(triggers);
        Check(strategy.baseInitialized, specialization + " retains the base AOE strategy");
        unsigned blizzards = 0, flamestrikes = 0, otExplosions = 0;
        for (TriggerNode* node : triggers)
        {
            for (unsigned i = 0; node->actions[i]; ++i)
            {
                const NextAction* action = node->actions[i];
                if (action->name == "mage ot arcane explosion")
                {
                    ++otExplosions;
                    Check(node->name == action->name && action->priority == ACTION_HIGH + 5,
                        specialization + " prioritizes its gated OT explosion over ground AOE");
                }
                if (action->name == "blizzard")
                {
                    ++blizzards;
                    Check(node->name == "mage blizzard aoe", specialization + " schedules Blizzard for a real AOE pack");
                    Check(action->priority == ACTION_HIGH + 3, specialization + " prioritizes Blizzard at 23");
                }
                if (action->name == "flamestrike")
                {
                    ++flamestrikes;
                    Check(action->priority <= ACTION_HIGH + 2, specialization + " keeps Flamestrike below Blizzard");
                }
                if (action->name == "arcane explosion")
                    Check(action->priority < ACTION_HIGH + 3, specialization + " keeps the gated legacy explosion below Blizzard");
                Check(action->name != "leveling arcane explosion",
                    specialization + " does not automatically schedule the old leveling-only alias");
            }
            delete node;
        }
        Check(otExplosions == 1, specialization + " registers exactly one gated OT explosion");
        Check(blizzards == 1, specialization + " registers exactly one prioritized Blizzard");
        Check(flamestrikes >= 1, specialization + " retains its safe Flamestrike fallback");
    }
    template<class T> void CheckAoeReactionPriority(const std::string& edition)
    {
        T strategy;
        std::list<TriggerNode*> triggers;
        strategy.InitReactionTriggers(triggers);
        Check(strategy.baseReactionInitialized, edition + " retains base AOE reactions");
        unsigned explosions = 0;
        for (TriggerNode* node : triggers)
        {
            if (node->name == "mage ot arcane explosion")
            {
                ++explosions;
                Check(node->actions[0]->name == node->name && node->actions[0]->priority == ACTION_HIGH + 5,
                    edition + " can react to OT during a ground AOE channel");
            }
            delete node;
        }
        Check(explosions == 1, edition + " installs exactly one OT channel replacement reaction");
    }
    template<class T> void CheckControlPriority(const std::string& edition)
    {
        T strategy;
        std::list<TriggerNode*> triggers;
        strategy.InitCombatTriggers(triggers);
        unsigned sheep = 0;
        for (TriggerNode* node : triggers)
        {
            if (node->name == "polymorph")
            {
                ++sheep;
                Check(node->actions[0]->name == "polymorph" && node->actions[0]->priority == ACTION_INTERRUPT + 1,
                    edition + " preserves the higher-priority Polymorph control response");
            }
            delete node;
        }
        Check(sheep == 1, edition + " retains exactly one Polymorph control response");
    }
    template<class T> void CheckRegistration(const std::string& edition)
    {
        T strategy;
        std::list<TriggerNode*> triggers;
        strategy.InitCombatTriggers(triggers);
        Check(strategy.baseInitialized, edition + " still initializes the base class strategy");
        unsigned spacing = 0;
        for (TriggerNode* node : triggers)
        {
            if (node->name == "mage pve retreat")
            {
                ++spacing;
                Check(node->actions[0] && node->actions[0]->name == "mage pve retreat",
                    edition + " schedules the dedicated retreat action");
                Check(node->actions[0]->priority > ACTION_MOVE && node->actions[0]->priority < ACTION_EMERGENCY,
                    edition + " spacing outranks ordinary damage and yields to emergency defenses");
            }
            delete node;
        }
        Check(spacing == 1, edition + " registers exactly one PvE spacing response");
        triggers.clear();
        strategy.InitReactionTriggers(triggers);
        Check(strategy.baseReactionInitialized, edition + " retains the normal reaction initialization");
        spacing = 0;
        for (TriggerNode* node : triggers)
        {
            if (node->name == "mage pve retreat")
            {
                ++spacing;
                Check(node->actions[0] && node->actions[0]->name == "mage pve retreat",
                    edition + " can reposition during an unsafe cast or channel");
                Check(node->actions[0]->priority > ACTION_MOVE && node->actions[0]->priority < ACTION_EMERGENCY,
                    edition + " reaction positioning yields to emergency defenses");
            }
            delete node;
        }
        Check(spacing == 1, edition + " registers exactly one reaction spacing response");
    }
}

int main()
{
    Check(!FindMagePveRetreatTarget(nullptr), "missing AI has no spacing target");
    Fixture f;
    Check(f.Target() == &f.enemy, "one nearby hostile creates casting space");
    f.enemy.speed = 100;
    Check(f.Target() == &f.enemy, "a fast pursuing creature cannot veto mage spacing");
    f.ai.strategies = {"follow"};
    Check(f.Target() == &f.enemy, "ordinary combat follow does not lock a mage in melee");
    f.ai.strategies = {"guard"};
    Check(f.Target() == &f.enemy, "ordinary guard does not lock a mage in melee");
    f.ai.strategies = {"wander"};
    Check(f.Target() == &f.enemy, "ordinary wander does not lock a mage in melee");
    f.enemy.rooted = true;
    Check(f.Target() == &f.enemy, "Frost Nova roots permit rather than hide the retreat opportunity");
    f.enemy.rooted = false;
    f.enemy.x = 11.9f;
    Check(f.Target() == &f.enemy, "retreat remains active until casting space is reached");
    f.enemy.x = 12.0f;
    Check(!f.Target(), "safe casting distance ends retreat");
    f.enemy.x = 5;
    f.ai.strategies = {"stay"};
    Check(!f.Target(), "explicit stay still prohibits repositioning");
    f.ai.strategies.clear(); f.ai.pvp = true;
    Check(!f.Target(), "PvP remains on its existing kiting path");
    f.ai.pvp = false; f.ai.canMove = false;
    Check(!f.Target(), "immobile mages do not request movement");
    f.ai.canMove = true; f.ai.realPlayer = true;
    Check(!f.Target(), "real player control is preserved");
    f.ai.realPlayer = false; f.ai.bot.playerClass = 9;
    Check(!f.Target(), "warlock spacing is not modified");
    f.ai.bot.playerClass = CLASS_MAGE; f.ai.bot.alive = false;
    Check(!f.Target(), "dead mages do not retreat");
    f.ai.bot.alive = true; f.ai.bot.inWorld = false;
    Check(!f.Target(), "unloaded mages do not retreat");
    f.ai.bot.inWorld = true; f.ai.bot.teleported = true;
    Check(!f.Target(), "teleporting mages do not retreat");
    f.ai.bot.teleported = false;
    sPlayerbotAIConfig.fleeingEnabled = false;
    Check(!f.Target(), "the configured global fleeing opt-out is preserved");
    sPlayerbotAIConfig.fleeingEnabled = true;
    f.ai.fleeRange = ATTACK_DISTANCE;
    Check(!f.Target(), "an explicitly disabled flee range is preserved");
    f.ai.fleeRange = 10;
    for (bool Unit::* flag : {&Unit::safe, &Unit::hostile, &Unit::perceived, &Unit::visible, &Unit::alive, &Unit::inWorld})
    {
        f.enemy.*flag = false;
        Check(!f.Target(), "unsafe, unseen, friendly or invalid units do not cause retreat");
        f.enemy.*flag = true;
    }
    f.enemy.player = true;
    Check(!f.Target(), "player targets are excluded from PvE spacing");
    f.enemy.player = false; f.enemy.breakableCC = true;
    Check(!f.Target(), "a sheep does not fabricate a close threat");
    f.enemy.breakableCC = false; f.enemy.map = 2;
    Check(!f.Target(), "another map cannot contribute a retreat target");
    f.enemy.map = 1; f.enemy.instance = 2;
    Check(!f.Target(), "another instance cannot contribute a retreat target");
    f.enemy.instance = 1;
    Creature near;
    near.guid = 3; near.x = 2;
    f.ai.units[near.guid] = &near;
    f.enemy.x = 25;
    f.ai.context.guids["closest attacker targeting me"].value = near.guid;
    f.ai.context.lists["attackers targeting me"].value = {near.guid};
    Check(f.Target() == &near, "a close attacker wins over a distant current target");
    f.enemy.x = 7;
    Check(f.Target() == &near, "the nearest attacker is preferred when both targets are nearby");
    near.perceived = false;
    Check(f.Target() == &f.enemy, "an unseen cached attacker cannot beat a visible target");
    near.perceived = true;
    Creature nearest;
    nearest.guid = 4; nearest.x = 1;
    f.ai.units[nearest.guid] = &nearest;
    f.ai.context.lists["attackers targeting me"].value = {near.guid, nearest.guid, nearest.guid};
    Check(f.Target() == &nearest, "the complete personal attacker list selects the actual nearest threat");
    f.ai.context.guids["current target"].value = 0;
    f.ai.context.guids["closest attacker targeting me"].value = 0;
    f.ai.context.lists["attackers targeting me"].value = {nearest.guid};
    Check(f.Target() == &nearest, "personal attackers remain valid when current target is missing");
    MagePveRetreatTrigger trigger(&f.ai);
    Check(trigger.IsActive(), "the real retreat trigger uses the selected threat");
    MagePveRetreatAction action(&f.ai);
    Check(action.ShouldReactionInterruptCast(), "spacing can interrupt an unsafe cast or channel");
    Check(!action.ShouldReactionInterruptMovement(), "repeated retreat reactions do not stop an existing safe movement spline");
    Event event;
    Check(action.Execute(event) && action.fledFrom == &nearest && action.requests == 1,
        "the real retreat action delegates safe movement away from that exact threat");
    action.movementPossible = false;
    WorldTimer::now = 0;
    Check(!action.Execute(event) && action.requests == 2, "native movement failure is reported rather than forced");
    Check(!action.isUseful(), "a failure at clock zero immediately backs off the reaction");
    WorldTimer::now = 4999;
    Check(!action.isUseful(), "a failed route cannot repeatedly request cast interruption during backoff");
    WorldTimer::now = 5000;
    Check(action.isUseful(), "the route may be retried after exactly five seconds");
    action.baseUseful = false;
    Check(!action.isUseful(), "retry expiry still honors the movement action's base usefulness");
    action.baseUseful = true;
    action.movementPossible = true;
    Check(action.Execute(event) && action.requests == 3, "a valid route can recover after its backoff");
    WorldTimer::now = 1;
    Check(action.isUseful(), "successful retry clears the previous failure state");
    const SpellEntry polymorphInfo{12826}, frostboltInfo{116};
    Spell crowdControl{&polymorphInfo};
    f.ai.bot.currentSpells[CURRENT_GENERIC_SPELL] = &crowdControl;
    Check(!action.isUseful(), "retreat reactions let an already-started ranked Polymorph finish");
    crowdControl.state = SPELL_STATE_FINISHED;
    Check(action.isUseful(), "a finished Polymorph no longer delays retreat");
    crowdControl.state = SPELL_STATE_CASTING;
    crowdControl.m_spellInfo = &frostboltInfo;
    Check(action.isUseful(), "ordinary single-target casts still yield to necessary spacing");
    crowdControl.m_spellInfo = &polymorphInfo;
    f.ai.bot.currentSpells[CURRENT_GENERIC_SPELL] = nullptr;
    f.ai.bot.currentSpells[CURRENT_CHANNELED_SPELL] = &crowdControl;
    Check(!action.isUseful(), "both current cast slots preserve active Polymorph control");
    f.ai.bot.currentSpells[CURRENT_CHANNELED_SPELL] = nullptr;
    MagePveRetreatAction wrapped(&f.ai);
    wrapped.movementPossible = false;
    WorldTimer::now = std::numeric_limits<uint32>::max() - 1999;
    Check(!wrapped.Execute(event), "a route failure may occur just before the millisecond timer wraps");
    WorldTimer::now = 0;
    Check(!wrapped.isUseful(), "timer wrap does not prematurely end route backoff");
    WorldTimer::now = 2999;
    Check(!wrapped.isUseful(), "wrapped backoff remains active for the full duration");
    WorldTimer::now = 3000;
    Check(wrapped.isUseful(), "wrapped backoff expires after five seconds");
    f.ai.strategies = {"stay"};
    Check(!trigger.IsActive(), "stay cancels a queued trigger");
    Check(!action.Execute(event) && action.requests == 3, "action execution rechecks eligibility after stay");
    Check(action.isUseful(), "a missing target does not incorrectly record another route failure");
    SpacingTriggerContext triggerContext;
    SpacingActionContext actionContext;
    auto registeredTrigger = std::unique_ptr<Trigger>(triggerContext.creators.at("mage pve retreat")(&f.ai));
    auto registeredAction = std::unique_ptr<Action>(actionContext.creators.at("mage pve retreat")(&f.ai));
    Check(dynamic_cast<MagePveRetreatTrigger*>(registeredTrigger.get()), "the real mage context registers the retreat trigger");
    Check(dynamic_cast<MagePveRetreatAction*>(registeredAction.get()), "the real mage context registers the retreat action");
    CheckRegistration<classic::MageStrategy>("Classic");
    CheckRegistration<tbc::MageStrategy>("TBC");
    CheckRegistration<wotlk::MageStrategy>("WotLK");
    CheckAoePriorities<classic::ArcaneMageAoeStrategy>("Classic Arcane");
    CheckAoePriorities<classic::FireMageAoeStrategy>("Classic Fire");
    CheckAoePriorities<classic::FrostMageAoeStrategy>("Classic Frost");
    CheckControlPriority<classic::MageCcStrategy>("Classic");
    CheckAoeReactionPriority<classic::MageAoeStrategy>("Classic");
    CheckAoePriorities<tbc::ArcaneMageAoeStrategy>("TBC Arcane");
    CheckAoePriorities<tbc::FireMageAoeStrategy>("TBC Fire");
    CheckAoePriorities<tbc::FrostMageAoeStrategy>("TBC Frost");
    CheckControlPriority<tbc::MageCcStrategy>("TBC");
    CheckAoeReactionPriority<tbc::MageAoeStrategy>("TBC");
    CheckAoePriorities<wotlk::ArcaneMageAoeStrategy>("WotLK Arcane");
    CheckAoePriorities<wotlk::FireMageAoeStrategy>("WotLK Fire");
    CheckAoePriorities<wotlk::FrostMageAoeStrategy>("WotLK Frost");
    CheckControlPriority<wotlk::MageCcStrategy>("WotLK");
    CheckAoeReactionPriority<wotlk::MageAoeStrategy>("WotLK");
    std::cout << checks << " mage spacing runtime checks passed\n";
}
