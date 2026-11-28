#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

using ObjectGuid = unsigned;
using uint8 = std::uint8_t;

enum CurrentSpellTypes { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL };
enum SpellState { SPELL_STATE_CASTING, SPELL_STATE_FINISHED };
enum class BotState { BOT_STATE_COMBAT };
constexpr int SPELL_AURA_PERIODIC_DAMAGE = 1;

struct WorldLocation { float coord_x = 0, coord_y = 0; };
struct Map
{
    bool dungeon = true;
    bool IsDungeon() const { return dungeon; }
};
struct SpellEntry { bool positive = false; };
bool IsPositiveSpell(const SpellEntry* spell) { return spell->positive; }

struct Spell
{
    struct Targets
    {
        ObjectGuid target;
        ObjectGuid getUnitTargetGuid() const { return target; }
    } m_targets;
    const SpellEntry* m_spellInfo;
    SpellState state = SPELL_STATE_CASTING;
    SpellState getState() const { return state; }
};

struct Unit
{
    explicit Unit(ObjectGuid guid, float x = 0) : guid(guid), x(x) {}
    ObjectGuid GetObjectGuid() const { return guid; }
    uint8 GetHealthPercent() const { return 100; }
    bool IsCreature() const { return creature; }
    bool IsPlayer() const { return !creature; }
    bool HasAuraType(int) const { return dotted; }
    bool HasBreakableByDamageCrowdControlAura() const { return controlled; }
    Unit* GetVictim() const { return victim; }
    Spell* GetCurrentSpell(CurrentSpellTypes slot) const { return casts[slot]; }

    ObjectGuid guid;
    float x;
    bool creature = true, dotted = false, controlled = false, canCastOn = true;
    Unit* victim = nullptr;
    std::array<Spell*, 3> casts{};
};

template <class T>
struct Value
{
    T value{};
    const T& Get() const { return value; }
};

struct AiObjectContext
{
    template <class T> Value<T>* GetValue(const std::string& name)
    {
        if constexpr (std::is_same_v<T, ObjectGuid>)
        {
            if (name == "current target")
                return &current;
            return name == "rti target" ? &focus : &marked;
        }
        else if constexpr (std::is_same_v<T, uint8>)
            return &aoeCount;
        else if constexpr (std::is_same_v<T, WorldLocation>)
            return &aoePosition;
        else
            return &icon;
    }

    Value<ObjectGuid> current, focus, marked;
    Value<uint8> aoeCount{3};
    Value<WorldLocation> aoePosition;
    Value<std::string> icon{"moon"};
};

struct Group
{
    struct MemberSlot { ObjectGuid guid; };
    std::vector<MemberSlot> members;
    const auto& GetMemberSlots() const { return members; }
};

struct PlayerbotAI;
struct Player : Unit
{
    explicit Player(ObjectGuid guid) : Unit(guid) { creature = false; }
    bool IsInWorld() const { return true; }
    Map* GetMap() { return &map; }
    unsigned GetMapId() const { return 1; }
    unsigned GetInstanceId() const { return instance; }
    Group* GetGroup() const { return group; }
    Unit* GetPet() const { return pet; }
    PlayerbotAI* GetPlayerbotAI() const { return ai; }
    ObjectGuid GetSelectionGuid() const { return selection; }

    Map map;
    Group* group = nullptr;
    Unit* pet = nullptr;
    PlayerbotAI* ai = nullptr;
    ObjectGuid selection = 0;
    unsigned instance = 1;
};

struct PlayerbotAI
{
    explicit PlayerbotAI(Player* bot) : bot(bot) {}
    Player* GetBot() const { return bot; }
    AiObjectContext* GetAiObjectContext() { return &context; }
    Unit* GetUnit(ObjectGuid guid) const
    {
        for (Unit* unit : enemies)
            if (unit->guid == guid)
                return unit;
        return nullptr;
    }
    float GetRange(const char*) const { return 26; }
    bool CanCastSpell(const std::string&, Unit* target, bool, void*, bool, bool) const { return target->canCastOn; }
    bool HasStrategy(const char*, BotState) const { return cc; }
    bool IsTank(Player*) const { return true; }

    Player* bot;
    AiObjectContext context;
    std::vector<Unit*> enemies;
    bool cc = true;
};

struct ThreatManager {};
class FindTargetStrategy
{
public:
    explicit FindTargetStrategy(PlayerbotAI* ai) : ai(ai) {}
    virtual void CheckAttacker(Unit*, ThreatManager*) = 0;
    Unit* GetResult() const { return result; }

protected:
    PlayerbotAI* ai;
    Unit* result = nullptr;
};

struct RtiTargetValue
{
    static int GetRtiIndex(const std::string& icon) { return icon == "moon" ? 4 : -1; }
};

const struct
{
    float sightDistance = 60.0f, aoeRadius = 5.0f;
    uint8 mediumHealth = 70;
} sPlayerbotAIConfig;

struct
{
    Player* member = nullptr;
    Player* GetPlayer(ObjectGuid guid) const { return member && member->guid == guid ? member : nullptr; }
} sObjectMgr;

const struct
{
    bool IsAlive(const Player*) const { return true; }
    float GetDistance2d(Unit* first, Unit* second) const { return std::abs(first->x - second->x); }
    float GetDistance2d(Unit* unit, float x, float) const { return std::abs(unit->x - x); }
    bool IsDistanceLessOrEqualThan(float distance, float radius) const { return distance <= radius; }
} sServerFacade;

#define AI_VALUE(Type, name) context->GetValue<Type>(name)->Get()
#include "mage_dungeon_polymorph_selector.inc"
#undef AI_VALUE

void Check(bool condition, const char* description)
{
    if (!condition)
    {
        std::cerr << description << '\n';
        std::exit(EXIT_FAILURE);
    }
}

int main()
{
    Unit focus{1}, untouched{2, 2}, selected{3, 4};
    Player mage{10}, ally{11};
    Group group{{{mage.guid}, {ally.guid}}};
    mage.group = &group;
    PlayerbotAI ai(&mage);
    ai.context.current.value = ai.context.focus.value = focus.guid;
    sObjectMgr.member = &ally;

    const auto choose = [&](std::vector<Unit*> targets, const std::string& spell = "polymorph")
    {
        ai.enemies = targets;
        FindTargetForCcStrategy finder(&ai, spell);
        for (Unit* target : targets)
            finder.CheckAttacker(target, nullptr);
        return finder.GetResult();
    };

    Check(choose({&focus, &untouched}) == &untouched, "an unused moon icon allows a dungeon mage to sheep an unmarked add in the AOE cluster");
    ally.selection = selected.guid;
    Check(choose({&focus, &selected, &untouched}) == &untouched, "an untouched add beats an idle selected add further from the tank");
    Check(choose({&untouched, &selected}) == &untouched, "selection preference is independent of scan order");
    Check(choose({&selected}) == &selected, "an idle selection is not an attack");

    ally.victim = &untouched;
    Check(choose({&focus, &selected, &untouched}) == &selected, "active attacks exclude an add from automatic sheep");
    ally.victim = nullptr;
    Unit pet{20};
    ally.pet = &pet;
    pet.victim = &untouched;
    Check(!choose({&focus, &untouched}), "active pet attacks also exclude an add");
    pet.victim = nullptr;

    const SpellEntry harmful;
    Spell cast{{untouched.guid}, &harmful};
    ally.casts[CURRENT_GENERIC_SPELL] = &cast;
    Check(!choose({&untouched}), "a spell aimed at the add excludes it before impact");
    cast.state = SPELL_STATE_FINISHED;
    Check(choose({&untouched}) == &untouched, "a finished spell does not falsely keep the add busy");
    untouched.dotted = true;
    Check(!choose({&untouched}), "a damage over time effect can immediately break sheep");
    untouched.dotted = false;
    untouched.controlled = true;
    Check(!choose({&untouched}), "leave other breakable crowd control in place");
    untouched.controlled = false;
    untouched.canCastOn = false;
    Check(!choose({&untouched}), "automatic sheep still requires a valid spell target");
    untouched.canCastOn = true;

    ai.cc = false;
    Check(!choose({&untouched}), "disabling the control strategy prevents automatic selection");
    ai.cc = true;
    ai.context.marked.value = selected.guid;
    Check(choose({&untouched, &selected}) == &selected, "an assigned moon keeps manual priority");
    ai.context.marked.value = 0;
    ai.context.icon.value = "none";
    Check(!choose({&focus, &untouched}, "fear"), "other CC retains normal AOE avoidance");
    mage.map.dungeon = false;
    Check(!choose({&focus, &untouched}), "outdoor sheep retains its existing AOE avoidance");
}
