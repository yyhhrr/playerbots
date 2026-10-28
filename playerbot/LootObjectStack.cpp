#include "LootObjectStack.h"
#include "playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/DungeonMovement.h"
#include "playerbot/strategy/values/SharedValueContext.h"

using namespace ai;

#define MAX_LOOT_OBJECT_COUNT 10

LootTarget::LootTarget(ObjectGuid guid) : guid(guid), asOfTime(time(0))
{
}

LootTarget::LootTarget(LootTarget const& other)
{
    guid = other.guid;
    asOfTime = other.asOfTime;
}

LootTarget& LootTarget::operator=(LootTarget const& other)
{
    if((void*)this == (void*)&other)
        return *this;

    guid = other.guid;
    asOfTime = other.asOfTime;

    return *this;
}

bool LootTarget::operator< (const LootTarget& other) const
{
    return guid < other.guid;
}

void LootTargetList::shrink(time_t fromTime)
{
    for (std::set<LootTarget>::iterator i = begin(); i != end(); )
    {
        if (i->asOfTime <= fromTime)
            erase(i++);
		else
			++i;
    }
}

LootObject::LootObject(Player* bot, ObjectGuid guid)
	: guid(), skillId(SKILL_NONE), reqSkillValue(0), reqItem(0)
{
    Refresh(bot, guid);
}

void LootObject::Refresh(Player* bot, ObjectGuid guid, bool debug)
{
    skillId = SKILL_NONE;
    reqSkillValue = 0;
    reqItem = 0;
    this->guid = ObjectGuid();

    PlayerbotAI* ai = bot->GetPlayerbotAI();
    Creature* creature = ai->GetCreature(guid);
    if (creature && sServerFacade.GetDeathState(creature) == CORPSE)
    {
        if (creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
        {
            if (debug)
                ai->TellDebug(ai->GetMaster(), "Creature flag lootable.", "debug loot");

            this->guid = guid;
        }

        if (creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE) &&
            !creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
        {
            skillId = creature->GetCreatureInfo()->GetRequiredLootSkill();
            uint32 targetLevel = creature->GetLevel();
            reqSkillValue = targetLevel < 10 ? 1 : targetLevel < 20 ? (targetLevel - 10) * 10 : targetLevel * 5;
            if (ai->HasSkill((SkillType)skillId) && bot->GetSkillValue(skillId) >= reqSkillValue)
            {
                if (debug)
                    ai->TellDebug(ai->GetMaster(), "Creature flag skinnable and has skill.", "debug loot");
                this->guid = guid;
            }

            if (debug)
                ai->TellDebug(ai->GetMaster(), "Creature flag skinnable not enough skill.", "debug loot");
            return;
        }

        if (debug)
            ai->TellDebug(ai->GetMaster(), "Creature without loot or skin flag.", "debug loot");

        return;
    }

    GameObject* go = ai->GetGameObject(guid);
    if (go && sServerFacade.isSpawned(go) && !go->IsInUse())
    {
        bool isQuestItemOnly = false;

#ifdef MANGOSBOT_TWO
        for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; i++)
        {
            int itemId = go->GetGOInfo()->questItems[i];

            if (ItemUsageValue::IsNeededForQuest(bot, itemId))
            {
                if (debug)
                    ai->TellDebug(ai->GetMaster(), "GO has item needed for quest.", "debug loot");
                this->guid = guid;
                return;
            }
            isQuestItemOnly |= itemId > 0;
        }
#else
        /*if (!guid.IsEmpty())
        {
            for (auto& entry : GAI_VALUE2(std::list<int32>, "item drop list", -1*uint(go->GetEntry())))
            {
                if (IsNeededForQuest(bot, entry))
                {
                    this->guid = guid;
                    return;
                }
                isQuestItemOnly |= entry > 0;
            }
        }*/
#endif

        if (isQuestItemOnly)
        {
            if (debug)
                ai->TellDebug(ai->GetMaster(), "Go has only quests items we don't need.", "debug loot");
            return;
        }

        uint32 goId = go->GetGOInfo()->id;
        std::set<uint32>& skipGoLootList = ai->GetAiObjectContext()->GetValue<std::set<uint32>&>("skip go loot list")->Get();
        if (skipGoLootList.find(goId) != skipGoLootList.end())
        {
            if (debug)
                ai->TellDebug(ai->GetMaster(), "Go in skip go loot list.", "debug loot");
            return;
        }

        uint32 lockId = go->GetGOInfo()->GetLockId();
        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
        {
            if (debug)
                ai->TellDebug(ai->GetMaster(), "Go has no lockid.", "debug loot");
            return;
        }

        for (int i = 0; i < 8; ++i)
        {
            switch (lockInfo->Type[i])
            {
                case LOCK_KEY_ITEM:
                    if (lockInfo->Index[i] > 0)
                    {
                        if (debug)
                            ai->TellDebug(ai->GetMaster(), "Go has lock with key requirement.", "debug loot");
                        reqItem = lockInfo->Index[i];
                        this->guid = guid;
                    }
                    break;
                case LOCK_KEY_SKILL:
                    if (goId == 13891 || goId == 19535) // Serpentbloom
                    {
                        if (debug)
                            ai->TellDebug(ai->GetMaster(), "Go is serpentbloom.", "debug loot");
                        this->guid = guid;
                    }
                    else if (SkillByLockType(LockType(lockInfo->Index[i])) > 0)
                    {
                        if (debug)
                            ai->TellDebug(ai->GetMaster(), "Go requires skill.", "debug loot");
                        skillId = SkillByLockType(LockType(lockInfo->Index[i]));
                        reqSkillValue = std::max((uint32)1, lockInfo->Skill[i]);
                        this->guid = guid;
                    }
                    break;
                case LOCK_KEY_NONE:
                    if (debug)
                        ai->TellDebug(ai->GetMaster(), "Go has open lock.", "debug loot");
                    this->guid = guid;
                    break;
            }
        }
    }

    if (debug && guid && !this->guid)
        ai->TellDebug(ai->GetMaster(), "Go has bad lock.", "debug loot");
}

WorldObject* LootObject::GetWorldObject(Player* bot)
{
    Refresh(bot, guid);

    PlayerbotAI* ai = bot->GetPlayerbotAI();

    Creature *creature = ai->GetCreature(guid);
    if (creature && sServerFacade.GetDeathState(creature) == CORPSE)
        return creature;

    GameObject* go = ai->GetGameObject(guid);
    if (go && sServerFacade.isSpawned(go))
        return go;

    return NULL;
}

LootObject::LootObject(const LootObject& other)
{
    guid = other.guid;
    skillId = other.skillId;
    reqSkillValue = other.reqSkillValue;
    reqItem = other.reqItem;
}

bool LootObject::IsLootPossible(Player* bot)
{
    WorldObject* object = IsEmpty() ? nullptr : GetWorldObject(bot);
    if (!object)
        return false;

    PlayerbotAI* ai = bot->GetPlayerbotAI();

    // Recheck queued corpses and objects too, before choosing or moving to loot.
    if (!dungeon::CanLoot(ai, object))
        return false;

    if (reqItem && !bot->HasItemCount(reqItem, 1))
    {
        bool canOpen = false;
        // If this is a chest and it was locked before, check if it's unlocked now so we don't need item
        if (guid.IsGameObject())
        {
            GameObject* go = ai->GetGameObject(guid);
            if (go)
            {
                uint32 lootid = go->GetGOInfo()->GetLootId();
                if (lootid && go->GetGOInfo()->type == GAMEOBJECT_TYPE_CHEST)
                {
                    if (!go->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_LOCKED))
                        canOpen = true;
                }
            }
        }
        if (!canOpen)
            return false;

    }
        

    if (guid.IsCreature())
    {
        Creature* creature = ai->GetCreature(guid);
        if (creature && sServerFacade.GetDeathState(creature) == CORPSE)
        {
            if (creature->m_loot && skillId != SKILL_SKINNING)
                if (!creature->m_loot->CanLoot(bot))
                    return false;
        }
    }

    AiObjectContext* context = ai->GetAiObjectContext();

    if (!AI_VALUE2(bool, "should loot object", std::to_string(guid.GetRawValue())))
        return false;

    // Check if the game object has quest loot and bot has the quest for it
    if (guid.IsGameObject())
    {
        GameObject* go = ai->GetGameObject(guid);
        if (go)
        {
            // Ignore for mining nodes and herbs
            if (skillId != SKILL_MINING && skillId != SKILL_HERBALISM)
            {
                if (sObjectMgr.IsGameObjectForQuests(guid.GetEntry()))
                {
                    if (!go->ActivateToQuest(bot))
                    {
                        return false;
                    }
                }
            }
            
            // herb-like quest objects
            if (skillId == SKILL_HERBALISM && reqSkillValue == 1)
            {
                if (sObjectMgr.IsGameObjectForQuests(guid.GetEntry()))
                {
                    if (go->ActivateToQuest(bot))
                    {
                        bool hasQuestItems = false;
                        for (auto& entry : GAI_VALUE2(std::list<uint32>, "entry loot list", -1*int(go->GetEntry())))
                        {
                            if (ItemUsageValue::IsNeededForQuest(bot, entry))
                            {
                                hasQuestItems = true;
                            }
                        }
                        return hasQuestItems || go->GetLootState() != GO_READY;
                    }
                }
            }

            //Ignore objects that are currently in use.
            if (go->IsInUse() || go->GetGoState() == GO_STATE_ACTIVE)
                return false;
        }
    }

    if (skillId == SKILL_NONE)
        return true;

    if (skillId == SKILL_FISHING)
        return false;

    if (!ai->HasSkill((SkillType)skillId))
        return false;

    if (!reqSkillValue)
        return true;

    uint32 skillValue = uint32(bot->GetSkillValue(skillId));
    if (reqSkillValue > skillValue)
        return false;

    if (skillId == SKILL_MINING && !bot->HasItemCount(2901, 1))
        return false;

    if (skillId == SKILL_SKINNING && !bot->HasItemCount(7005, 1))
        return false;

    return true;
}

bool LootObject::IsWithinLootDistance(Player* bot)
{
    WorldObject* object = bot && !IsEmpty() ? GetWorldObject(bot) : nullptr;
    if (!object || !bot->IsWithinDistInMap(object, INTERACTION_DISTANCE))
        return false;

    const float dx = bot->GetPositionX() - object->GetPositionX();
    const float dy = bot->GetPositionY() - object->GetPositionY();
    return dx * dx + dy * dy <= INTERACTION_DISTANCE * INTERACTION_DISTANCE;
}

bool LootObjectStack::Add(ObjectGuid guid)
{
    if (!guid || IsDeferred(guid))
        return false;

    if (!availableLoot.insert(guid).second)
        return false;

    if (availableLoot.size() < MAX_LOOT_OBJECT_COUNT)
        return true;

    std::vector<LootObject> ordered = OrderByDistance();
    for (size_t i = MAX_LOOT_OBJECT_COUNT; i < ordered.size(); i++)
        Remove(ordered[i].guid);

    return true;
}

void LootObjectStack::Remove(ObjectGuid guid)
{
    LootTargetList::iterator i = availableLoot.find(guid);
    if (i != availableLoot.end())
        availableLoot.erase(i);
}

void LootObjectStack::Clear()
{
    availableLoot.clear();
    lootRetries.clear();
    ObjectGuid previous = activeLoot;
    activeLoot = ObjectGuid();
    lootOpening = false;
    if (previous)
        ResetCurrentLoot(previous);
}

bool LootObjectStack::CanLoot(float maxDistance)
{
    std::vector<LootObject> ordered = OrderByDistance(maxDistance);
    return !ordered.empty();
}

LootObject LootObjectStack::GetLoot(float maxDistance)
{
    if (IsLootOpening())
        return LootObject();

    if (activeLoot && !IsDeferred(activeLoot))
    {
        LootObject current(bot, activeLoot);
        WorldObject* object = current.GetWorldObject(bot);
        if (object && current.IsLootPossible(bot) && (!maxDistance || bot->GetDistance(object) <= maxDistance))
            return current;
    }

    std::vector<LootObject> ordered = OrderByDistance(maxDistance);
    return ordered.empty() ? LootObject() : *ordered.begin();
}

std::vector<LootObject> LootObjectStack::OrderByDistance(float maxDistance)
{
    if (IsLootOpening())
        return {};

    availableLoot.shrink(time(0) - 30);

    std::map<float, LootObject> sortedMap;
    LootTargetList safeCopy(availableLoot);
    for (LootTargetList::iterator i = safeCopy.begin(); i != safeCopy.end(); i++)
    {
        ObjectGuid guid = i->guid;
        if (IsDeferred(guid))
            continue;

        LootObject lootObject(bot, guid);
        if (!lootObject.IsLootPossible(bot))
            continue;

        float distance = bot->GetDistance(lootObject.GetWorldObject(bot));
        if (!maxDistance || distance <= maxDistance)
            sortedMap[distance] = lootObject;
    }

    std::vector<LootObject> result;
    for (std::map<float, LootObject>::iterator i = sortedMap.begin(); i != sortedMap.end(); i++)
        result.push_back(i->second);
    return result;
}

void LootObjectStack::ResetCurrentLoot(ObjectGuid guid)
{
    PlayerbotAI* ai = bot ? bot->GetPlayerbotAI() : nullptr;
    AiObjectContext* context = ai ? ai->GetAiObjectContext() : nullptr;
    if (!context || !guid)
        return;

    if (context->GetValue<LootObject>("loot target")->Get().guid == guid)
        context->GetValue<LootObject>("loot target")->Set(LootObject());

    context->GetValue<bool>("can loot")->Reset();
    context->GetValue<bool>("has available loot")->Reset();
    context->GetValue<bool>("should loot object", std::to_string(guid.GetRawValue()))->Reset();
    context->GetValue<float>("distance", "loot target")->Reset();
}

void LootObjectStack::PruneLootRetries(uint32 now)
{
    for (auto it = lootRetries.begin(); it != lootRetries.end();)
    {
        if (uint32(now - it->second.started) >= it->second.duration)
            it = lootRetries.erase(it);
        else
            ++it;
    }
}

bool LootObjectStack::IsDeferred(ObjectGuid guid)
{
    PruneLootRetries(WorldTimer::getMSTime());
    return lootRetries.find(guid) != lootRetries.end();
}

bool LootObjectStack::StartLoot(ObjectGuid guid)
{
    if (!guid || IsDeferred(guid))
        return false;

    const uint32 now = WorldTimer::getMSTime();
    if (activeLoot == guid)
    {
        if (uint32(now - lootStarted) >= 15000)
        {
            DeferLoot(guid);
            return false;
        }
        return true;
    }

    if (activeLoot)
        CompleteLoot(activeLoot);

    activeLoot = guid;
    lootStarted = lootProgress = now;
    lootOpening = false;
    lootProgressX = bot->GetPositionX();
    lootProgressY = bot->GetPositionY();
    lootProgressZ = bot->GetPositionZ();
    LootObject current(bot, guid);
    WorldObject* object = current.GetWorldObject(bot);
    lootBestDistance = object ? sServerFacade.GetDistance(bot, object) : 0.0f;
    return true;
}

void LootObjectStack::CompleteLoot(ObjectGuid guid)
{
    if (!guid)
        return;

    Remove(guid);
    lootRetries.erase(guid);
    if (activeLoot == guid)
    {
        activeLoot = ObjectGuid();
        lootOpening = false;
    }
    ResetCurrentLoot(guid);
}

void LootObjectStack::DeferLoot(ObjectGuid guid, uint32 delayMs)
{
    if (!guid)
        return;

    CompleteLoot(guid);
    const uint32 now = WorldTimer::getMSTime();
    PruneLootRetries(now);
    while (lootRetries.size() >= 64)
    {
        auto oldest = lootRetries.begin();
        for (auto it = lootRetries.begin(); it != lootRetries.end(); ++it)
            if (uint32(now - it->second.started) > uint32(now - oldest->second.started))
                oldest = it;
        lootRetries.erase(oldest);
    }
    lootRetries[guid] = { now, std::min<uint32>(delayMs, 30000) };
}

void LootObjectStack::MarkLootOpened(ObjectGuid guid)
{
    if (!StartLoot(guid))
        return;

    if (!lootOpening)
    {
        lootOpening = true;
        lootOpened = WorldTimer::getMSTime();
    }
}

bool LootObjectStack::IsLootOpening(ObjectGuid guid)
{
    if (!activeLoot || (guid && guid != activeLoot) || !lootOpening)
        return false;

    PlayerbotAI* ai = bot->GetPlayerbotAI();
    AiObjectContext* context = ai ? ai->GetAiObjectContext() : nullptr;
    if (!context || context->GetValue<LootObject>("loot target")->Get().guid != activeLoot)
    {
        CompleteLoot(activeLoot);
        return false;
    }

    const uint32 now = WorldTimer::getMSTime();
    if (uint32(now - lootStarted) >= 15000 ||
        (uint32(now - lootOpened) >= 5000 && !bot->IsNonMeleeSpellCasted(true)))
    {
        DeferLoot(activeLoot);
        return false;
    }
    return true;
}

bool LootObjectStack::ShouldHoldFollow()
{
    if (!activeLoot)
        return false;

    PlayerbotAI* ai = bot->GetPlayerbotAI();
    AiObjectContext* context = ai ? ai->GetAiObjectContext() : nullptr;
    if (!context || context->GetValue<LootObject>("loot target")->Get().guid != activeLoot)
    {
        CompleteLoot(activeLoot);
        return false;
    }

    const uint32 now = WorldTimer::getMSTime();
    LootObject current(bot, activeLoot);
    WorldObject* object = current.GetWorldObject(bot);
    if (!context || !sServerFacade.IsAlive(bot) || bot->IsInCombat() || bot->IsBeingTeleported() ||
        bot->IsTaxiFlying() || !object || object->GetMapId() != bot->GetMapId() ||
        object->GetInstanceId() != bot->GetInstanceId() || !dungeon::CanLoot(ai, object) ||
        uint32(now - lootStarted) >= 15000)
    {
        DeferLoot(activeLoot);
        return false;
    }

    Unit* anchor = ai->GetUnit(context->GetValue<ObjectGuid>("follow target")->Get());
    if (!anchor)
        anchor = ai->GetMaster();
    if (anchor && anchor != bot)
    {
        float allowedRange = sPlayerbotAIConfig.lootDistance;
        if (bot->GetGroup() && !ai->IsGroupLeader())
            allowedRange = ai->HasActivePlayerMaster() ? sPlayerbotAIConfig.groupMemberLootDistanceWithActiveMaster :
                sPlayerbotAIConfig.groupMemberLootDistance;

        if (anchor->GetMapId() != bot->GetMapId() || anchor->GetInstanceId() != bot->GetInstanceId() ||
            anchor->GetTransport() != bot->GetTransport() || anchor->IsTaxiFlying() ||
            anchor->IsFlying() != bot->IsFlying() ||
            (anchor->IsPlayer() && static_cast<Player*>(anchor)->IsBeingTeleported()) ||
            sServerFacade.GetDistance(bot, anchor) > sPlayerbotAIConfig.sightDistance ||
            sServerFacade.GetDistance2d(anchor, object) > allowedRange + INTERACTION_DISTANCE)
        {
            DeferLoot(activeLoot);
            return false;
        }
    }

    if (lootOpening)
        return IsLootOpening();

    if (!current.IsLootPossible(bot))
    {
        DeferLoot(activeLoot);
        return false;
    }

    const float dx = bot->GetPositionX() - lootProgressX;
    const float dy = bot->GetPositionY() - lootProgressY;
    const float dz = bot->GetPositionZ() - lootProgressZ;
    const float distance = sServerFacade.GetDistance(bot, object);
    if (dx * dx + dy * dy + dz * dz >= 0.25f || distance + 0.5f <= lootBestDistance)
    {
        lootProgress = now;
        lootProgressX = bot->GetPositionX();
        lootProgressY = bot->GetPositionY();
        lootProgressZ = bot->GetPositionZ();
        lootBestDistance = std::min(lootBestDistance, distance);
    }
    if (uint32(now - lootProgress) >= 5000)
    {
        DeferLoot(activeLoot);
        return false;
    }
    return true;
}

