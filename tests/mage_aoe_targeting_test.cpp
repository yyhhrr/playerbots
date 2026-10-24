#include "playerbot/strategy/mage/MageAoeGeometry.h"
#include "playerbot/strategy/mage/MageAoePolicy.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

// Core objects are reduced to the inputs of the extracted production filters.
// This tests candidate selection, not spell execution or the live world grid.
using uint32 = std::uint32_t;
using uint8 = std::uint8_t;
using Point = ai::MageAoeGeometry::Point;
constexpr uint32 EFFECT_INDEX_0 = 0, EFFECT_INDEX_2 = 2;
constexpr uint32 SPELL_EFFECT_SCHOOL_DAMAGE = 2, SPELL_EFFECT_PERSISTENT_AREA_AURA = 27;
constexpr uint32 UNIT_FIELD_TARGET = 0;
enum SpellEffectIndex { First, Second, Third };

struct ObjectGuid
{
    uint32 value = 0;
    bool operator==(const ObjectGuid&) const = default;
    bool operator<(const ObjectGuid& other) const { return value < other.value; }
    explicit operator bool() const { return value != 0; }
    bool IsPlayer() const { return false; }
};

struct Unit;
struct Player;
struct PlayerbotAI;
using Pet = Unit;
struct ThreatManager
{
    Unit* victim = nullptr;
    std::map<const Unit*, float> threat;
    float getThreat(const Unit* unit) const
    {
        const auto found = threat.find(unit);
        return found == threat.end() ? 0.0f : found->second;
    }
    Unit* getCurrentVictim() const { return victim; }
};

struct SpellEntry
{
    std::array<uint32, 3> Effect{SPELL_EFFECT_PERSISTENT_AREA_AURA, 0, 0};
};

struct Unit
{
    ObjectGuid guid;
    Point position{20.0f, 0.0f, 0.0f};
    bool alive = true, safe = true, legal = true, breakableCC = false;
    bool areaImmune = false, flying = false, totem = false;
    bool damageImmune = false, spellImmune = false, effectImmune = false;
    bool unbreakableCC = false;
    ThreatManager manager;
    bool IsAlive() const { return alive; }
    bool HasBreakableByDamageCrowdControlAura() const { return breakableCC; }
    bool IsAOEImmune() const { return areaImmune; }
    bool IsTaxiFlying() const { return flying; }
    bool IsCreature() const { return true; }
    bool IsTotem() const { return totem; }
    bool IsImmuneToDamage(uint32) const { return damageImmune; }
    bool IsImmuneToSpell(const SpellEntry*, bool, uint8, Player*) const { return spellImmune; }
    bool IsImmuneToSpellEffect(const SpellEntry*, SpellEffectIndex, bool) const { return effectImmune; }
    ObjectGuid GetObjectGuid() const { return guid; }
    ObjectGuid GetGuidValue(uint32) const { return {}; }
    Unit* GetVictim() const { return manager.victim; }
    ThreatManager& getThreatManager() { return manager; }
};
using Creature = Unit;

struct Player : Unit
{
    PlayerbotAI* ai = nullptr;
    Pet* pet = nullptr;
    PlayerbotAI* GetPlayerbotAI() const { return ai; }
    Pet* GetPet() const { return pet; }
    bool CanAttackSpell(const Unit* unit, const SpellEntry*, bool) const { return unit->legal; }
};

struct GuidListValue
{
    std::list<ObjectGuid> guids;
    const std::list<ObjectGuid>& Get() const { return guids; }
};
struct AiObjectContext
{
    std::map<std::string, GuidListValue> values;
    template<class T> GuidListValue* GetValue(const std::string& key) { return &values[key]; }
};
struct PlayerbotAI
{
    AiObjectContext context;
    std::map<uint32, Unit*> units;
    Unit* GetUnit(ObjectGuid guid) { return units.contains(guid.value) ? units.at(guid.value) : nullptr; }
    bool IsSafe(Unit* unit) const { return unit->safe; }
    bool HasActivePlayerMaster() const { return true; }
    ObjectGuid Value(const std::string&) const { return {}; }
};
#define PAI_VALUE(type, name) player->GetPlayerbotAI()->Value(name)

struct ServerFacade
{
    ThreatManager& GetThreatManager(Unit* unit) const { return unit->manager; }
} sServerFacade;

struct AttackersValue
{
    static bool InCombat(Unit*, Player*, bool);
    static bool IsValid(Unit* target, Player* player) { return target && InCombat(target, player, false); }
};

struct PossibleAttackTargetsValue
{
    static bool IsValid(Unit*, Player*, float, bool ignoreCC = false, bool checkAttackerValid = true);
    static bool IsPossibleTarget(Unit* target, Player* player, float range, bool)
    {
        return target && target->legal && ai::MageAoeGeometry::IsWithinRadius(player->position, target->position, range);
    }
    static bool HasIgnoreCCRti(Unit*, Player*) { return false; }
    static bool HasBreakableCC(Unit* target, Player*) { return target->breakableCC; }
    static bool HasUnBreakableCC(Unit* target, Player*) { return target->unbreakableCC; }
};

#include "mage_attackers_in_combat.inc"
#include "mage_possible_attack_targets.inc"

Point GetPoint(Unit* unit) { return unit->position; }
float GetHitRadius(Player*, Unit*, float radius) { return radius; }
uint32 GetSpellSchoolMask(const SpellEntry*) { return 1; }
bool CanHit(Player*, Point center, Unit* target, float radius, bool)
{
    return ai::MageAoeGeometry::IsWithinRadius(center, target->position, radius);
}
bool AlreadyCovered(Player*, const std::vector<Unit*>&, const std::list<int>&) { return false; }

std::vector<Unit*> CollectTargets(Player* bot, PlayerbotAI* ai)
{
    AiObjectContext* context = &ai->context;
    SpellEntry fixtureSpell;
    const SpellEntry* spell = &fixtureSpell;
    const float maxRange = 30.0f, radius = 10.0f;
    float maxHitRadius = radius;
    std::vector<Unit*> targets;
    std::vector<Point> points;
    std::set<ObjectGuid> seen;
#include "mage_aoe_candidates.inc"
    return targets;
}

bool CanPlaceWithoutBreakingCC(Player* bot, PlayerbotAI* ai, Point center)
{
    AiObjectContext* context = &ai->context;
#include "mage_aoe_protected_targets.inc"
    const float radius = 10.0f;
    const bool ignoreLos = false, avoidExistingFlamestrike = false;
    const std::vector<Unit*> covered;
    const std::list<int> existing;
    for (int once : {0})
    {
#include "mage_aoe_protection.inc"
        return true;
    }
    return false;
}

unsigned checks = 0, failures = 0;
void Check(bool passed, const std::string& description)
{
    ++checks;
    if (!passed) { ++failures; std::cerr << "FAILED: " << description << '\n'; }
}

int main()
{
    Player mage, tank;
    PlayerbotAI ai;
    mage.ai = &ai;
    mage.position = {0.0f, 0.0f, 0.0f};
    Unit first, second, third, idle, sheep;
    first.guid = {1}; second.guid = {2}; third.guid = {3}; idle.guid = {4}; sheep.guid = {5};
    std::vector<Unit*> pack{&first, &second, &third};
    for (Unit* target : pack)
    {
        target->manager.victim = &tank;
        target->manager.threat[&tank] = 100.0f;
        ai.units[target->guid.value] = target;
        ai.context.values["attackers"].guids.push_back(target->guid);
    }
    ai.units[4] = &idle; ai.units[5] = &sheep;
    Check(!AttackersValue::InCombat(&first, &mage, false), "fixture reproduces zero personal mage threat");
    Check(CollectTargets(&mage, &ai).size() == 3, "three tank-held enemies count before mage deals any damage");
    first.manager.threat[&mage] = 5.0f;
    Check(CollectTargets(&mage, &ai).size() == 3, "single-target damage on one enemy does not hide its two allies");
    Check(ai::HasMageAoeTargetCount(CollectTargets(&mage, &ai).size()), "three valid group enemies enable AOE");
    ai.context.values["attackers"].guids.pop_back();
    Check(!ai::HasMageAoeTargetCount(CollectTargets(&mage, &ai).size()), "two enemies remain below threshold");
    ai.context.values["attackers"].guids.push_back(third.guid);
    // Keep the following protection checks independent of the personal-threat
    // regression, so the baseline fails specifically at the three start cases.
    for (Unit* target : pack)
        target->manager.threat[&mage] = 5.0f;
    ai.context.values["attackers"].guids.push_back(third.guid);
    Check(CollectTargets(&mage, &ai).size() == 3, "duplicate GUIDs count once");
    third.unbreakableCC = true;
    Check(CollectTargets(&mage, &ai).size() == 3, "non-breakable roots remain eligible for ground AOE");
    third.unbreakableCC = false;
    ai.context.values["possible targets no los"].guids.push_back(idle.guid);
    Check(CollectTargets(&mage, &ai).size() == 3, "nearby idle creature outside group attackers never enters the count");
    ai.context.values["attackers"].guids.push_back(idle.guid);
    Check(CollectTargets(&mage, &ai).size() == 3, "idle target without combat victim or attack target is still rejected");
    for (auto member : {&Unit::alive, &Unit::safe, &Unit::legal})
    {
        third.*member = false;
        Check(CollectTargets(&mage, &ai).size() == 2, "dead, unsafe or illegal target is excluded");
        third.*member = true;
    }
    for (auto member : {&Unit::breakableCC, &Unit::areaImmune, &Unit::flying, &Unit::totem,
        &Unit::damageImmune, &Unit::spellImmune, &Unit::effectImmune})
    {
        third.*member = true;
        Check(CollectTargets(&mage, &ai).size() == 2, "controlled, immune, flying or totem target is excluded");
        third.*member = false;
    }
    third.position = {100.0f, 0.0f, 0.0f};
    Check(CollectTargets(&mage, &ai).size() == 2, "out-of-range target remains excluded");
    third.position = first.position;
    sheep.breakableCC = true;
    sheep.position = first.position;
    ai.context.values["possible targets no los"].guids.push_back(sheep.guid);
    Check(!CanPlaceWithoutBreakingCC(&mage, &ai, first.position), "AOE placement cannot hit a sheep beside the tank-held pack");
    sheep.position = {60.0f, 0.0f, 0.0f};
    Check(CanPlaceWithoutBreakingCC(&mage, &ai, first.position), "distant sheep does not forbid an unrelated pack");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
