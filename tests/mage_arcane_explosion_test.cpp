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
enum SpellEffectIndex { First, Second, Third };

struct ObjectGuid
{
    uint32 value = 0;
    bool operator==(const ObjectGuid&) const = default;
    bool operator<(const ObjectGuid& other) const { return value < other.value; }
};
struct SpellEntry
{
    std::array<uint32, 3> Effect{SPELL_EFFECT_SCHOOL_DAMAGE, 0, 0};
    float radius = 10.0f;
    bool ignoreLos = false;
    bool HasAttribute(uint32) const { return ignoreLos; }
};
struct Player;
struct Unit
{
    ObjectGuid guid;
    Point position{};
    bool alive = true, safe = true, legal = true, player = false;
    bool breakableCC = false, areaImmune = false, flying = false, totem = false;
    bool damageImmune = false, spellImmune = false, effectImmune = false, visible = true;
    float reach = 0.0f;
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
    bool inWorld = true, teleporting = false;
    unsigned GetLevel() const { return level; }
    unsigned getClass() const { return playerClass; }
    bool IsInWorld() const { return inWorld; }
    bool IsBeingTeleported() const { return teleporting; }
    bool CanAttackSpell(Unit* unit, const SpellEntry*, bool = false) const { return unit->legal; }
};
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
        // The generic core rejects negative spells explicitly aimed at oneself.
        return target && target != &bot && target->legal;
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
}
#include "mage_arcane_explosion_target.inc"

struct Event {};
struct CastSpellAction
{
    PlayerbotAI* ai;
    Player* bot;
    AiObjectContext* context;
    CastSpellAction(PlayerbotAI* ai, const std::string&) : ai(ai), bot(ai->GetBot()), context(ai->GetAiObjectContext()) {}
    virtual ~CastSpellAction() = default;
    virtual std::string getName() { return "arcane explosion"; }
    std::string GetSpellName() const { return "arcane explosion"; }
    virtual bool isUseful() { return GetTarget() != nullptr; }
    virtual bool isPossible() { return GetTarget() == bot; }
    virtual bool Execute(Event&) { if (GetTarget() != bot || !isPossible()) return false; ++ai->casts; return true; }
protected:
    virtual Unit* GetTarget() { return nullptr; }
    virtual std::string GetTargetName() { return "current target"; }
    virtual std::string GetReachActionName() { return "reach spell"; }
};
namespace ai
{
#include "mage_arcane_explosion_actions.inc"
}

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
            sServerFacade.spell = {};
        }
        Unit* Target(bool leveling = true) { return ai::FindMageArcaneExplosionTarget(&ai, leveling); }
    };
}

int main()
{
    {
        Fixture f;
        Check(f.Target() == &f.first, "three enemies at 6-10 yards trigger leveling Arcane Explosion");
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
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
