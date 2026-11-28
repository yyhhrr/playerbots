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
constexpr uint32 SPELL_ATTR_EX2_IGNORE_LINE_OF_SIGHT = 1;
constexpr uint32 SPELLMOD_RANGE = 1;
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
    uint32 Id = 5740, rangeIndex = 4;
    std::array<uint32, 3> Effect{SPELL_EFFECT_PERSISTENT_AREA_AURA, 0, 0};
    bool HasAttribute(uint32) const { return false; }
};

struct Unit
{
    ObjectGuid guid;
    Point position{20.0f, 0.0f, 0.0f};
    bool alive = true, safe = true, legal = true, breakableCC = false;
    bool areaImmune = false, flying = false, totem = false;
    bool damageImmune = false, spellImmune = false, effectImmune = false;
    bool unbreakableCC = false, visible = true;
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
using WorldObject = Unit;

struct Player : Unit
{
    PlayerbotAI* ai = nullptr;
    Pet* pet = nullptr;
    bool groundVisible = true;
    PlayerbotAI* GetPlayerbotAI() const { return ai; }
    Pet* GetPet() const { return pet; }
    bool CanAttackSpell(const Unit* unit, const SpellEntry*, bool) const { return unit->legal; }
    bool IsInWorld() const { return true; }
    bool IsBeingTeleported() const { return false; }
    Player* GetSpellModOwner() const { return nullptr; }
    void ApplySpellMod(uint32, uint32, float&, bool) {}
    void UpdateAllowedPositionZ(float, float, float& z) const { z = 0.0f; }
    bool IsWithinLOS(float, float, float) const { return groundVisible; }
    unsigned GetMapId() const { return 1; }
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
    Player* GetBot() const { return bot; }
    AiObjectContext* GetAiObjectContext() { return &context; }
    Player* bot = nullptr;
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
    SpellEntry spell;
    const SpellEntry* LookupSpellInfo(uint32 id) const { return id ? &spell : nullptr; }
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
    return target->visible && ai::MageAoeGeometry::IsWithinRadius(center, target->position, radius);
}
using DynamicObject = int;
bool AlreadyCovered(Player*, const std::vector<Unit*>&, const std::list<DynamicObject*>&) { return false; }

struct WorldLocation
{
    unsigned mapId = 0;
    float coord_x = 0, coord_y = 0, coord_z = 0, orientation = 0;
};
struct OwnGroundSpellCheck { OwnGroundSpellCheck(Player*, float, uint32) {} };
namespace MaNGOS
{
    template <class Check> struct DynamicObjectListSearcher
    {
        DynamicObjectListSearcher(std::list<DynamicObject*>&, Check&) {}
    };
}
namespace Cell
{
    template <class Searcher> void VisitAllObjects(const WorldObject*, Searcher&, float) {}
}
struct SpellRangeStore
{
    float LookupEntry(uint32) const { return 30.0f; }
} sSpellRangeStore;
struct SpellManager
{
    uint32 GetFirstSpellInChain(uint32 id) const { return id; }
} sSpellMgr;
float GetSpellMaxRange(float range) { return range; }
float GetDamageRadius(Player*, const SpellEntry*) { return 8.0f; }

namespace ai
{
    struct GroundAoeTarget
    {
        WorldLocation position;
        std::vector<ObjectGuid> targets;
        float radius = 0.0f;
    };
    bool FindGroundAoeTarget(PlayerbotAI*, uint32, GroundAoeTarget&, size_t, bool = false);
}
using namespace ai;
#include "spell_aoe_ground_target.inc"

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
    const bool ignoreLos = false, avoidExistingArea = false;
    const std::vector<Unit*> covered;
    const std::list<DynamicObject*> existing;
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
    ai.bot = &mage;
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

    GroundAoeTarget rain;
    for (Unit* target : pack)
        target->position = {37.0f, 0.0f, 0.0f};
    Check(FindGroundAoeTarget(&ai, 5740, rain, 3) && rain.targets.size() == 3 &&
        MageAoeGeometry::IsWithinRadius(mage.position,
            {rain.position.coord_x, rain.position.coord_y, rain.position.coord_z}, 30.0f),
        "Rain of Fire can cover three targets at a destination inside its 30-yard range");
    third.position.x = 39.0f;
    Check(!FindGroundAoeTarget(&ai, 5740, rain, 3), "a third enemy outside the real spell footprint cannot start Rain of Fire");
    third.position.x = 37.0f;
    third.visible = false;
    Check(!FindGroundAoeTarget(&ai, 5740, rain, 3), "a third enemy behind terrain cannot start Rain of Fire");
    third.visible = true;
    mage.groundVisible = false;
    Check(!FindGroundAoeTarget(&ai, 5740, rain, 3), "Rain of Fire requires line of sight to its destination");
    mage.groundVisible = true;
    sheep.position = third.position;
    Check(!FindGroundAoeTarget(&ai, 5740, rain, 3), "Rain of Fire does not start when every useful landing breaks sheep");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
