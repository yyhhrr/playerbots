#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

using uint32 = std::uint32_t;
using SkillType = uint32;
constexpr uint32 SKILL_NONE = 0, SKILL_SKINNING = 1;
constexpr uint32 UNIT_DYNAMIC_FLAGS = 1, UNIT_FIELD_FLAGS = 2;
constexpr uint32 UNIT_DYNFLAG_LOOTABLE = 1, UNIT_FLAG_SKINNABLE = 2;
constexpr unsigned CORPSE = 1;
struct ObjectGuid
{
    std::uint64_t raw = 0;
    ObjectGuid(std::uint64_t value = 0) : raw(value) {}
    explicit operator bool() const { return raw != 0; }
    std::uint64_t GetRawValue() const { return raw; }
    bool operator==(ObjectGuid other) const { return raw == other.raw; }
    bool operator!=(ObjectGuid other) const { return raw != other.raw; }
    bool operator<(ObjectGuid other) const { return raw < other.raw; }
};
constexpr float INTERACTION_DISTANCE = 5.0f;
constexpr unsigned MAX_LOOT_OBJECT_COUNT = 10;
struct PlayerbotAI;
struct Player;
struct Group { bool raid = false; };
struct WorldObject
{
    ObjectGuid guid;
    float x = 0, y = 0, z = 0;
    uint32 map = 1, instance = 1;
    bool valid = true, possible = true, safe = true;
    virtual ~WorldObject() = default;
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    float GetPositionZ() const { return z; }
    uint32 GetMapId() const { return map; }
    uint32 GetInstanceId() const { return instance; }
    ObjectGuid GetObjectGuid() const { return guid; }
};
struct Unit : WorldObject
{
    bool player = false, taxi = false, flying = false;
    void* transport = nullptr;
    bool IsPlayer() const { return player; }
    bool IsTaxiFlying() const { return taxi; }
    bool IsFlying() const { return flying; }
    void* GetTransport() const { return transport; }
};
struct CreatureInfo
{
    uint32 GetRequiredLootSkill() const { return SKILL_SKINNING; }
};
struct Creature : WorldObject
{
    bool lootable = true, skinnable = false;
    unsigned deathState = CORPSE;
    CreatureInfo info;
    bool HasFlag(uint32 field, uint32 flag) const
    {
        return field == UNIT_DYNAMIC_FLAGS && flag == UNIT_DYNFLAG_LOOTABLE ? lootable :
            field == UNIT_FIELD_FLAGS && flag == UNIT_FLAG_SKINNABLE && skinnable;
    }
    const CreatureInfo* GetCreatureInfo() const { return &info; }
    uint32 GetLevel() const { return 30; }
};
static float Distance(const WorldObject* left, const WorldObject* right)
{
    const float dx = left->x - right->x, dy = left->y - right->y, dz = left->z - right->z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
struct Player : Unit
{
    PlayerbotAI* ai = nullptr;
    Group* group = nullptr;
    bool alive = true, combat = false, teleporting = false, casting = false;
    Player() { player = true; }
    PlayerbotAI* GetPlayerbotAI() const { return ai; }
    Group* GetGroup() const { return group; }
    bool IsInCombat() const { return combat; }
    bool IsBeingTeleported() const { return teleporting; }
    bool IsNonMeleeSpellCasted(bool) const { return casting; }
    uint32 GetSkillValue(uint32) const { return 300; }
    float GetDistance(const WorldObject* other) const { return Distance(this, other); }
    bool IsWithinDistInMap(const WorldObject* other, float distance) const
    {
        return other && map == other->map && instance == other->instance && Distance(this, other) <= distance;
    }
};
std::map<ObjectGuid, WorldObject*> world;
namespace WorldTimer
{
    uint32 now = 0;
    uint32 getMSTime() { return now; }
}

#include "loot_task_declarations.inc"

template<class T> struct TestValue
{
    T value{};
    unsigned resets = 0;
    T Get() const { return value; }
    void Set(T next) { value = next; }
    void Reset() { ++resets; }
};
namespace ai
{
    class AiObjectContext
    {
    public:
        TestValue<LootObject> target;
        TestValue<ObjectGuid> follow;
        TestValue<bool> canLoot, available;
        TestValue<float> distance;
        std::map<std::string, TestValue<bool>> shouldLoot;
        template<class T> TestValue<T>* GetValue(const std::string& name)
        {
            if constexpr (std::is_same_v<T, LootObject>) return &target;
            else if constexpr (std::is_same_v<T, ObjectGuid>) return &follow;
            else if constexpr (std::is_same_v<T, bool>) return name == "can loot" ? &canLoot : &available;
            else return &distance;
        }
        template<class T> TestValue<T>* GetValue(const std::string&, const std::string& qualifier)
        {
            if constexpr (std::is_same_v<T, bool>) return &shouldLoot[qualifier];
            else return &distance;
        }
    };
}
using namespace ai;
struct PlayerbotAI
{
    AiObjectContext* context = nullptr;
    Player* master = nullptr;
    bool groupLeader = false, activeMaster = true, knowsSkill = false;
    AiObjectContext* GetAiObjectContext() const { return context; }
    Player* GetMaster() const { return master; }
    Unit* GetUnit(ObjectGuid guid) const { return master && master->guid == guid ? master : nullptr; }
    Creature* GetCreature(ObjectGuid guid) const
    {
        auto found = world.find(guid);
        return found == world.end() ? nullptr : dynamic_cast<Creature*>(found->second);
    }
    bool IsGroupLeader() const { return groupLeader; }
    bool HasActivePlayerMaster() const { return activeMaster; }
    bool HasSkill(SkillType) const { return knowsSkill; }
    void TellDebug(Player*, const std::string&, const std::string&) {}
};
struct
{
    float lootDistance = 25, groupMemberLootDistance = 15;
    float groupMemberLootDistanceWithActiveMaster = 10, sightDistance = 75;
} sPlayerbotAIConfig;
struct
{
    bool IsAlive(Player* player) const { return player->alive; }
    unsigned GetDeathState(Creature* creature) const { return creature->deathState; }
    float GetDistance(Unit* unit, WorldObject* object) const { return Distance(unit, object); }
    float GetDistance2d(Unit* unit, WorldObject* object) const
    {
        const float dx = unit->x - object->x, dy = unit->y - object->y;
        return std::sqrt(dx * dx + dy * dy);
    }
} sServerFacade;
namespace ai::dungeon
{
    bool CanLoot(PlayerbotAI*, WorldObject* object) { return object && object->safe; }
}
namespace ai
{
    LootObject::LootObject(Player* bot, ObjectGuid candidate) : skillId(0), reqSkillValue(0), reqItem(0)
    {
        Refresh(bot, candidate);
    }
    LootObject::LootObject(const LootObject& other) = default;
    void LootObject::Refresh(Player* bot, ObjectGuid guid, bool debug)
    {
        skillId = SKILL_NONE;
        reqSkillValue = 0;
        reqItem = 0;
        this->guid = ObjectGuid();
        PlayerbotAI* ai = bot->GetPlayerbotAI();
#include "loot_task_creature_refresh.inc"
        const auto found = world.find(guid);
        this->guid = found != world.end() && found->second->valid ? guid : ObjectGuid();
    }
    WorldObject* LootObject::GetWorldObject(Player*)
    {
        const auto found = world.find(guid);
        return found != world.end() && found->second->valid ? found->second : nullptr;
    }
    bool LootObject::IsLootPossible(Player* bot)
    {
        WorldObject* object = GetWorldObject(bot);
        return object && object->possible && object->safe;
    }
    LootTarget::LootTarget(ObjectGuid candidate) : guid(candidate), asOfTime(time(nullptr)) {}
    LootTarget::LootTarget(const LootTarget& other) = default;
    LootTarget& LootTarget::operator=(const LootTarget& other) = default;
    bool LootTarget::operator<(const LootTarget& other) const { return guid < other.guid; }
    void LootTargetList::shrink(time_t fromTime)
    {
        for (auto it = begin(); it != end();)
            if (it->asOfTime <= fromTime) it = erase(it); else ++it;
    }
}

#include "loot_task_source.inc"

unsigned checks = 0;
static void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
struct Fixture
{
    Player bot, master;
    Group group;
    WorldObject corpse, other;
    AiObjectContext context;
    PlayerbotAI ai;
    LootObjectStack stack{&bot};
    Fixture()
    {
        world.clear(); WorldTimer::now = 0;
        bot.guid = 1; master.guid = 2; corpse.guid = 100; other.guid = 101;
        corpse.x = 8; other.x = 9;
        world[corpse.guid] = &corpse; world[other.guid] = &other;
        bot.group = &group; bot.ai = &ai; ai.context = &context; ai.master = &master;
        context.follow.value = master.guid;
    }
    bool Begin(ObjectGuid guid = 100)
    {
        context.target.Set(LootObject(&bot, guid));
        stack.Add(guid);
        return stack.StartLoot(guid);
    }
};
int main()
{
    for (bool raid : { false, true })
    {
        Fixture f; f.group.raid = raid;
        Check(f.Begin(), "party and raid can start a permitted loot task");
        Check(f.stack.ShouldHoldFollow(), "ordinary close formation following yields to current loot");
        WorldTimer::now = 4000; f.bot.x = 1;
        Check(f.stack.ShouldHoldFollow(), "real approach progress retains the loot task");
        WorldTimer::now = 8000; f.bot.x = -1; f.bot.z = 1;
        Check(f.stack.ShouldHoldFollow(), "stairs and a moving detour count as progress");
        f.stack.CompleteLoot(100);
        Check(!f.stack.ShouldHoldFollow(), "normal following resumes after completion");
        Check(f.context.target.Get().IsEmpty(), "completion clears matching current target");
        Check(f.context.canLoot.resets && f.context.available.resets && f.context.shouldLoot["100"].resets,
            "completion invalidates admission and action caches");
    }
    {
        Fixture f; Check(f.Begin(), "task can start at monotonic time zero");
        WorldTimer::now = 4999; Check(f.stack.ShouldHoldFollow(), "no-progress timeout does not expire early");
        WorldTimer::now = 5000; Check(!f.stack.ShouldHoldFollow(), "five seconds without progress releases following");
        Check(f.stack.IsDeferred(100) && !f.stack.Add(100), "failed task cannot immediately reenter scan queue");
        Check(f.context.target.Get().IsEmpty(), "timeout clears stale target used by flee-to-master too");
        WorldTimer::now = 9999; Check(f.stack.IsDeferred(100), "cooldown remains active just before boundary");
        WorldTimer::now = 10000; Check(!f.stack.IsDeferred(100) && f.stack.Add(100), "cooldown expires exactly at boundary");
        Check(f.Begin(), "cooled target can be tried again");
    }
    {
        Fixture f; Check(f.Begin(), "total-timeout fixture starts");
        for (uint32 step = 1; step <= 7; ++step)
        {
            WorldTimer::now = step * 2000; f.bot.x = float(step);
            Check(f.stack.StartLoot(100), "repeating Start on same object does not restart its timer");
            Check(f.stack.ShouldHoldFollow(), "moving task remains active below hard deadline");
        }
        WorldTimer::now = 15000;
        Check(!f.stack.StartLoot(100) && f.stack.IsDeferred(100), "hard deadline ends even a repeatedly started moving task");
    }
    {
        Fixture f; Check(f.Begin(), "pending fixture starts at time zero");
        f.stack.MarkLootOpened(100);
        f.corpse.possible = false;
        Check(f.stack.IsLootOpening(100) && f.stack.IsLootOpening(), "pending state is valid at zero and can query any object");
        Check(!f.stack.IsLootOpening(101), "pending state belongs to its own corpse");
        Check(f.stack.ShouldHoldFollow(), "opened object becoming in-use does not interrupt response wait");
        Check(f.stack.Add(101) && !f.stack.CanLoot(25) && f.stack.GetLoot(25).IsEmpty(),
            "pending response prevents switching to another corpse");
        WorldTimer::now = 4999; Check(f.stack.ShouldHoldFollow(), "response wait holds until its actual deadline");
        WorldTimer::now = 5000; Check(!f.stack.ShouldHoldFollow() && f.stack.IsDeferred(100), "lost response expires into bounded retry");
        Check(f.stack.GetLoot(25).guid == ObjectGuid(101), "next valid corpse is available after response timeout");
    }
    {
        Fixture f; f.Begin(); f.stack.MarkLootOpened(100); f.bot.casting = true;
        WorldTimer::now = 6000;
        Check(f.stack.IsLootOpening() && f.stack.ShouldHoldFollow(), "active gathering cast may finish after response timeout");
        WorldTimer::now = 14999; Check(f.stack.IsLootOpening(), "gathering retains bounded pending state");
        WorldTimer::now = 15000; Check(!f.stack.IsLootOpening() && f.stack.IsDeferred(100), "gathering cannot hold indefinitely");
    }
    for (unsigned condition = 0; condition != 12; ++condition)
    {
        Fixture f; f.Begin();
        switch (condition)
        {
        case 0: f.bot.alive = false; break;
        case 1: f.bot.combat = true; break;
        case 2: f.bot.teleporting = true; break;
        case 3: f.bot.taxi = true; break;
        case 4: f.corpse.valid = false; break;
        case 5: f.corpse.safe = false; break;
        case 6: f.master.map = 2; break;
        case 7: f.master.instance = 2; break;
        case 8: f.master.transport = &f; break;
        case 9: f.master.taxi = true; break;
        case 10: f.master.teleporting = true; break;
        case 11: f.master.flying = true; break;
        }
        Check(!f.stack.ShouldHoldFollow(), "urgent state or unsafe/invalid world target releases ordinary follow hold");
        Check(f.context.target.Get().IsEmpty(), "invalid task never leaves stale manual target");
    }
    {
        Fixture f; f.Begin(); f.master.x = -8;
        Check(!f.stack.ShouldHoldFollow(), "a master leaving the admitted loot area makes bots follow promptly");
    }
    {
        Fixture f; f.Begin(); f.bot.x = 100;
        Check(!f.stack.ShouldHoldFollow(), "emergency separation takes priority over looting");
    }
    {
        Fixture f; f.Begin(); f.corpse.possible = false;
        Check(!f.stack.ShouldHoldFollow() && f.stack.IsDeferred(100), "lost pickup permission releases task and applies retry");
    }
    {
        Fixture f; f.Begin(); f.context.target.Set(LootObject(&f.bot, 101));
        Check(f.stack.StartLoot(101), "new current object starts independently");
        f.stack.DeferLoot(100);
        Check(f.context.target.Get().guid == ObjectGuid(101) && f.stack.ShouldHoldFollow(),
            "late completion or failure of old object cannot clear new target");
        f.stack.CompleteLoot(100);
        Check(f.context.target.Get().guid == ObjectGuid(101), "late completion does not reset new target");
    }
    {
        Fixture f; f.Begin(); f.stack.DeferLoot(100); f.stack.DeferLoot(101);
        f.stack.Clear();
        Check(!f.stack.IsDeferred(100) && !f.stack.IsDeferred(101) && !f.stack.ShouldHoldFollow(),
            "explicit queue clear releases task and retries");
        Check(f.stack.Add(100), "clear allows an explicit rescan immediately");
    }
    for (bool opened : { false, true })
    {
        Fixture f; f.Begin();
        if (opened) f.stack.MarkLootOpened(100);
        f.context.target.Set(LootObject());
        Check(!f.stack.ShouldHoldFollow() && !f.stack.IsLootOpening(),
            "explicit follow/reset clears both travelling and pending loot tasks");
        Check(!f.stack.IsDeferred(100) && f.stack.GetLoot(25).IsEmpty(),
            "explicit cancellation removes the old queue entry without a failure cooldown");
    }
    {
        Fixture f; f.Begin(); f.stack.MarkLootOpened(100);
        f.context.target.Set(LootObject(&f.bot, 101));
        Check(!f.stack.IsLootOpening(), "pending old task notices an explicitly replaced target");
        Check(f.context.target.Get().guid == ObjectGuid(101) && !f.stack.ShouldHoldFollow(),
            "cancelling old pending task preserves the replacement target");
        Check(f.stack.StartLoot(101) && f.stack.ShouldHoldFollow(), "replacement task can start normally after cancellation");
    }
    {
        Fixture f;
        for (uint32 i = 1; i <= 65; ++i) { WorldTimer::now = i; f.stack.DeferLoot(1000 + i); }
        Check(!f.stack.IsDeferred(1001) && f.stack.IsDeferred(1002) && f.stack.IsDeferred(1065),
            "retry map bounds memory by discarding the oldest of more than 64 entries");
        f.stack.DeferLoot(100, 0); Check(!f.stack.IsDeferred(100), "zero duration does not become a permanent block");
    }
    {
        Fixture f; WorldTimer::now = UINT32_MAX - 2000; f.Begin();
        WorldTimer::now += 3000; f.bot.x = 1;
        Check(f.stack.ShouldHoldFollow(), "movement progress survives monotonic counter wrap");
        f.stack.DeferLoot(100);
        WorldTimer::now += 4999; Check(f.stack.IsDeferred(100), "retry holds correctly across wrap");
        ++WorldTimer::now; Check(!f.stack.IsDeferred(100), "retry expires across wrap");
    }
    {
        Fixture f; f.corpse.x = 5;
        LootObject loot(&f.bot, 100);
        Check(loot.IsWithinLootDistance(&f.bot), "five exact yards permits opening");
        f.corpse.x = 5.04f; Check(!loot.IsWithinLootDistance(&f.bot), "rounded or tolerant five-yard overshoot cannot count as arrived");
        f.corpse.x = 3; f.corpse.z = 5; Check(!loot.IsWithinLootDistance(&f.bot), "native 3D interaction still prevents a cross-floor pickup");
        f.corpse.z = 0; f.corpse.map = 2; Check(!loot.IsWithinLootDistance(&f.bot), "other-map corpse cannot be in reach");
        Check(!loot.IsWithinLootDistance(nullptr), "missing player is not within pickup reach");
    }
    {
        Fixture f; Creature creature; creature.guid = 100; creature.x = 8;
        creature.lootable = true; creature.skinnable = true; world[100] = &creature;
        LootObject normal(&f.bot, 100);
        Check(normal.guid == ObjectGuid(100) && normal.skillId == SKILL_NONE,
            "LOOTABLE plus SKINNABLE corpse first exposes normal loot without requiring skinning");
        creature.lootable = false;
        LootObject unskilled(&f.bot, 100);
        Check(unskilled.IsEmpty(), "skin-only corpse still requires the profession");
        f.ai.knowsSkill = true;
        LootObject skilled(&f.bot, 100);
        Check(skilled.guid == ObjectGuid(100) && skilled.skillId == SKILL_SKINNING,
            "skilled bot can gather after ordinary loot is gone");
        creature.lootable = true; creature.skinnable = false; f.ai.knowsSkill = false;
        LootObject ordinary(&f.bot, 100);
        Check(ordinary.guid == ObjectGuid(100) && ordinary.skillId == SKILL_NONE,
            "ordinary corpse remains available without a gathering profession");
    }
    std::cout << checks << " loot-task runtime checks passed\n";
}
