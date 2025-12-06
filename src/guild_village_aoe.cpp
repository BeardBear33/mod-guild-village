// modules/mod-guild-village/src/guild_village_aoe.cpp

#include "ScriptMgr.h"
#include "LootMgr.h"
#include "ServerScript.h"
#include "WorldSession.h"
#include "WorldPacket.h"
#include "Player.h"
#include "Creature.h"
#include "Config.h"
#include "Map.h"
#include "Corpse.h"
#include "Group.h"
#include "ObjectMgr.h"
#include "GameObject.h"
#include "ObjectAccessor.h"
#include "Chat.h"
#include "DatabaseEnv.h"

#include <mutex>
#include <unordered_map>
#include <list>
#include <vector>
#include <optional>
#include <string>
#include <algorithm>
#include <cctype>

// ======================================================================
//  Guild Village – AoE loot backend
//  - per-session toggle (v paměti)
//  - auto AoE loot při CMSG_LOOT
//  - veřejné API pro .village aoeloot / .v aoeloot
// ======================================================================

namespace GuildVillageAoe
{
    namespace
    {
        std::unordered_map<uint64, bool> s_playerAoeLootState;
        std::mutex s_playerAoeLootStateMutex;

        uint64 GetPlayerGuidRaw(Player* player)
        {
            return player ? player->GetGUID().GetRawValue() : 0;
        }

        bool IsPlayerAoeLootActiveInternal(Player* player)
        {
            uint64 guid = GetPlayerGuidRaw(player);
            if (!guid)
                return false;

            std::lock_guard<std::mutex> lock(s_playerAoeLootStateMutex);
            auto it = s_playerAoeLootState.find(guid);
            return it != s_playerAoeLootState.end() && it->second;
        }

        bool TogglePlayerAoeLootInternal(Player* player)
        {
            uint64 guid = GetPlayerGuidRaw(player);
            if (!guid)
                return false;

            std::lock_guard<std::mutex> lock(s_playerAoeLootStateMutex);
            auto it = s_playerAoeLootState.find(guid);
            bool current = (it != s_playerAoeLootState.end() && it->second);
            bool newState = !current;

            if (newState)
            {
                s_playerAoeLootState[guid] = true;
            }
            else
            {
                if (it != s_playerAoeLootState.end())
                    s_playerAoeLootState.erase(it);
            }

            return newState;
        }

        void ClearPlayerAoeLootInternal(Player* player)
        {
            uint64 guid = GetPlayerGuidRaw(player);
            if (!guid)
                return;

            std::lock_guard<std::mutex> lock(s_playerAoeLootStateMutex);
            s_playerAoeLootState.erase(guid);
        }

        enum class AoeErrorReason : uint8
        {
            None = 0,
            NoGuild,
            NoVillage,
            ConfigDisabled
        };

        bool IsEnglishLocale()
        {
            std::string loc = sConfigMgr->GetOption<std::string>("GuildVillage.Locale", "cs");
            std::transform(loc.begin(), loc.end(), loc.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return (loc == "en" || loc == "english");
        }

        void SendAoeError(Player* player, AoeErrorReason reason)
        {
            if (!player)
                return;

            WorldSession* session = player->GetSession();
            if (!session)
                return;

            ChatHandler handler(session);
            bool en = IsEnglishLocale();

            switch (reason)
            {
                case AoeErrorReason::NoGuild:
                    if (en)
                        handler.SendSysMessage("To use AoE loot, you must be in a guild that owns a guild village.");
                    else
                        handler.SendSysMessage("Pro AoE loot musíš mít guildu, která vlastní guildovní vesnici.");
                    break;
                case AoeErrorReason::NoVillage:
                    if (en)
                        handler.SendSysMessage("Your guild must own a guild village to enable AoE loot.");
                    else
                        handler.SendSysMessage("Tvá guilda musí vlastnit guildovní vesnici pro aktivaci AoE lootu.");
                    break;
                case AoeErrorReason::ConfigDisabled:
                    if (en)
                        handler.SendSysMessage("AoE loot is not enabled on this server.");
                    else
                        handler.SendSysMessage("Na tomto serveru není povolen AoE loot.");
                    break;
                default:
                    break;
            }
        }

        bool PlayerHasGuildVillage(Player* player)
        {
            if (!player)
                return false;

            uint32 guildId = player->GetGuildId();
            if (!guildId)
                return false;

            if (QueryResult res = WorldDatabase.Query(
                    "SELECT 1 FROM customs.gv_guild WHERE guild={} LIMIT 1", guildId))
                return true;

            return false;
        }
    }

    bool IsAoeLootEnabledForPlayer(Player* player)
    {
        if (!player)
            return false;

        uint32 mode = sConfigMgr->GetOption<uint32>("GuildVillage.Aoe.Loot", 2);

        switch (mode)
        {
            case 0:
                return false;
            case 1:
                return !player->GetGroup();
            case 2:
            default:
                return true;
        }
    }

    bool IsSessionActive(Player* player)
    {
        return IsPlayerAoeLootActiveInternal(player);
    }

    void ClearSession(Player* player)
    {
        ClearPlayerAoeLootInternal(player);
    }

    std::optional<bool> ToggleAoeLootForPlayer(Player* player)
    {
        if (!player)
            return std::nullopt;

        uint32 mode = sConfigMgr->GetOption<uint32>("GuildVillage.Aoe.Loot", 2);
        if (mode == 0)
        {
            SendAoeError(player, AoeErrorReason::ConfigDisabled);
            return std::nullopt;
        }

        if (!player->GetGuildId())
        {
            SendAoeError(player, AoeErrorReason::NoGuild);
            return std::nullopt;
        }

        if (!PlayerHasGuildVillage(player))
        {
            SendAoeError(player, AoeErrorReason::NoVillage);
            return std::nullopt;
        }

        if (!IsAoeLootEnabledForPlayer(player))
            return std::nullopt;

        bool newState = TogglePlayerAoeLootInternal(player);
        return std::optional<bool>(newState);
    }

    bool ValidateLootingDistance(Player* player, ObjectGuid lguid, float maxDistance)
    {
        if (!player)
            return false;

        if (maxDistance <= 0.0f)
            maxDistance = sConfigMgr->GetOption<float>("GuildVillage.Aoe.Loot.Range", 55.0f);

        if (lguid.IsGameObject())
        {
            GameObject* go = player->GetMap()->GetGameObject(lguid);
            if (!go)
                return false;

            if (go->GetOwnerGUID() == player->GetGUID() || go->GetGoType() == GAMEOBJECT_TYPE_FISHINGHOLE)
                return true;

            return go->IsWithinDistInMap(player, maxDistance);
        }
        else if (lguid.IsItem())
        {
            ::Item* pItem = player->GetItemByGuid(lguid);
            return (pItem != nullptr);
        }
        else if (lguid.IsCorpse())
        {
            Corpse* corpse = ObjectAccessor::GetCorpse(*player, lguid);
            if (!corpse)
                return false;

            return corpse->IsWithinDistInMap(player, maxDistance);
        }
        else
        {
            Creature* creature = player->GetMap()->GetCreature(lguid);
            if (!creature)
                return false;

            if (creature->IsAlive() &&
                player->IsClass(CLASS_ROGUE, CLASS_CONTEXT_ABILITY) &&
                creature->loot.loot_type == LOOT_PICKPOCKETING)
            {
                return creature->IsWithinDistInMap(player, INTERACTION_DISTANCE);
            }

            return creature->IsWithinDistInMap(player, maxDistance);
        }
    }

    bool ProcessCreatureGold(Player* player, Creature* creature)
    {
        if (!player || !creature)
            return false;

        if (!ValidateLootingDistance(player, creature->GetGUID(), 0.0f))
        {
            player->SendLootError(creature->GetGUID(), LOOT_ERROR_TOO_FAR);
            return false;
        }

        Loot* loot = &creature->loot;
        if (!loot || loot->gold == 0)
            return false;

        bool shareMoney = true;

        if (shareMoney && player->GetGroup())
        {
            Group* group = player->GetGroup();
            std::vector<Player*> playersNear;

            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* member = itr->GetSource();
                if (member)
                    playersNear.push_back(member);
            }

            if (playersNear.empty())
                return false;

            uint32 goldPerPlayer = uint32(loot->gold / playersNear.size());

            for (Player* groupMember : playersNear)
            {
                groupMember->ModifyMoney(goldPerPlayer);
                groupMember->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY, goldPerPlayer);

                WorldPacket data(SMSG_LOOT_MONEY_NOTIFY, 4 + 1);
                data << uint32(goldPerPlayer);
                data << uint8(playersNear.size() > 1 ? 0 : 1);
                groupMember->GetSession()->SendPacket(&data);
            }
        }
        else
        {
            player->ModifyMoney(loot->gold);
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_LOOT_MONEY, loot->gold);

            WorldPacket data(SMSG_LOOT_MONEY_NOTIFY, 4 + 1);
            data << uint32(loot->gold);
            data << uint8(1);
            player->GetSession()->SendPacket(&data);
        }

        loot->gold = 0;
        loot->NotifyMoneyRemoved();
        return true;
    }

    void ReleaseAndCleanupLoot(ObjectGuid lguid, Player* player, Loot* /*lootPtr*/)
    {
        if (!player)
            return;

        player->SetLootGUID(ObjectGuid::Empty);
        player->SendLootRelease(lguid);
        player->RemoveUnitFlag(UNIT_FLAG_LOOTING);

        if (!player->IsInWorld())
            return;

        Loot* loot = nullptr;

        if (lguid.IsGameObject())
        {
            GameObject* go = player->GetMap()->GetGameObject(lguid);
            if (!go)
            {
                player->SendLootRelease(lguid);
                return;
            }

            if (!ValidateLootingDistance(player, lguid, 0.0f))
            {
                player->SendLootRelease(lguid);
                return;
            }

            loot = &go->loot;
        }
        else if (lguid.IsCorpse())
        {
            Corpse* corpse = ObjectAccessor::GetCorpse(*player, lguid);
            if (!corpse)
                return;

            if (!ValidateLootingDistance(player, lguid, 0.0f))
                return;

            loot = &corpse->loot;
        }
        else if (lguid.IsItem())
        {
            ::Item* pItem = player->GetItemByGuid(lguid);
            if (!pItem)
                return;

            return;
        }
        else
        {
            Creature* creature = player->GetMap()->GetCreature(lguid);
            if (!creature)
                return;

            if (!ValidateLootingDistance(player, lguid, 0.0f))
            {
                player->SendLootError(lguid, LOOT_ERROR_TOO_FAR);
                return;
            }

            loot = &creature->loot;
            if (!loot)
                return;

            if (loot->isLooted())
            {
                if (!creature->IsAlive())
                    creature->AllLootRemovedFromCorpse();

                creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
                loot->clear();
            }
            else
            {
                if (player->GetGUID() == loot->roundRobinPlayer)
                {
                    loot->roundRobinPlayer.Clear();

                    if (Group* group = player->GetGroup())
                        group->SendLooter(creature, nullptr);
                }

                creature->ForceValuesUpdateAtIndex(UNIT_DYNAMIC_FLAGS);
            }
        }

        if (!lguid.IsItem() && loot)
        {
            loot->RemoveLooter(player->GetGUID());
        }
    }

    bool ProcessSingleLootSlot(Player* player, ObjectGuid lguid, uint8 lootSlot)
    {
        if (!player)
            return false;

        if (!ValidateLootingDistance(player, lguid, 0.0f))
        {
            player->SendLootError(lguid, LOOT_ERROR_TOO_FAR);
            return false;
        }

        Loot* loot = nullptr;

        if (lguid.IsGameObject())
        {
            GameObject* go = player->GetMap()->GetGameObject(lguid);
            if (!go)
            {
                player->SendLootRelease(lguid);
                return false;
            }
            loot = &go->loot;
        }
        else if (lguid.IsItem())
        {
            ::Item* pItem = player->GetItemByGuid(lguid);
            if (!pItem)
            {
                player->SendLootRelease(lguid);
                return false;
            }
            loot = &pItem->loot;
        }
        else if (lguid.IsCorpse())
        {
            Corpse* bones = ObjectAccessor::GetCorpse(*player, lguid);
            if (!bones)
            {
                player->SendLootRelease(lguid);
                return false;
            }
            loot = &bones->loot;
        }
        else
        {
            Creature* creature = player->GetMap()->GetCreature(lguid);
            if (!creature)
            {
                player->SendLootRelease(lguid);
                return false;
            }
            loot = &creature->loot;
        }

        if (!loot)
            return false;

        if (lootSlot >= loot->items.size())
        {
            uint8 questItemOffset = loot->items.size();

            const QuestItemMap& questItems = loot->GetPlayerQuestItems();
            auto q_itr = questItems.find(player->GetGUID());
            if (q_itr != questItems.end())
            {
                const QuestItemList* qlist = q_itr->second;
                uint8 questCount = qlist->size();
                if (lootSlot < questItemOffset + questCount)
                {
                    uint8 questIndex = lootSlot - questItemOffset;
                    const QuestItem& qitem = (*qlist)[questIndex];
                    if (!qitem.is_looted && qitem.index < loot->quest_items.size())
                    {
                        LootItem& li = loot->quest_items[qitem.index];
                        player->AddItem(li.itemid, li.count);
                        const_cast<QuestItem&>(qitem).is_looted = true;
                        return true;
                    }
                }
            }

            const QuestItemMap& ffaItems = loot->GetPlayerFFAItems();
            auto ffa_itr = ffaItems.find(player->GetGUID());
            if (ffa_itr != ffaItems.end())
            {
                const QuestItemList* flist = ffa_itr->second;
                uint8 ffaOffset = questItemOffset + (q_itr != questItems.end() ? q_itr->second->size() : 0);
                uint8 ffaCount = flist->size();
                if (lootSlot < ffaOffset + ffaCount)
                {
                    uint8 ffaIndex = lootSlot - ffaOffset;
                    const QuestItem& fitem = (*flist)[ffaIndex];
                    if (!fitem.is_looted && fitem.index < loot->quest_items.size())
                    {
                        LootItem& li = loot->quest_items[fitem.index];
                        player->AddItem(li.itemid, li.count);
                        const_cast<QuestItem&>(fitem).is_looted = true;
                        return true;
                    }
                }
            }

            return false;
        }

        Group* group = player->GetGroup();
        LootItem* lootItem = nullptr;
        InventoryResult msg = EQUIP_ERR_OK;
        bool isGroupLoot = false;
        bool isFFA = false;
        bool isMasterLooter = false;
        bool isRoundRobin = false;
        bool isThreshold = false;
        LootMethod lootMethod = GROUP_LOOT;
        uint8 groupLootThreshold = 2;

        isFFA = loot->items[lootSlot].freeforall;

        if (group)
        {
            lootMethod = group->GetLootMethod();
            groupLootThreshold = group->GetLootThreshold();
            isMasterLooter = (lootMethod == MASTER_LOOT);
            isRoundRobin   = (lootMethod == ROUND_ROBIN);
            isGroupLoot    = (lootMethod == GROUP_LOOT || lootMethod == NEED_BEFORE_GREED);

            ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(loot->items[lootSlot].itemid);
            isThreshold = (itemTemplate && itemTemplate->Quality >= groupLootThreshold);
        }

        if (group && isGroupLoot && isThreshold && !isFFA && !isMasterLooter)
        {
            if (!loot->items[lootSlot].is_blocked)
            {
                Roll roll(lguid, loot->items[lootSlot]);
                roll.itemSlot = lootSlot;
                roll.setLoot(loot);

                for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
                {
                    Player* member = itr->GetSource();
                    if (member && member->IsInWorld() && !member->isDead())
                    {
                        roll.playerVote[member->GetGUID()] = RollVote(0);
                        roll.totalPlayersRolling++;
                    }
                }

                group->SendLootStartRoll(60, player->GetMapId(), roll);
            }
            return true;
        }
        else if (group && isMasterLooter && !isFFA)
        {
            if (group->GetMasterLooterGuid() != player->GetGUID())
            {
                player->SendLootError(lguid, LOOT_ERROR_MASTER_OTHER);
                return false;
            }
        }
        else if (group && isRoundRobin && loot->roundRobinPlayer && loot->roundRobinPlayer != player->GetGUID())
        {
            return false;
        }

        sScriptMgr->OnPlayerAfterCreatureLoot(player);

        lootItem = player->StoreLootItem(lootSlot, loot, msg);

        if (msg != EQUIP_ERR_OK && lguid.IsItem() && loot->loot_type != LOOT_CORPSE && lootItem)
        {
            lootItem->is_looted = true;
            loot->NotifyItemRemoved(lootItem->itemIndex);
            loot->unlootedCount--;
            player->SendItemRetrievalMail(lootItem->itemid, lootItem->count);
        }

        if (loot->isLooted() && lguid.IsItem())
        {
            ReleaseAndCleanupLoot(lguid, player, loot);
        }

        return true;
    }

    bool TriggerAoeLoot(Player* player)
    {
        if (!player)
            return true;

        if (!IsAoeLootEnabledForPlayer(player))
            return true;

        float range = sConfigMgr->GetOption<float>("GuildVillage.Aoe.Loot.Range", 55.0f);

        std::list<Creature*> nearbyCorpses;
        player->GetDeadCreatureListInGrid(nearbyCorpses, range);

        std::list<Creature*> validCorpses;

        for (Creature* creature : nearbyCorpses)
        {
            if (!player || !creature)
                continue;

            if (!player->isAllowedToLoot(creature))
                continue;

            if (!creature->HasDynamicFlag(UNIT_DYNFLAG_LOOTABLE))
                continue;

            if (!creature->hasLootRecipient())
                continue;

            if (!creature->isTappedBy(player))
                continue;

            Group* group = player->GetGroup();
            if (group)
            {
                Loot* loot = &creature->loot;
                LootMethod lootMethod = group->GetLootMethod();

                if (lootMethod == ROUND_ROBIN)
                {
                    if (loot->roundRobinPlayer && loot->roundRobinPlayer != player->GetGUID())
                        continue;
                }
                else if (lootMethod == MASTER_LOOT)
                {
                    if (group->GetMasterLooterGuid() != player->GetGUID())
                        continue;
                }
            }

            validCorpses.push_back(creature);
        }

        for (Creature* creature : validCorpses)
        {
            ObjectGuid lguid = creature->GetGUID();
            Loot* loot = &creature->loot;

            if (!loot)
                continue;

            if (!ValidateLootingDistance(player, lguid, range))
                continue;

            player->SetLootGUID(lguid);

            for (uint8 lootSlot = 0; lootSlot < loot->items.size(); ++lootSlot)
            {
                ProcessSingleLootSlot(player, lguid, lootSlot);
            }

            const QuestItemMap& questItems = loot->GetPlayerQuestItems();
            auto q_itr = questItems.find(player->GetGUID());
            if (q_itr != questItems.end())
            {
                const QuestItemList* qlist = q_itr->second;
                for (uint8 i = 0; i < qlist->size(); ++i)
                {
                    uint8 questSlot = loot->items.size() + i;
                    ProcessSingleLootSlot(player, lguid, questSlot);
                }
            }

            const QuestItemMap& ffaItems = loot->GetPlayerFFAItems();
            auto ffa_itr = ffaItems.find(player->GetGUID());
            if (ffa_itr != ffaItems.end())
            {
                const QuestItemList* flist = ffa_itr->second;
                uint8 base = loot->items.size() + (q_itr != questItems.end() ? q_itr->second->size() : 0);
                for (uint8 i = 0; i < flist->size(); ++i)
                {
                    uint8 ffaSlot = base + i;
                    ProcessSingleLootSlot(player, lguid, ffaSlot);
                }
            }

            if (loot->gold > 0)
                ProcessCreatureGold(player, creature);

            if (loot->isLooted())
                ReleaseAndCleanupLoot(lguid, player, loot);
        }

        return true;
    }

    class guild_village_AoeLootQuestParty : public PlayerScript
    {
    public:
        guild_village_AoeLootQuestParty() : PlayerScript("guild_village_AoeLootQuestParty") { }

        void OnPlayerBeforeFillQuestLootItem(Player* /*player*/, LootItem& item) override
        {
            ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(item.itemid);
            if (itemTemplate &&
                itemTemplate->Quality  == ITEM_QUALITY_NORMAL &&
                itemTemplate->Class    == ITEM_CLASS_QUEST &&
                itemTemplate->SubClass == ITEM_SUBCLASS_QUEST &&
                itemTemplate->Bonding  == BIND_QUEST_ITEM)
            {
                item.freeforall = true;
            }
        }
    };

} // namespace GuildVillageAoe

namespace
{
    class guild_village_AoeLootServer : public ServerScript
    {
    public:
        guild_village_AoeLootServer() : ServerScript("guild_village_AoeLootServer") { }

        bool CanPacketReceive(WorldSession* session, WorldPacket& packet) override
        {
            if (packet.GetOpcode() != CMSG_LOOT)
                return true;

            Player* player = session->GetPlayer();
            if (!player)
                return true;

            if (!GuildVillageAoe::IsAoeLootEnabledForPlayer(player))
                return true;

            if (!GuildVillageAoe::IsSessionActive(player))
                return true;

            if (!player->IsInWorld() || player->isDead())
                return true;

            ObjectGuid targetGuid;
            packet >> targetGuid;

            if (Creature* creature = player->GetMap()->GetCreature(targetGuid))
            {
                if (creature->IsAlive())
                {
                    if (player->IsClass(CLASS_ROGUE, CLASS_CONTEXT_ABILITY) &&
                        creature->loot.loot_type == LOOT_PICKPOCKETING)
                    {
                        return true;
                    }
                    return true;
                }

                if (!creature->isDead())
                    return true;
            }
            else
            {
                return true;
            }

            if (player->GetLootGUID().IsEmpty())
            {
                GuildVillageAoe::TriggerAoeLoot(player);
            }

            return true;
        }
    };

    class guild_village_AoeLootPlayer : public PlayerScript
    {
    public:
        guild_village_AoeLootPlayer() : PlayerScript("guild_village_AoeLootPlayer") { }

        void OnPlayerLogin(Player* player) override
        {
            GuildVillageAoe::ClearSession(player);
        }

        void OnPlayerLogout(Player* player) override
        {
            GuildVillageAoe::ClearSession(player);
        }
    };
}

void RegisterGuildVillageAoe()
{
    new guild_village_AoeLootPlayer();
    new guild_village_AoeLootServer();
    new GuildVillageAoe::guild_village_AoeLootQuestParty();
}
