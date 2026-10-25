#include "playerbot/WarsongCarrierAbilities.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

using uint32 = std::uint32_t;
enum ShapeshiftForm { FORM_NONE, FORM_CAT, FORM_TRAVEL, FORM_BEAR, FORM_DIREBEAR, FORM_GHOSTWOLF };
enum Powers { POWER_MANA, MAX_POWERS, POWER_HEALTH };
constexpr unsigned CLASS_DRUID = 11, CLASS_ROGUE = 4, CLASS_MAGE = 8, CLASS_SHAMAN = 7;
constexpr unsigned SPELL_AURA_MOD_ROOT = 1, SPELL_AURA_MOD_DECREASE_SPEED = 2;
constexpr uint32 UNIT_STAT_ROOT = 1, MECHANIC_NOT_REMOVED_BY_SHAPESHIFT = 2, MECHANIC_SNARE = 3;
namespace WorldTimer { uint32 now = 10000; uint32 getMSTime() { return now; } }
uint32 convertEnumToFlag(uint32 mechanic) { return 1u << mechanic; }
struct SpellEntry
{
    uint32 Id = 0, cost = 10, castTime = 0, mechanics = 0, SpellIconID = 0, Dispel = 0;
    unsigned powerType = POWER_MANA;
};
uint32 GetAllSpellMechanicMask(const SpellEntry* entry) { return entry->mechanics; }
struct Aura
{
    SpellEntry entry;
    const SpellEntry* GetSpellProto() const { return &entry; }
};
struct Unit
{
    float x = 0, y = 0, z = 0;
    bool alive = true, los = true;
    Unit* victim = nullptr;
    bool IsAlive() const { return alive; }
    Unit* GetVictim() const { return victim; }
};
struct Terrain
{
    bool outdoors = true;
    bool IsOutdoors(float, float, float) const { return outdoors; }
};
struct Player : Unit
{
    unsigned playerClass = CLASS_DRUID, level = 60;
    bool carrier = true, casting = false, rooted = false, mounted = false, falling = false, water = false;
    uint32 health = 1000, maxHealth = 1000, mana = 1000;
    ShapeshiftForm form = FORM_NONE;
    Terrain terrain;
    std::set<uint32> known, notReady;
    std::vector<Unit*> attackers;
    std::map<unsigned, std::vector<Aura*>> auraTypes;
    unsigned getClass() const { return playerClass; }
    unsigned GetLevel() const { return level; }
    bool IsMounted() const { return mounted; }
    bool HasSpell(uint32 id) const { return known.count(id) != 0; }
    bool IsSpellReady(uint32 id) const { return !notReady.count(id); }
    uint32 GetHealth() const { return health; }
    uint32 GetMaxHealth() const { return maxHealth; }
    uint32 GetPower(Powers) const { return mana; }
    uint32 GetMapId() const { return 489; }
    bool IsNonMeleeSpellCasted(bool) const { return casting; }
    bool hasUnitState(uint32) const { return rooted; }
    bool HasAuraType(unsigned type) const { return type == SPELL_AURA_MOD_ROOT && rooted; }
    const std::vector<Aura*>& GetAurasByType(unsigned type) { return auraTypes[type]; }
    const std::vector<Unit*>& getAttackers() const { return attackers; }
    float GetDistance(const Unit* target) const { return std::hypot(x - target->x, y - target->y); }
    float GetDistance2d(float gx, float gy) const { return std::hypot(x - gx, y - gy); }
    float GetAngle(float gx, float gy) const { return std::atan2(gy - y, gx - x); }
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    float GetPositionZ() const { return z; }
    float GetCollisionHeight() const { return 2; }
    void UpdateAllowedPositionZ(float, float, float&) const {}
    bool IsWithinLOS(float, float, float, bool = false) const { return los; }
    bool IsFalling() const { return falling; }
    bool IsInWater() const { return water; }
    Terrain* GetTerrain() { return &terrain; }
    ShapeshiftForm GetShapeshiftForm() const { return form; }
};
uint32 GetSpellCastTime(const SpellEntry* entry, Player*) { return entry->castTime; }
struct Spell { static uint32 CalculatePowerCost(const SpellEntry* entry, Player*) { return entry->cost; } };
struct PlayerbotAI;
namespace ai
{
    struct PositionEntry { float x = 0, y = 0, z = 0; uint32 mapId = 489; bool valueSet = false; };
    using PositionMap = std::map<std::string, PositionEntry>;
}
template<class T> struct Value { T data{}; T& Get() { return data; } };
struct Context
{
    Value<ai::PositionMap> positions;
    std::map<std::string, Value<uint32>> spells;
    template<class T> auto GetValue(const char*, const std::string& qualifier = "")
    {
        if constexpr (std::is_same_v<T, ai::PositionMap&>) return &positions;
        else return &spells[qualifier];
    }
};
std::map<uint32, SpellEntry> spellEntries;
struct ServerFacade
{
    const SpellEntry* LookupSpellInfo(uint32 id) const
    {
        const auto found = spellEntries.find(id);
        return found == spellEntries.end() ? nullptr : &found->second;
    }
    bool IsWithinLOSInMap(Player*, Unit* target) const { return target->los; }
    void SetFacingTo(Player*, float, bool) {}
} sServerFacade;
struct Config { uint32 reactDelay = 100; } sPlayerbotAIConfig;
struct PlayerbotAI
{
    Player* bot;
    Context* context;
    std::map<std::string, bool> auras;
    std::map<uint32, ShapeshiftForm> spellForms;
    bool buffUseful = true, canCast = true, castResult = true, applyForm = true;
    bool jumping = false;
    unsigned stops = 0, casts = 0, buffChecks = 0;
    uint32 lastSpell = 0;
    Player* GetBot() const { return bot; }
    Context* GetAiObjectContext() const { return context; }
    bool HasAura(const char* name, Player*) const
    {
        const auto found = auras.find(name);
        return found != auras.end() && found->second;
    }
    bool CanCastSpell(uint32, Player*, unsigned) const { return canCast; }
    bool CastSpell(uint32 id, Player*, void*, bool, uint32* duration)
    {
        ++casts; lastSpell = id; *duration = 1500;
        if (castResult && applyForm && spellForms.count(id)) bot->form = spellForms[id];
        return castResult;
    }
    bool IsJumping() const { return jumping; }
    void StopMoving() { ++stops; }
};
namespace ai
{
    struct Event {};
    struct Action
    {
        PlayerbotAI* ai;
        Player* bot;
        uint32 duration = 0;
        Action(PlayerbotAI* ai, const char*) : ai(ai), bot(ai->GetBot()) {}
        void SetDuration(uint32 next) { duration = next; }
    };
    class WarsongCarrierMobilityAction : public Action
    {
    public:
        explicit WarsongCarrierMobilityAction(PlayerbotAI* ai) : Action(ai, "bg warsong mobility") {}
        bool isUseful();
        bool Execute(Event&);
        uint32 lastFormChange = 0;
        bool changedForm = false;
    };
    struct CastBuffSpellAction : Action
    {
        using Action::Action;
        virtual bool isUseful() { ++ai->buffChecks; return ai->buffUseful; }
    };
}
namespace ai::warsong
{
    bool IsCarrier(Player* player) { return player->carrier; }
    namespace
    {
#include "warsong_carrier_helpers.inc"
}
namespace ai
{
#include "druid_travel_action.inc"
}
using namespace ai;
#include "warsong_carrier_execution.inc"

namespace
{
    unsigned checks = 0, failures = 0;
    void Check(bool result, const char* text)
    {
        ++checks;
        if (!result) { ++failures; std::cerr << "FAILED: " << text << '\n'; }
    }
    struct Fixture
    {
        Player bot;
        Context context;
        PlayerbotAI ai{&bot, &context};
        WarsongCarrierMobilityAction mobility{&ai};
        CastTravelFormAction travel{&ai};
        Event event;
        Fixture()
        {
            WorldTimer::now = 10000;
            spellEntries.clear();
            context.positions.data["bg objective"] = {100, 0, 0, 489, true};
            Add("travel form", 783, FORM_TRAVEL);
            Add("cat form", 768, FORM_CAT);
            Add("bear form", 5487, FORM_BEAR);
        }
        void Add(const std::string& name, uint32 id, ShapeshiftForm form = FORM_NONE)
        {
            context.spells[name].data = id;
            bot.known.insert(id);
            spellEntries[id] = {id};
            if (form != FORM_NONE) ai.spellForms[id] = form;
        }
    };
}

int main()
{
    {
        Fixture f; f.bot.form = FORM_TRAVEL; f.ai.buffUseful = false;
        Check(!f.travel.isUseful(), "the ordinary high-priority travel action cannot cancel an existing carrier form");
        Check(!f.mobility.isUseful() && !f.mobility.Execute(f.event) && f.ai.casts == 0,
            "native travel form is recognized even without a matching aura-name cache entry");
    }
    for (ShapeshiftForm form : {FORM_NONE, FORM_CAT, FORM_BEAR, FORM_DIREBEAR, FORM_TRAVEL})
    {
        Fixture f; f.bot.form = form;
        Check(!f.travel.isUseful(), "a WSG carrier leaves form selection to the dedicated mobility action");
    }
    {
        Fixture f; f.bot.carrier = false; f.ai.buffUseful = false;
        Check(!f.travel.isUseful(), "non-carrier travel respects the inherited buff usefulness check");
        f.ai.buffUseful = true;
        Check(f.travel.isUseful(), "an ordinary unmounted non-carrier can still choose travel form");
        f.bot.mounted = true;
        Check(!f.travel.isUseful(), "a mounted level-60 non-carrier preserves its existing mount");
        f.bot.level = 39;
        Check(f.travel.isUseful(), "the existing classic first-mount level boundary remains intact");
    }
    {
        Fixture f;
        Check(f.mobility.isUseful() && f.mobility.Execute(f.event) && f.bot.form == FORM_TRAVEL,
            "an outdoor carrier performs one effective instant travel-form transition");
        Check(f.mobility.changedForm && f.mobility.lastFormChange == WorldTimer::now && f.mobility.duration == 1500,
            "a confirmed form change records its actual throttle and cast duration");
        f.ai.auras["travel form"] = false;
        Check(!f.mobility.isUseful() && !f.mobility.Execute(f.event) && f.ai.casts == 1,
            "the completed shift yields subsequent decisions to the delivery movement");
        WorldTimer::now += 5000;
        Check(!f.mobility.isUseful() && !f.travel.isUseful() && f.ai.casts == 1,
            "the carrier continues yielding after the form throttle expires instead of repeatedly reshifting");
    }
    {
        Fixture f; f.ai.applyForm = false;
        Check(f.mobility.isUseful() && !f.mobility.Execute(f.event) && f.ai.casts == 1,
            "a reported cast success without an actual instant form change does not claim the update");
        Check(!f.mobility.changedForm && !f.mobility.lastFormChange && !f.mobility.duration,
            "an ineffective shift cannot consume the four-second throttle or reserve the next action delay");
        f.ai.applyForm = true;
        Check(f.mobility.Execute(f.event) && f.bot.form == FORM_TRAVEL,
            "a later genuinely effective shift remains immediately eligible");
    }
    {
        Fixture f; f.ai.castResult = false;
        Check(!f.mobility.Execute(f.event) && !f.mobility.changedForm && !f.mobility.duration,
            "a failed cast also yields without falsely recording form progress");
    }
    {
        Fixture f; f.bot.casting = true;
        Check(!f.mobility.isUseful(), "an ongoing meaningful cast is not interrupted by routine mobility");
        f.bot.casting = false; f.ai.canCast = false;
        Check(!f.mobility.isUseful(), "uncastable forms do not claim a carrier action tick");
    }
    {
        Fixture f; f.bot.terrain.outdoors = false;
        Check(!f.mobility.isUseful(), "the tunnel does not permit an outdoor-only travel shift");
        f.bot.terrain.outdoors = true;
        Check(f.mobility.Execute(f.event) && !f.mobility.isUseful(), "leaving the tunnel shifts once and then yields to the route");
    }
    {
        Fixture f; f.bot.form = FORM_CAT; f.ai.auras["cat form"] = false;
        f.Add("dash", 1850);
        Check(f.mobility.isUseful() && f.mobility.Execute(f.event) && f.ai.lastSpell == 1850,
            "native cat form prepares the real dash without cycling through another cat-form cast");
        f.ai.auras["dash"] = true; f.bot.notReady.insert(1850);
        Check(!f.mobility.isUseful(), "an active dash is not lost to an unnecessary travel transition");
    }
    {
        Fixture f; f.bot.form = FORM_BEAR; f.ai.auras["bear form"] = false;
        Unit enemy; enemy.victim = &f.bot; enemy.x = 5; f.bot.attackers = {&enemy}; f.bot.health = 600;
        Check(!f.mobility.isUseful(), "native defensive bear form is held under pressure without repeating the armor shift");
    }
    {
        Fixture f; f.bot.form = FORM_TRAVEL; f.bot.rooted = true;
        Aura root; root.entry.mechanics = 1; f.bot.auraTypes[SPELL_AURA_MOD_ROOT] = {&root};
        Check(f.mobility.Execute(f.event) && f.bot.form == FORM_CAT, "a rooted carrier can still change directly into a useful different form");
        Check(!f.mobility.isUseful(), "the same root cannot cause another form change inside the actual four-second throttle");
        WorldTimer::now += 4000;
        Check(f.mobility.isUseful(), "a continuing removable root is retried only after the legal form interval");
    }
    {
        Fixture f; f.bot.carrier = false;
        Check(!f.mobility.isUseful() && !f.mobility.Execute(f.event), "dropping the flag immediately disables dedicated carrier mobility");
    }
    std::cout << checks << " Warsong carrier runtime checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
