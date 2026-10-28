#include "playerbot/strategy/mage/MageAoeGeometry.h"
#include "playerbot/strategy/mage/MageAoePolicy.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

using uint32 = std::uint32_t;
using uint8 = std::uint8_t;
using Point = ai::MageAoeGeometry::Point;
constexpr uint32 CLASS_MAGE = 8, EFFECT_INDEX_0 = 0, EFFECT_INDEX_2 = 2;
constexpr uint32 SPELL_EFFECT_SCHOOL_DAMAGE = 2, SPELL_EFFECT_PERSISTENT_AREA_AURA = 27;
constexpr uint32 SPELL_ATTR_EX2_IGNORE_LINE_OF_SIGHT = 1;
constexpr uint32 TRIGGERED_IGNORE_CURRENT_CASTED_SPELL = 2;
constexpr uint32 UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL = 4, POWER_MANA = 0;
enum CurrentSpellTypes { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL };
enum SpellState { SPELL_STATE_CASTING, SPELL_STATE_CHANNELING, SPELL_STATE_FINISHED };
enum SpellCastResult { SPELL_CAST_OK, SPELL_FAILED_NATIVE };
enum SpellEffectIndex { First, Second, Third };

struct ObjectGuid
{
    uint32 value = 0;
    bool operator==(const ObjectGuid&) const = default;
    bool operator<(const ObjectGuid& other) const { return value < other.value; }
};
struct SpellEntry
{
    uint32 Id = 1449, powerCost = 100;
    std::array<uint32, 3> Effect{SPELL_EFFECT_SCHOOL_DAMAGE, 0, 0};
    float radius = 10.0f;
    bool ignoreLos = false;
    bool HasAttribute(uint32) const { return ignoreLos; }
};
struct Player;
struct Spell;
struct Unit
{
    ObjectGuid guid;
    Point position{};
    bool alive = true, safe = true, legal = true, player = false;
    bool breakableCC = false, areaImmune = false, flying = false, totem = false;
    bool damageImmune = false, spellImmune = false, effectImmune = false, visible = true;
    float reach = 0.0f;
    Unit* victim = nullptr;
    bool rooted = false;
    Unit* GetVictim() const { return victim; }
    bool IsAlive() const { return alive; }
    bool IsPlayer() const { return player; }
    bool IsCreature() const { return !player; }
    bool IsTotem() const { return totem; }
    bool HasBreakableByDamageCrowdControlAura() const { return breakableCC; }
    bool IsAOEImmune() const { return areaImmune; }
    bool IsTaxiFlying() const { return flying; }
    bool IsImmuneToDamage(uint32) const { return damageImmune; }
    bool IsImmuneToSpell(const SpellEntry*, bool, uint8, Player*) const { return spellImmune; }
    bool IsImmuneToSpellEffect(const SpellEntry*, SpellEffectIndex, bool) const { return effectImmune; }
    ObjectGuid GetObjectGuid() const { return guid; }
};
using Creature = Unit;
using WorldObject = Unit;
struct Player : Unit
{
    unsigned level = 59, playerClass = CLASS_MAGE;
    std::array<Spell*, 2> currentSpells{};
    bool spellReady = true, globalCooldown = false, uncontrolled = false, nativeCastAllowed = true;
    uint32 mana = 1000, lastProbeFlags = 0;
    Unit* lastProbeTarget = nullptr;
    Spell* GetCurrentSpell(CurrentSpellTypes slot) const { return currentSpells[slot]; }
    bool IsSpellReady(uint32) const { return spellReady; }
    bool HasGCD(const SpellEntry*) const { return globalCooldown; }
    bool hasUnitState(uint32) const { return uncontrolled; }
    uint32 GetPower(uint32) const { return mana; }
    bool inWorld = true, teleporting = false;
    unsigned GetLevel() const { return level; }
    unsigned getClass() const { return playerClass; }
    bool IsInWorld() const { return inWorld; }
    bool IsBeingTeleported() const { return teleporting; }
    bool CanAttackSpell(Unit* unit, const SpellEntry*, bool = false) const { return unit->legal; }
};
struct SpellCastTargets
{
    Unit* target = nullptr;
    void setUnitTarget(Unit* unit) { target = unit; }
};
struct Spell
{
    const SpellEntry* m_spellInfo;
    SpellState state = SPELL_STATE_CASTING;
    SpellCastTargets m_targets;
    Player* caster = nullptr;
    Spell(const SpellEntry* info, SpellState state = SPELL_STATE_CASTING) : m_spellInfo(info), state(state) {}
    Spell(Player* caster, const SpellEntry* info, uint32 flags) : m_spellInfo(info), caster(caster)
    {
        caster->lastProbeFlags = flags;
    }
    SpellState getState() const { return state; }
    static uint32 CalculatePowerCost(const SpellEntry* info, Player*) { return info->powerCost; }
    SpellCastResult CheckCast(bool)
    {
        caster->lastProbeTarget = m_targets.target;
        return caster->nativeCastAllowed ? SPELL_CAST_OK : SPELL_FAILED_NATIVE;
    }
};
struct SpellMgr
{
    uint32 GetFirstSpellInChain(uint32 id) const
    {
        if (id == 10161) return 10;
        if (id == 10216) return 2120;
        return id;
    }
} sSpellMgr;

template<class T> struct Value
{
    T data{};
    const T& Get() const { return data; }
};
struct AiObjectContext
{
    Value<std::list<ObjectGuid>> attackers;
    Value<uint32> spellId{1449};
    template<class T> Value<T>* GetValue(const std::string&, const std::string& = "")
    {
        if constexpr (std::is_same_v<T, uint32>) return &spellId;
        else return &attackers;
    }
};
struct PlayerbotAI
{
    Player bot;
    AiObjectContext context;
    std::map<uint32, Unit*> units;
    unsigned casts = 0;
    unsigned interruptions = 0;
    bool knownSpell = true, spellUseful = true;
    bool HasSpell(uint32 id) const { return id && knownSpell; }
    void InterruptSpell()
    {
        ++interruptions;
        bot.currentSpells.fill(nullptr);
    }
    bool pvp = false, realPlayer = false;
    bool IsInPvp() const { return pvp; }
    bool IsRealPlayer() const { return realPlayer; }
    Unit* checkedTarget = nullptr;
    bool checkedIgnoreRange = false;
    Player* GetBot() { return &bot; }
    AiObjectContext* GetAiObjectContext() { return &context; }
    Unit* GetUnit(ObjectGuid guid) { return units.contains(guid.value) ? units.at(guid.value) : nullptr; }
    bool IsSafe(Unit* target) const { return target->safe; }
    bool CanCastSpell(const std::string&, Unit* target, uint32, void*, bool ignoreRange)
    {
        checkedTarget = target;
        checkedIgnoreRange = ignoreRange;
        // The native preflight rejects concurrent casts and all ordinary restrictions.
        bool casting = false;
        for (Spell* current : bot.currentSpells)
            casting = casting || (current && current->getState() != SPELL_STATE_FINISHED);
        return target && target != &bot && target->legal && !casting && knownSpell &&
            bot.spellReady && !bot.globalCooldown && !bot.uncontrolled && bot.mana >= 100 && bot.nativeCastAllowed;
    }
};
#define AI_VALUE2(type, name, qualifier) ai->GetAiObjectContext()->GetValue<type>(name, qualifier)->Get()
struct ServerFacade
{
    SpellEntry spell;
    const SpellEntry* LookupSpellInfo(uint32 id) { return id ? &spell : nullptr; }
} sServerFacade;
std::vector<Unit*> worldUnits;
namespace MaNGOS
{
    struct AnyUnitInObjectRangeCheck
    {
        AnyUnitInObjectRangeCheck(Player*, float) {}
        bool operator()(Unit* unit) const { return unit->IsAlive(); }
    };
    template<class Check> struct UnitListSearcher
    {
        std::list<Unit*>& units;
        Check& check;
        UnitListSearcher(std::list<Unit*>& units, Check& check) : units(units), check(check) {}
    };
}
namespace Cell
{
    template<class Object, class Check> void VisitAllObjects(Object*, MaNGOS::UnitListSearcher<Check>& searcher, float)
    {
        for (Unit* unit : worldUnits)
            if (searcher.check(unit)) searcher.units.push_back(unit);
    }
}
struct PossibleAttackTargetsValue
{
    static bool IsValid(Unit* unit, Player*, float, bool, bool) { return unit->legal; }
};
Point GetPoint(Unit* unit) { return unit->position; }
float GetHitRadius(Player*, Unit* unit, float radius) { return radius + (unit->IsPlayer() ? 0.0f : unit->reach); }
float GetDamageRadius(Player*, const SpellEntry* spell) { return spell->radius; }
uint32 GetSpellSchoolMask(const SpellEntry*) { return 1; }
bool CanHit(Player* bot, Point center, Unit* target, float radius, bool ignoreLos)
{
    return ai::MageAoeGeometry::IsWithinRadius(center, target->position, GetHitRadius(bot, target, radius)) &&
        (ignoreLos || target->visible);
}
namespace ai
{
    Unit* FindMageArcaneExplosionTarget(PlayerbotAI*, bool levelingOnly = false);
    bool IsMageAoeAreaSafe(PlayerbotAI*, uint32, float, float, float);
}
#include "mage_aoe_area_safe.inc"
#include "mage_arcane_explosion_target.inc"

struct Event {};
struct CastSpellAction
{
    PlayerbotAI* ai;
    Player* bot;
    AiObjectContext* context;
    CastSpellAction(PlayerbotAI* ai, const std::string&) : ai(ai), bot(ai->GetBot()), context(ai->GetAiObjectContext()) {}
    virtual ~CastSpellAction() = default;
    virtual bool ShouldReactionInterruptCast() const { return true; }
    virtual std::string getName() { return "arcane explosion"; }
    std::string GetSpellName() const { return "arcane explosion"; }
    uint32 GetSpellID() const { return context->spellId.data; }
    virtual bool isUseful() { return ai->spellUseful && GetTarget() != nullptr; }
    virtual bool isPossible() { return GetTarget() == bot; }
    virtual bool Execute(Event&) { if (GetTarget() != bot || !isPossible()) return false; ++ai->casts; return true; }
protected:
    virtual Unit* GetTarget() { return nullptr; }
    virtual std::string GetTargetName() { return "current target"; }
    virtual std::string GetReachActionName() { return "reach spell"; }
};
struct Trigger
{
    PlayerbotAI* ai;
    Trigger(PlayerbotAI* ai, const std::string&) : ai(ai) {}
    virtual ~Trigger() = default;
    virtual bool IsActive() { return false; }
    virtual Unit* GetTarget() { return nullptr; }
};
namespace ai
{
#include "mage_arcane_explosion_actions.inc"
#include "mage_ot_arcane_explosion_trigger_class.inc"
}
using namespace ai;
#include "mage_ot_arcane_explosion_action.inc"
#include "mage_ot_arcane_explosion_trigger.inc"

namespace
{
    unsigned checks = 0, failures = 0;
    void Check(bool condition, const std::string& description)
    {
        ++checks;
        if (!condition) { ++failures; std::cerr << "FAILED: " << description << '\n'; }
    }
    struct Fixture
    {
        PlayerbotAI ai;
        Unit first, second, third, sheep;
        Fixture()
        {
            first.guid = {1}; second.guid = {2}; third.guid = {3}; sheep.guid = {4};
            first.position = {6.0f, 0.0f, 0.0f}; second.position = {8.0f, 0.0f, 0.0f}; third.position = {10.0f, 0.0f, 0.0f};
            sheep.position = {9.0f, 0.0f, 0.0f};
            for (Unit* unit : {&first, &second, &third, &sheep}) ai.units[unit->guid.value] = unit;
            ai.context.attackers.data = {first.guid, second.guid, third.guid};
            worldUnits = {&first, &second, &third, &sheep};
            first.victim = &ai.bot;
            sServerFacade.spell = {};
        }
        Unit* Target(bool leveling = true) { return ai::FindMageArcaneExplosionTarget(&ai, leveling); }
    };
}

int main()
{
    {
        Fixture f;
        Check(f.Target() == &f.first, "three enemies at 6-10 yards with a real mage pursuer permit Arcane Explosion");
        f.ai.context.attackers.data.pop_back();
        Check(!f.Target(), "two enemies do not trigger");
        f.ai.context.attackers.data.push_back(f.second.guid);
        Check(!f.Target(), "duplicate GUIDs do not count as a third enemy");
        f.ai.context.attackers.data.push_back(f.third.guid);
        Check(f.Target(), "three unique enemies still trigger with duplicate input");
        f.ai.bot.level = 60;
        Check(!f.Target(), "level sixty remains excluded from the new leveling branch");
        Check(f.Target(false), "the original Arcane Explosion branch still supports level sixty");
        f.ai.bot.level = 61;
        Check(!f.Target(), "levels above sixty remain excluded from the new branch");
    }
    {
        Fixture f;
        f.first.victim = nullptr;
        Check(!f.Target(false), "three nearby enemies attacking other party members do not cause automatic Arcane Explosion");
        f.second.victim = &f.ai.bot;
        Check(f.Target(false), "one actual mage pursuer among three effective close enemies permits the explosion");
        f.ai.bot.level = 60;
        Check(f.Target(false), "automatic OT Arcane Explosion remains available at level sixty");
        f.second.position.x = 25;
        Check(!f.Target(false), "an OT enemy beyond the blast cannot authorize three other nearby targets");
        f.ai.context.attackers.data.push_back(f.sheep.guid);
        Check(!f.Target(false), "three nearby non-OT enemies plus a distant OT enemy still prefer ranged output");
        f.second.position.x = 8;
        f.first.rooted = f.second.rooted = f.third.rooted = true;
        Check(f.Target(false), "Frost Nova roots do not conceal a genuinely over-threat close pack");
        f.ai.context.attackers.data = {f.second.guid, f.third.guid};
        Check(!f.Target(false), "one or two nearby targets still do not authorize OT Arcane Explosion");
    }
    {
        Fixture f;
        f.third.position.x = 10.01f;
        Check(!f.Target(), "a third enemy beyond ten yards does not trigger");
        f.third.position = {0.0f, 0.0f, 10.0f};
        Check(f.Target(), "the three-dimensional ten-yard boundary counts");
        f.third.position.z = 10.01f;
        Check(!f.Target(), "a third enemy on another floor outside the radius does not count");
        f.third.position = {11.0f, 0.0f, 0.0f}; f.third.reach = 1.0f;
        Check(f.Target(), "NPC combat reach matches the core's AOE footprint");
    }
    {
        Fixture f;
        Check(!ai::FindMageArcaneExplosionTarget(nullptr, true), "a missing AI cannot select an explosion target");
        f.ai.bot.alive = false;
        Check(!f.Target(), "a dead caster cannot trigger");
        f.ai.bot.alive = true; f.ai.bot.inWorld = false;
        Check(!f.Target(), "an out-of-world caster cannot trigger");
        f.ai.bot.inWorld = true; f.ai.bot.teleporting = true;
        Check(!f.Target(), "a teleporting caster cannot trigger");
        f.ai.bot.teleporting = false; f.ai.bot.playerClass = 1;
        Check(!f.Target(), "the new rule applies only to mages");
        f.ai.bot.playerClass = CLASS_MAGE; f.ai.context.spellId.data = 0;
        Check(!f.Target(), "a caster without a known Arcane Explosion cannot trigger");
        f.ai.context.spellId.data = 1449; sServerFacade.spell.radius = 0.0f;
        Check(!f.Target(), "a missing damaging radius prevents the explosion");
        sServerFacade.spell.radius = 8.0f;
        Check(!f.Target(), "an actual spell footprint smaller than ten yards caps the count");
    }
    for (auto flag : {&Unit::alive, &Unit::safe, &Unit::legal, &Unit::visible})
    {
        Fixture f;
        f.third.*flag = false;
        Check(!f.Target(), "dead, unsafe, illegal and obstructed enemies do not count");
    }
    for (auto flag : {&Unit::player, &Unit::breakableCC, &Unit::areaImmune, &Unit::flying,
        &Unit::totem, &Unit::damageImmune, &Unit::spellImmune, &Unit::effectImmune})
    {
        Fixture f;
        f.third.*flag = true;
        Check(!f.Target(), "players, controlled, immune, flying and totem units do not count");
    }
    {
        Fixture f;
        f.sheep.breakableCC = true;
        Check(!f.Target(), "a nearby sheep outside the attacker list blocks the whole explosion");
        Check(!f.Target(false), "the original Arcane Explosion branch shares sheep protection");
        f.sheep.position.x = 10.01f;
        Check(f.Target(), "a sheep outside the explosion radius does not block");
        f.sheep.reach = 1.0f;
        Check(!f.Target(), "a sheep whose NPC combat reach intersects the explosion remains protected");
        f.sheep.reach = 0.0f; f.sheep.position.x = 9.0f; f.sheep.legal = false;
        Check(f.Target(), "a friendly controlled unit does not block a hostile explosion");
        f.sheep.legal = true; f.sheep.alive = false;
        Check(f.Target(), "a dead controlled unit does not block");
        f.sheep.alive = true; f.sheep.visible = false;
        Check(f.Target(), "an obstructed sheep that the spell cannot hit does not block");
        sServerFacade.spell.ignoreLos = true;
        Check(!f.Target(), "a spell that ignores LOS protects the obstructed sheep too");
        sServerFacade.spell.ignoreLos = false; sServerFacade.spell.radius = 12.0f;
        f.sheep.visible = true; f.sheep.position.x = 11.0f;
        Check(!f.Target(), "an expanded spell footprint protects sheep outside the ten-yard count radius");
    }
    {
        Fixture f;
        f.ai.context.attackers.data.push_back(f.sheep.guid);
        for (auto flag : {&Unit::safe, &Unit::legal, &Unit::visible, &Unit::alive})
        {
            f.first.*flag = false;
            Check(!f.Target(false), "an invalid OT enemy cannot authorize three other effective nearby enemies");
            f.first.*flag = true;
        }
        f.first.breakableCC = true;
        Check(!f.Target(false), "a controlled OT enemy cannot authorize the explosion");
    }
    {
        Fixture f;
        f.ai.bot.level = 60;
        MageOtArcaneExplosionTrigger trigger(&f.ai);
        CastMageOtArcaneExplosionAction action(&f.ai);
        Check(trigger.IsActive() && action.isUseful() && action.isPossible(),
            "the actual automatic OT trigger and action are available at level sixty");
        Check(action.getName() == "mage ot arcane explosion", "the automatic action has its dedicated context name");
        CastArcaneExplosionAction legacy(&f.ai);
        Check(legacy.ShouldReactionInterruptCast(), "the native spell-action default requests reaction cast interruption");
        Check(!action.ShouldReactionInterruptCast(), "automatic OT output defers interruption until its own execution rechecks succeed");
        f.first.victim = nullptr;
        Check(!trigger.IsActive() && !action.isUseful() && !action.isPossible(),
            "the real trigger and action require actual over-threat rather than nearby group combat");
    }
    {
        Fixture f;
        f.ai.bot.level = 60;
        const SpellEntry blizzard{10161};
        Spell channel(&blizzard, SPELL_STATE_CHANNELING);
        f.ai.bot.currentSpells[CURRENT_CHANNELED_SPELL] = &channel;
        CastMageOtArcaneExplosionAction action(&f.ai);
        Check(action.isUseful() && action.isPossible(), "a real nearby OT pack can replace an ongoing ranked Blizzard");
        Check(f.ai.bot.lastProbeFlags == TRIGGERED_IGNORE_CURRENT_CASTED_SPELL &&
            f.ai.bot.lastProbeTarget == &f.ai.bot,
            "the probe ignores only the current cast and validates the actual self-centered explosion");
        Event event;
        Check(action.Execute(event) && f.ai.interruptions == 1 && f.ai.casts == 1,
            "execution interrupts Blizzard once and actually casts the explosion");
        f.ai.bot.currentSpells[CURRENT_CHANNELED_SPELL] = &channel;
        f.first.victim = nullptr;
        Check(!action.Execute(event) && f.ai.interruptions == 1,
            "loss of real OT before execution leaves the current channel untouched");
        f.first.victim = &f.ai.bot; f.sheep.breakableCC = true;
        Check(!action.Execute(event) && f.ai.interruptions == 1,
            "a newly controlled unit prevents interruption and protects the current channel");
    }
    {
        Fixture f;
        const SpellEntry flamestrike{10216}, frostbolt{116}, missiles{5143};
        Spell ground(&flamestrike), single(&frostbolt), otherChannel(&missiles, SPELL_STATE_CHANNELING);
        CastMageOtArcaneExplosionAction action(&f.ai);
        f.ai.bot.currentSpells[CURRENT_GENERIC_SPELL] = &ground;
        Check(action.isUseful() && action.isPossible(), "a ranked Flamestrike cast can be replaced by real OT output");
        f.ai.bot.currentSpells[CURRENT_GENERIC_SPELL] = &single;
        Check(!action.isUseful() && !action.isPossible(), "a normal single-target cast is not interrupted by automatic OT output");
        f.ai.bot.currentSpells[CURRENT_GENERIC_SPELL] = &ground;
        f.ai.bot.currentSpells[CURRENT_CHANNELED_SPELL] = &otherChannel;
        Event event;
        Check(!action.Execute(event) && f.ai.interruptions == 0,
            "a simultaneous unrelated channel prevents replacement of either current cast");
        otherChannel.state = SPELL_STATE_FINISHED;
        Check(action.isPossible(), "a finished unrelated channel does not block replacement of the active ground cast");
    }
    for (unsigned changedState = 0; changedState < 3; ++changedState)
    {
        Fixture f;
        const SpellEntry blizzard{10161};
        Spell channel(&blizzard, SPELL_STATE_CHANNELING);
        f.ai.bot.currentSpells[CURRENT_CHANNELED_SPELL] = &channel;
        CastMageOtArcaneExplosionAction action(&f.ai);
        Check(action.isUseful() && action.isPossible(), "a queued OT reaction initially passes target and native spell checks");
        if (changedState == 0) f.ai.context.attackers.data.pop_back();
        if (changedState == 1) f.ai.bot.mana = 99;
        if (changedState == 2) f.ai.bot.globalCooldown = true;
        Event event;
        Check(!action.Execute(event) && f.ai.interruptions == 0 && f.ai.casts == 0 &&
            f.ai.bot.currentSpells[CURRENT_CHANNELED_SPELL] == &channel,
            "a selected OT action rechecks fewer targets, lost mana and new GCD before interrupting Blizzard");
    }
    for (unsigned restriction = 0; restriction < 7; ++restriction)
    {
        Fixture f;
        const SpellEntry blizzard{10161};
        Spell channel(&blizzard, SPELL_STATE_CHANNELING);
        f.ai.bot.currentSpells[CURRENT_CHANNELED_SPELL] = &channel;
        if (restriction == 0) f.ai.knownSpell = false;
        if (restriction == 1) f.ai.bot.spellReady = false;
        if (restriction == 2) f.ai.bot.globalCooldown = true;
        if (restriction == 3) f.ai.bot.uncontrolled = true;
        if (restriction == 4) f.ai.bot.mana = 99;
        if (restriction == 5) f.ai.bot.nativeCastAllowed = false;
        if (restriction == 6) f.ai.spellUseful = false;
        CastMageOtArcaneExplosionAction action(&f.ai);
        Event event;
        Check(!action.Execute(event) && f.ai.interruptions == 0 && f.ai.casts == 0,
            "unknown, cooldown, GCD, control, mana and native restrictions preserve the current Blizzard");
    }
    {
        Fixture f;
        ai::CastLevelingArcaneExplosionAction action(&f.ai);
        Event event;
        Check(action.isUseful(), "the leveling action accepts three enemies beyond melee reach");
        Check(action.isPossible(), "spell preflight succeeds for enemies at 6-10 yards");
        Check(f.ai.checkedTarget == &f.first && f.ai.checkedIgnoreRange,
            "preflight uses a valid enemy with no irrelevant melee or target range check");
        f.sheep.breakableCC = true;
        Check(!action.Execute(event) && f.ai.casts == 0, "execution rechecks a sheep applied after the trigger");
        f.sheep.breakableCC = false;
        Check(action.Execute(event) && f.ai.casts == 1, "safe enemies at 6-10 yards can actually be exploded");
        f.ai.context.attackers.data.pop_back();
        Check(!action.Execute(event) && f.ai.casts == 1, "execution rechecks the count when the third enemy has left");
        f.ai.context.attackers.data.push_back(f.third.guid);
        f.ai.bot.level = 60;
        Check(!action.isUseful(), "the leveling action remains disabled at sixty");
        Check(!action.isPossible(), "the leveling preflight remains disabled at sixty");
        Check(!action.Execute(event) && f.ai.casts == 1, "direct leveling execution cannot bypass the level cutoff");
        ai::CastArcaneExplosionAction normal(&f.ai);
        Check(normal.isUseful() && normal.Execute(event), "ordinary Arcane Explosion remains usable at sixty");
        f.sheep.breakableCC = true;
        Check(!normal.isUseful() && !normal.Execute(event), "ordinary action also checks crowd control at decision and execution");
    }
    {
        Fixture f;
        f.ai.context.attackers.data.clear();
        Check(ai::IsMageAoeAreaSafe(&f.ai, 10, 25.0f, 0.0f, 0.0f),
            "an ongoing fixed-area channel needs no new three-enemy pack");
        f.sheep.breakableCC = true; f.sheep.position = {25.0f, 0.0f, 0.0f};
        Check(!ai::IsMageAoeAreaSafe(&f.ai, 10, 25.0f, 0.0f, 0.0f),
            "a sheep at a remote current destination remains protected outside the attack list");
        f.sheep.position.x = 2.0f;
        Check(ai::IsMageAoeAreaSafe(&f.ai, 10, 25.0f, 0.0f, 0.0f),
            "a sheep next to the caster does not cancel a distant safe channel");
        f.sheep.position = {35.01f, 0.0f, 0.0f};
        Check(ai::IsMageAoeAreaSafe(&f.ai, 10, 25.0f, 0.0f, 0.0f),
            "a sheep outside the current destination radius does not block commitment");
        f.sheep.position = {25.0f, 0.0f, 10.01f};
        Check(ai::IsMageAoeAreaSafe(&f.ai, 10, 25.0f, 0.0f, 0.0f),
            "fixed-area safety uses three-dimensional separation from the actual destination");
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
