#include "playerbot/strategy/mage/MageAoePolicy.h"

#include <cstdint>
#include <cstdlib>
#include <array>
#include <iostream>
#include <list>
#include <map>
#include <string>
#include <utility>
#include <vector>

using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
constexpr uint32 CLASS_MAGE = 8;
constexpr uint32 UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL = 1;
constexpr uint32 POWER_MANA = 0;
constexpr uint32 SPELL_AURA_MOD_ROOT = 2, SPELL_AURA_MOD_STUN = 3;
enum class BotState { BOT_STATE_COMBAT, BOT_STATE_REACTION, BOT_STATE_NON_COMBAT, BOT_STATE_DEAD };
enum class ActionThreatType { ACTION_THREAT_NONE, ACTION_THREAT_SINGLE, ACTION_THREAT_AOE, ACTION_THREAT_LOW };
enum CurrentSpellTypes { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL };
enum SpellState { SPELL_STATE_CASTING, SPELL_STATE_CHANNELING, SPELL_STATE_FINISHED };
struct SpellEntry { uint32 Id; uint32 powerCost = 100; };
struct Position
{
    float x = 0, y = 0, z = 0;
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    float GetPositionZ() const { return z; }
};
struct SpellCastTargets
{
    Position destination;
    void getDestination(float& x, float& y, float& z) const
    {
        x = destination.x; y = destination.y; z = destination.z;
    }
};
struct Player;
struct Spell
{
    const SpellEntry* m_spellInfo;
    SpellState state = SPELL_STATE_CASTING;
    SpellCastTargets m_targets;
    SpellState getState() const { return state; }
    static uint32 CalculatePowerCost(const SpellEntry* info, Player*) { return info->powerCost; }
};
struct SpellMgr
{
    uint32 GetFirstSpellInChain(uint32 spell) const { return spell == 10161 ? 10 : spell; }
} sSpellMgr;
struct ServerFacade
{
    std::map<uint32, SpellEntry> spells;
    const SpellEntry* LookupSpellInfo(uint32 id) const
    {
        const auto found = spells.find(id);
        return found == spells.end() ? nullptr : &found->second;
    }
} sServerFacade;

struct Action
{
    std::string name;
    ActionThreatType threatType = ActionThreatType::ACTION_THREAT_SINGLE;
    bool useful = true, possible = true;
    Action(std::string name, ActionThreatType threatType = ActionThreatType::ACTION_THREAT_SINGLE) :
        name(name), threatType(threatType) {}
    virtual ~Action() = default;
    std::string getName() const { return name; }
    ActionThreatType getThreatType() const { return threatType; }
    bool isUseful() const { return useful; }
    bool isPossible() const { return possible; }
};
struct CastSpellAction : Action
{
    uint32 spellId;
    CastSpellAction(const std::string& name, uint32 spellId) : Action(name), spellId(spellId) {}
    uint32 GetSpellID() const { return spellId; }
};
struct Session { void SendPlaySpellVisual(uint32, uint32) {} };
struct Player
{
    uint8 playerClass = CLASS_MAGE;
    bool uncontrolled = false, falling = false;
    bool globalCooldown = false, rooted = false, stunned = false;
    uint32 mana = 100;
    std::array<Spell*, 2> currentSpells{};
    uint8 getClass() const { return playerClass; }
    bool hasUnitState(uint32) const { return uncontrolled; }
    bool IsFalling() const { return falling; }
    Spell* GetCurrentSpell(CurrentSpellTypes slot) const { return currentSpells[slot]; }
    bool HasGCD(const SpellEntry*) const { return globalCooldown; }
    uint32 GetPower(uint32) const { return mana; }
    bool HasAuraType(uint32 type) const { return type == SPELL_AURA_MOD_ROOT ? rooted : stunned; }
    Session* GetSession() { return &session; }
    uint32 GetObjectGuid() const { return 1; }
    Session session;
};
struct Trigger
{
    bool active = true;
    bool IsActive() const { return active; }
};
struct AiObjectContext
{
    std::map<std::string, Action*> actions;
    std::map<std::string, Trigger> triggers;
    Action* GetAction(const std::string& name)
    {
        const auto found = actions.find(name);
        return found == actions.end() ? nullptr : found->second;
    }
    Trigger* GetTrigger(const std::string& name) { return &triggers[name]; }
};
struct PlayerbotAI
{
    Player bot;
    AiObjectContext context;
    bool grouped = true, highThreat = true, areaDebuff = false, aoeEnabled = true, jumping = false;
    bool groundAoeSafe = true, pvp = false, closeThreat = false;
    std::vector<std::pair<std::string, BotState>> strategyChanges;
    Position checkedArea;
    uint8 aoeThreat = 100;
    Player* GetBot() { return &bot; }
    Player* GetMaster() { return nullptr; }
    AiObjectContext* GetAiObjectContext() { return &context; }
    bool IsJumping() const { return jumping; }
    bool IsInPvp() const { return pvp; }
    bool HasStrategy(const std::string& name, BotState) const { return name == "aoe" && aoeEnabled; }
    void ChangeStrategy(const std::string& name, BotState state) { strategyChanges.emplace_back(name, state); }
    template<class T> T Value(const std::string& name, const std::string& qualifier = "") const
    {
        if (name == "group") return T(grouped);
        if (name == "threat") return T(aoeThreat);
        if (name == "has area debuff") return T(areaDebuff);
        if (name == "trigger active") return T(qualifier == "high threat" && highThreat);
        return T{};
    }
};
#define AI_VALUE(type, name) ai->Value<type>(name)
#define AI_VALUE2(type, name, qualifier) ai->Value<type>(name, qualifier)

struct ThreatMultiplier
{
    PlayerbotAI* ai;
    Player* bot;
    explicit ThreatMultiplier(PlayerbotAI* ai) : ai(ai), bot(ai->GetBot()) {}
    float GetValue(Action*);
};
struct Multiplier
{
    virtual ~Multiplier() = default;
    virtual float GetValue(Action*) { return 1.0f; }
};
struct MageAoeCommitMultiplier : Multiplier
{
    PlayerbotAI* ai;
    Player* bot;
    AiObjectContext* context;
    std::string strategyName;
    explicit MageAoeCommitMultiplier(PlayerbotAI* ai, const std::string& strategyName = "aoe arcane pve") :
        ai(ai), bot(ai->GetBot()), context(ai->GetAiObjectContext()), strategyName(strategyName) {}
    float GetValue(Action*);
};
struct AoeStrategy
{
    PlayerbotAI* ai;
    std::string name = "aoe arcane pve";
    explicit AoeStrategy(PlayerbotAI* ai) : ai(ai) {}
    std::string getName() const { return name; }
    void InitCombatMultipliers(std::list<Multiplier*>&) {}
    void InitReactionMultipliers(std::list<Multiplier*>&) {}
};
struct MageAoeStrategy : AoeStrategy
{
    using AoeStrategy::AoeStrategy;
    void InitCombatMultipliers(std::list<Multiplier*>&);
    void InitReactionMultipliers(std::list<Multiplier*>&);
    void OnStrategyAdded(BotState);
    void OnStrategyRemoved(BotState);
};
struct Unit {};
Unit* FindMagePveRetreatTarget(PlayerbotAI* ai)
{
    static Unit target;
    return ai->closeThreat ? &target : nullptr;
}
struct MageAoeTarget {};
bool IsMageAoeAreaSafe(PlayerbotAI* ai, uint32, float x, float y, float z)
{
    ai->checkedArea = {x, y, z};
    return ai->groundAoeSafe;
}
struct CastBlizzardAction
{
    PlayerbotAI* ai;
    Player* bot;
    explicit CastBlizzardAction(PlayerbotAI* ai) : ai(ai), bot(ai->GetBot()) {}
    bool IsAoeReady(const MageAoeTarget&);
};

using namespace ai;
#include "mage_aoe_courage_threat.inc"
#include "mage_aoe_courage_multiplier.inc"
#include "mage_aoe_courage_blizzard.inc"
#include "mage_aoe_courage_registration.inc"

namespace
{
    unsigned checks = 0, failures = 0;
    void Check(bool condition, const std::string& description)
    {
        ++checks;
        if (!condition) { ++failures; std::cerr << "FAILED: " << description << '\n'; }
    }
}

int main()
{
    PlayerbotAI ai;
    ThreatMultiplier threat(&ai);
    for (const char* name : {"mage ot arcane explosion", "arcane explosion", "leveling arcane explosion", "flamestrike", "blizzard"})
    {
        Action action{name, ActionThreatType::ACTION_THREAT_AOE};
        Check(threat.GetValue(&action) == 1.0f, std::string(name) + " remains active at maximum AOE and current-target threat");
        ai.bot.playerClass = 9;
        Check(threat.GetValue(&action) == 0.0f, "other classes retain their original threat restriction");
        ai.bot.playerClass = CLASS_MAGE;
    }
    Action single{"frostbolt"};
    Check(threat.GetValue(&single) == 0.0f, "single-target mage damage retains normal threat handling");
    Action support{"mana potion", ActionThreatType::ACTION_THREAT_NONE};
    Check(threat.GetValue(&support) == 1.0f, "support actions retain their original threat behavior");
    Check(threat.GetValue(nullptr) == 1.0f, "a missing action remains harmless");

    MageAoeCommitMultiplier courage(&ai);
    Action aoe{"mage ot arcane explosion"};
    ai.context.actions[aoe.name] = &aoe;
    for (const char* name : {"flee", "runaway", "flee with pet", "invisibility", "lesser invisibility", "ice block", "shoot", "evocation", "blink", "mage pve retreat"})
    {
        Action action{name};
        Check(courage.GetValue(&action) == 0.0f, std::string(name) + " cannot displace a usable AOE pack");
    }
    ai.pvp = true;
    for (const char* name : {"flee", "runaway", "flee with pet", "invisibility", "lesser invisibility", "ice block", "shoot", "evocation", "blink"})
    {
        Action action{name};
        Check(courage.GetValue(&action) == 1.0f,
            std::string(name) + " remains available in PvP even while a usable AOE pack is present");
    }
    ai.pvp = false;
    for (const char* name : {"mana potion", "mana gem", "healing potion", "ice barrier", "mana shield", "frost nova", "counterspell", "polymorph", "follow", "attack", "select new target"})
    {
        Action action{name};
        Check(courage.GetValue(&action) == 1.0f, std::string(name) + " keeps its normal behavior during AOE");
    }
    Check(courage.GetValue(nullptr) == 1.0f, "a missing action is not suppressed by courage");
    Action flee{"flee"}, block{"ice block"};
    aoe.useful = false;
    Check(courage.GetValue(&flee) == 1.0f, "an unusable AOE pack such as two enemies or a protected sheep permits escape");
    aoe.useful = true; aoe.possible = false;
    Check(courage.GetValue(&block) == 1.0f, "insufficient mana or native cast restrictions permit emergency survival");
    aoe.possible = true; ai.bot.playerClass = 9;
    Check(courage.GetValue(&flee) == 1.0f, "the mage-only multiplier cannot change another class");
    ai.bot.playerClass = CLASS_MAGE; ai.areaDebuff = true;
    Check(courage.GetValue(&flee) == 1.0f, "a damaging ground effect still allows hazard avoidance");
    Action runaway{"runaway"};
    Check(courage.GetValue(&runaway) == 1.0f, "a damaging ground effect also preserves runaway");
    ai.areaDebuff = false;
    for (const char* name : {"mage ot arcane explosion", "flamestrike", "blizzard"})
    {
        ai.context.actions.clear();
        Action alternative{name};
        ai.context.actions[name] = &alternative;
        Check(courage.GetValue(&flee) == 0.0f, std::string(name) + " can independently maintain AOE commitment");
    }
    ai.context.actions.clear();
    Check(courage.GetValue(&flee) == 1.0f, "no registered AOE action leaves escape available");

    Action legacyExplosion{"arcane explosion"};
    ai.context.actions[legacyExplosion.name] = &legacyExplosion;
    MageAoeCommitMultiplier fire(&ai, "aoe fire pve"), frost(&ai, "aoe frost pve");
    Check(fire.GetValue(&flee) == 1.0f, "a fire mage cannot be held in place by an unscheduled legacy Arcane Explosion");
    Check(frost.GetValue(&flee) == 1.0f, "a frost mage cannot be held in place by an unscheduled legacy Arcane Explosion");
    ai.context.triggers["mage arcane explosion aoe"].active = false;
    Check(courage.GetValue(&flee) == 1.0f, "the legacy explosion cannot override the dedicated OT decision");
    ai.context.triggers["mage arcane explosion aoe"].active = true;
    Check(courage.GetValue(&flee) == 1.0f, "an active legacy arcane trigger still cannot force stationary output");
    ai.context.actions.clear();
    Action flamestrike{"flamestrike"};
    ai.context.actions[flamestrike.name] = &flamestrike;
    ai.context.triggers["frost spells locked"].active = false;
    Check(frost.GetValue(&flee) == 1.0f, "frost mages do not depend on an unscheduled Flamestrike");
    ai.context.triggers["frost spells locked"].active = true;
    Check(frost.GetValue(&flee) == 0.0f, "frost-locked mages can commit to their scheduled Flamestrike fallback");

    std::list<Multiplier*> multipliers;
    AoeStrategy ordinary(&ai);
    ordinary.InitCombatMultipliers(multipliers);
    Check(multipliers.empty(), "the base AOE strategy does not alter retreat for other classes");
    MageAoeStrategy mageAoe(&ai);
    mageAoe.InitCombatMultipliers(multipliers);
    Check(multipliers.size() == 1 && dynamic_cast<MageAoeCommitMultiplier*>(multipliers.front()),
        "the mage AOE strategy installs its own commitment multiplier");
    for (Multiplier* multiplier : multipliers) delete multiplier;
    for (const char* variant : {"aoe arcane pve", "aoe fire pve", "aoe frost pve", "aoe frost raid"})
    {
        mageAoe.name = variant;
        ai.strategyChanges.clear();
        mageAoe.OnStrategyAdded(BotState::BOT_STATE_COMBAT);
        Check(ai.strategyChanges.size() == 1 && ai.strategyChanges[0] ==
            std::make_pair("+" + std::string(variant), BotState::BOT_STATE_REACTION),
            "adding combat AOE mirrors the exact specialization into reaction");
        mageAoe.OnStrategyRemoved(BotState::BOT_STATE_COMBAT);
        Check(ai.strategyChanges.size() == 2 && ai.strategyChanges[1] ==
            std::make_pair("-" + std::string(variant), BotState::BOT_STATE_REACTION),
            "removing combat AOE also removes its reaction commitment");
        for (BotState state : {BotState::BOT_STATE_REACTION, BotState::BOT_STATE_NON_COMBAT, BotState::BOT_STATE_DEAD})
        {
            mageAoe.OnStrategyAdded(state);
            mageAoe.OnStrategyRemoved(state);
        }
        Check(ai.strategyChanges.size() == 2, "mirroring does not recurse or change other strategy states");
    }
    multipliers.clear();
    mageAoe.InitReactionMultipliers(multipliers);
    Check(multipliers.size() == 1 && dynamic_cast<MageAoeCommitMultiplier*>(multipliers.front()),
        "reaction movement also respects legal close-range AOE commitment");
    for (Multiplier* multiplier : multipliers) delete multiplier;

    ai.context.actions.clear();
    const SpellEntry currentBlizzard{10161}, currentFlamestrike{2120}, currentSingle{116};
    Spell channel{&currentBlizzard, SPELL_STATE_CHANNELING};
    channel.m_targets.destination = {25.0f, 3.0f, 1.0f};
    ai.bot.currentSpells[CURRENT_CHANNELED_SPELL] = &channel;
    Check(courage.GetValue(&flee) == 0.0f, "an ongoing higher-rank Blizzard channel keeps going when fewer than three enemies remain");
    Check(ai.checkedArea.x == 25.0f && ai.checkedArea.y == 3.0f && ai.checkedArea.z == 1.0f,
        "ongoing channels inspect their fixed destination rather than selecting another pack");
    ai.pvp = true;
    Check(courage.GetValue(&flee) == 1.0f, "an ongoing Blizzard channel cannot suppress PvP repositioning");
    ai.pvp = false;
    channel.state = SPELL_STATE_FINISHED;
    Check(courage.GetValue(&flee) == 1.0f, "a finished channel cannot indefinitely suppress escape");
    ai.bot.currentSpells[CURRENT_CHANNELED_SPELL] = nullptr;
    Spell cast{&currentFlamestrike};
    ai.bot.currentSpells[CURRENT_GENERIC_SPELL] = &cast;
    Check(courage.GetValue(&flee) == 0.0f, "an ongoing Flamestrike cast is not interrupted by ordinary escape");
    ai.pvp = true;
    Check(courage.GetValue(&flee) == 1.0f, "an ongoing Flamestrike cast cannot suppress PvP survival");
    ai.pvp = false;
    ai.groundAoeSafe = false;
    Check(courage.GetValue(&flee) == 1.0f, "a lost or controlled pack does not force continued commitment to a cast");
    ai.groundAoeSafe = true; cast.m_spellInfo = &currentSingle;
    Check(courage.GetValue(&flee) == 1.0f, "an unrelated single-target cast does not invoke AOE commitment");
    ai.bot.currentSpells[CURRENT_GENERIC_SPELL] = nullptr;

    // A distant ground pack must never immobilize a mage against a close add.
    ai.closeThreat = true;
    ai.bot.currentSpells[CURRENT_CHANNELED_SPELL] = &channel;
    channel.state = SPELL_STATE_CHANNELING;
    for (const char* name : {"flee", "runaway", "blink", "ice block", "mage pve retreat"})
    {
        Action escape{name};
        Check(courage.GetValue(&escape) == 1.0f,
            std::string(name) + " stays available when a nearby enemy interrupts a distant Blizzard pack");
    }
    ai.bot.currentSpells[CURRENT_CHANNELED_SPELL] = nullptr;
    cast.m_spellInfo = &currentFlamestrike;
    ai.bot.currentSpells[CURRENT_GENERIC_SPELL] = &cast;
    Check(courage.GetValue(&flee) == 1.0f, "a close threat permits movement during a Flamestrike cast");
    ai.bot.currentSpells[CURRENT_GENERIC_SPELL] = nullptr;
    for (const char* name : {"flamestrike", "blizzard"})
    {
        Action remoteAoe{name};
        ai.context.actions[name] = &remoteAoe;
        Check(courage.GetValue(&flee) == 1.0f,
            std::string(name) + " readiness cannot override space needed against one or two close enemies");
        ai.context.actions.clear();
    }
    Action closeExplosion{"mage ot arcane explosion"};
    ai.context.actions[closeExplosion.name] = &closeExplosion;
    Check(courage.GetValue(&flee) == 0.0f, "a genuinely usable OT explosion preserves its three-target commitment");
    Check(fire.GetValue(&flee) == 0.0f && frost.GetValue(&flee) == 0.0f,
        "all three specializations hold position for a real, castable OT explosion");
    Action dedicatedRetreat{"mage pve retreat"};
    Check(courage.GetValue(&dedicatedRetreat) == 0.0f, "the dedicated retreat cannot displace a legal close explosion");
    closeExplosion.useful = false;
    Check(courage.GetValue(&dedicatedRetreat) == 1.0f, "fewer than three or protected close targets release the retreat action");
    closeExplosion.useful = true; closeExplosion.possible = false;
    Check(courage.GetValue(&flee) == 1.0f, "a close explosion that cannot be cast does not hold the mage in melee");
    closeExplosion.possible = true;
    Action closeBlink{"blink"};
    ai.bot.rooted = true;
    Check(courage.GetValue(&closeBlink) == 1.0f, "a rooted PvE mage retains Blink even when close AOE is ready");
    ai.bot.rooted = false; ai.bot.stunned = true;
    Check(courage.GetValue(&closeBlink) == 1.0f, "a stunned PvE mage retains Blink even when close AOE is ready");
    ai.bot.stunned = false;
    ai.context.actions.clear();
    ai.closeThreat = false;

    CastSpellAction waitingExplosion{"mage ot arcane explosion", 1449};
    waitingExplosion.possible = false;
    ai.context.actions[waitingExplosion.name] = &waitingExplosion;
    sServerFacade.spells[1449] = {1449, 100};
    ai.bot.globalCooldown = true;
    Check(courage.GetValue(&flee) == 0.0f, "the global cooldown between affordable explosions does not trigger retreat");
    ai.pvp = true;
    Action rootedBlink{"blink"};
    Check(courage.GetValue(&rootedBlink) == 1.0f, "PvP root recovery is not held through an AOE global cooldown");
    ai.pvp = false;
    ai.bot.mana = 99;
    Check(courage.GetValue(&flee) == 1.0f, "a global cooldown with insufficient mana still permits escape");
    ai.bot.mana = 100; ai.bot.uncontrolled = true;
    Check(courage.GetValue(&flee) == 1.0f, "a global cooldown while uncontrolled still permits recovery actions");
    ai.bot.uncontrolled = false; ai.bot.globalCooldown = false;
    Check(courage.GetValue(&flee) == 1.0f, "other cast failures without a global cooldown do not lock escape");
    ai.context.actions.clear();

    CastBlizzardAction blizzard(&ai);
    MageAoeTarget pack;
    Check(blizzard.IsAoeReady(pack), "high threat and a nearby pack do not independently refuse Blizzard");
    ai.bot.uncontrolled = true;
    Check(!blizzard.IsAoeReady(pack), "loss of control still prevents a Blizzard channel");
    ai.bot.uncontrolled = false; ai.jumping = true;
    Check(!blizzard.IsAoeReady(pack), "jumping still prevents a Blizzard channel");
    ai.jumping = false; ai.bot.falling = true;
    Check(!blizzard.IsAoeReady(pack), "falling still prevents a Blizzard channel");
    ai.bot.falling = false;
    Check(blizzard.IsAoeReady(pack), "native movement restrictions clear without a tank or threat threshold");

    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
