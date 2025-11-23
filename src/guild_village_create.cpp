// modules/mod-guild-village/src/guild_village_create.cpp

#include "ScriptMgr.h"
#include "Config.h"
#include "Creature.h"
#include "ScriptedGossip.h"
#include "GossipDef.h"
#include "Player.h"
#include "Guild.h"
#include "Chat.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"
#include "ObjectAccessor.h"
#include "StringFormat.h"
#include "GuildMgr.h"
#include "Define.h"
#include "DataMap.h"
#include "Maps/MapMgr.h"
#include "GameObject.h"
#include "Transport.h"
#include "Log.h"
#include "gv_common.h"
#include "Item.h"

#include <string>
#include <optional>
#include <algorithm>
#include <unordered_set>

namespace GuildVillage
{
    // ===================== KONFIG =====================
    static inline uint32 PriceGold()      { return sConfigMgr->GetOption<uint32>("GuildVillage.Price.Gold", 0); }
    static inline uint32 PriceItemId()    { return sConfigMgr->GetOption<uint32>("GuildVillage.Price.ItemId", 0); }
    static inline uint32 PriceItemCount() { return sConfigMgr->GetOption<uint32>("GuildVillage.Price.ItemCount", 0); }

    static inline uint32 DefMap()   { return sConfigMgr->GetOption<uint32>("GuildVillage.Default.Map", 37); }
    static inline float  DefX()     { return sConfigMgr->GetOption<float>("GuildVillage.Default.X", 1026.7292f); }
    static inline float  DefY()     { return sConfigMgr->GetOption<float>("GuildVillage.Default.Y", 289.9494f); }
    static inline float  DefZ()     { return sConfigMgr->GetOption<float>("GuildVillage.Default.Z", 332.66083f); }
    static inline float  DefO()     { return sConfigMgr->GetOption<float>("GuildVillage.Default.O", 3.4305837f); }
    static inline uint32 CleanupDays()    { return sConfigMgr->GetOption<uint32>("GuildVillage.Inactivity.CleanupDays", 90); }
    static inline bool   ShowCapacityMsg(){ return sConfigMgr->GetOption<bool>("GuildVillage.ShowCapacityMessage", true); }
    static inline int32 MaxVillagesRaw()
    {
        return sConfigMgr->GetOption<int32>("GuildVillage.MaxVillages", 30);
    }

    static inline bool VillagesUnlimited()
    {
        return MaxVillagesRaw() < 0;
    }

    static inline bool VillagesDisabled()
    {
        return MaxVillagesRaw() == 0;
    }

    static inline uint32 VillagesLimit()
    {
        int32 raw = MaxVillagesRaw();
        return raw > 0 ? uint32(raw) : 0;
    }

    enum class Lang { CS, EN };
    static inline Lang LangOpt()
    {
        std::string loc = sConfigMgr->GetOption<std::string>("GuildVillage.Locale", "cs");
        std::transform(loc.begin(), loc.end(), loc.begin(), ::tolower);
        return (loc == "en" || loc == "english") ? Lang::EN : Lang::CS;
    }
    static inline char const* T(char const* cs, char const* en)
    {
        return (LangOpt() == Lang::EN) ? en : cs;
    }

    static inline uint32 PhaseIdForGuild(uint32 guildId) { return guildId + 10; }

    struct GVPhaseData : public DataMap::Base
    {
        uint32 phaseMask = 0;
    };

    // Aktuální počet vesnic
    static uint32 CountVillages()
    {
        if (QueryResult r = WorldDatabase.Query("SELECT COUNT(*) FROM customs.gv_guild"))
            return (*r)[0].Get<uint32>();
        return 0;
    }

    // ===================== DATA =====================
    struct VillageRow { uint32 guildId; uint32 phase; uint32 map; float x,y,z,o; };

    static std::optional<VillageRow> LoadVillage(uint32 guildId)
    {
        if (QueryResult res = WorldDatabase.Query(
            "SELECT guild, phase, map, positionx, positiony, positionz, orientation "
            "FROM customs.gv_guild WHERE guild={}", guildId))
        {
            Field* f = res->Fetch();
            VillageRow v;
            v.guildId = f[0].Get<uint32>();
            v.phase   = f[1].Get<uint32>();
            v.map     = f[2].Get<uint32>();
            v.x       = f[3].Get<float>();
            v.y       = f[4].Get<float>();
            v.z       = f[5].Get<float>();
            v.o       = f[6].Get<float>();
            return v;
        }
        return std::nullopt;
    }

    static void EnsureCurrencyRow(uint32 guildId)
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT 1 FROM customs.gv_currency WHERE guildId={}", guildId))
            return;

        WorldDatabase.Execute(
            "INSERT INTO customs.gv_currency "
            "(guildId, material1, material2, material3, material4, last_update) "
            "VALUES ({}, 0, 0, 0, 0, NOW())",
            guildId
        );
    }

    // NOVÉ: inicializace expedic pro guildu (gv_expedition_guild)
    // sloupce: heroes_owned, heroes_on_mission, heroes_max, gear_level, last_update
    static void EnsureExpeditionGuildRow(uint32 guildId)
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT 1 FROM customs.gv_expedition_guild WHERE guildId={}", guildId))
            return;

        WorldDatabase.Execute(
            "INSERT INTO customs.gv_expedition_guild "
            "(guildId, heroes_owned, heroes_on_mission, heroes_max, gear_level, last_update) "
            "VALUES ({}, 0, 0, 25, 182, NOW())",
            guildId
        );
    }

    // ===================== LIVE INSTALL: BASE LAYOUT & UPGRADES =====================
    static void InstallBaseLayout(uint32 /*guildId*/, uint32 phaseId, std::string const& layout_key = "base")
    {
        uint32 cCount = 0, goCount = 0;

        // --- CREATURES ---
        if (QueryResult cr = WorldDatabase.Query(
            "SELECT entry, map, position_x, position_y, position_z, orientation, spawntimesecs, spawndist, movementtype "
            "FROM customs.gv_creature_template WHERE layout_key='{}'", layout_key))
        {
            do
            {
                Field* f = cr->Fetch();
                uint32 entry=f[0].Get<uint32>(), mapId=f[1].Get<uint32>();
                float x=f[2].Get<float>(), y=f[3].Get<float>(), z=f[4].Get<float>(), o=f[5].Get<float>();
                uint32 resp=f[6].Get<uint32>(); float wander=f[7].Get<float>(); uint8 mt=f[8].Get<uint8>();

                Map* map = sMapMgr->FindMap(mapId, 0);
                if (!map)
                {
                    sMapMgr->CreateBaseMap(mapId);
                    map = sMapMgr->FindMap(mapId, 0);
                }

                if (!map)
                {
                    WorldDatabase.Execute(
                        "INSERT INTO creature (id1,map,spawnMask,phaseMask,position_x,position_y,position_z,orientation,spawntimesecs,wander_distance,MovementType,Comment) "
                        "VALUES ({}, {}, 1, {}, {}, {}, {}, {}, {}, {}, {}, 'Village mob')",
                        entry, mapId, phaseId, x, y, z, o, resp, wander, (uint32)mt
                    );
                    ++cCount; continue;
                }

                Creature* c = new Creature();
                ObjectGuid::LowType low = map->GenerateLowGuid<HighGuid::Unit>();
                if (!c->Create(low, map, phaseId, entry, 0, x, y, z, o)) { delete c; continue; }

                // jen defaultní delay, NE absolutní čas
                c->SetRespawnDelay(resp);
                c->SetWanderDistance(wander);
                c->SetDefaultMovementType(MovementGeneratorType(mt));

                // uložení
                c->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseId);
                uint32 spawnId = c->GetSpawnId();

                // pojistka do DB
                WorldDatabase.Execute(
                    "UPDATE creature SET spawntimesecs = {}, wander_distance = {}, MovementType = {} WHERE guid = {}",
                    resp, wander, (uint32)mt, spawnId
                );

                // reload + grid
                c->CleanupsBeforeDelete(); delete c;
                c = new Creature();
                if (!c->LoadCreatureFromDB(spawnId, map, true)) { delete c; continue; }
                sObjectMgr->AddCreatureToGrid(spawnId, sObjectMgr->GetCreatureData(spawnId));
                ++cCount;
            }
            while (cr->NextRow());
        }

        // --- GAMEOBJECTS ---
        if (QueryResult go = WorldDatabase.Query(
            "SELECT entry, map, position_x, position_y, position_z, orientation, rotation0, rotation1, rotation2, rotation3, spawntimesecs "
            "FROM customs.gv_gameobject_template WHERE layout_key='{}'", layout_key))
        {
            do
            {
                Field* f = go->Fetch();
                uint32 entry=f[0].Get<uint32>(), mapId=f[1].Get<uint32>();
                float x=f[2].Get<float>(), y=f[3].Get<float>(), z=f[4].Get<float>(), o=f[5].Get<float>();
                float r0=f[6].Get<float>(), r1=f[7].Get<float>(), r2=f[8].Get<float>(), r3=f[9].Get<float>();
                int32 st=f[10].Get<int32>();

                Map* map = sMapMgr->FindMap(mapId, 0);
                if (!map)
                {
                    sMapMgr->CreateBaseMap(mapId);
                    map = sMapMgr->FindMap(mapId, 0);
                }

                if (map)
                {
                    GameObject* g = sObjectMgr->IsGameObjectStaticTransport(entry) ? new StaticTransport() : new GameObject();
                    ObjectGuid::LowType low = map->GenerateLowGuid<HighGuid::GameObject>();
                    if (!g->Create(low, entry, map, phaseId, x, y, z, o, G3D::Quat(r0,r1,r2,r3), 0, GO_STATE_READY))
                    { delete g; continue; }

                    g->SetRespawnTime(st);

                    // uložit
                    g->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseId);
                    uint32 spawnId = g->GetSpawnId();

                    WorldDatabase.Execute(
                        "UPDATE gameobject SET spawntimesecs = {} WHERE guid = {}",
                        st, spawnId
                    );

                    // reload do mapy + grid
                    g->CleanupsBeforeDelete(); delete g;
                    g = sObjectMgr->IsGameObjectStaticTransport(entry) ? new StaticTransport() : new GameObject();
                    if (!g->LoadGameObjectFromDB(spawnId, map, true)) { delete g; continue; }
                    sObjectMgr->AddGameobjectToGrid(spawnId, sObjectMgr->GetGameObjectData(spawnId));
                    ++goCount;
                }
            }
            while (go->NextRow());
        }

        LOG_INFO("modules", "GV: Installed base layout '{}' -> creatures={}, gameobjects={}, phaseId={}",
                 layout_key, cCount, goCount, phaseId);
    }

    static bool InstallExpansionForGuild(uint32 guildId, uint32 phaseId, std::string const& key)
    {
        if (QueryResult q = WorldDatabase.Query(
                "SELECT 1 FROM customs.gv_upgrades WHERE guildId={} AND expansion_key='{}'", guildId, key))
            return false;

        if (QueryResult cr = WorldDatabase.Query(
            "SELECT entry, map, position_x, position_y, position_z, orientation, spawntimesecs, spawndist, movementtype "
            "FROM customs.gv_expansion_creatures WHERE expansion_key='{}'", key))
        {
            do
            {
                Field* f = cr->Fetch();
                uint32 entry=f[0].Get<uint32>(), mapId=f[1].Get<uint32>();
                float x=f[2].Get<float>(), y=f[3].Get<float>(), z=f[4].Get<float>(), o=f[5].Get<float>();
                uint32 resp=f[6].Get<uint32>(); float wander=f[7].Get<float>(); uint8 mt=f[8].Get<uint8>();

                Map* map = sMapMgr->FindMap(mapId, 0);
                if (!map)
                {
                    WorldDatabase.Execute(
                        "INSERT INTO creature (id1,map,spawnMask,phaseMask,position_x,position_y,position_z,orientation,spawntimesecs,wander_distance,MovementType) "
                        "VALUES ({}, {}, 1, {}, {}, {}, {}, {}, {}, {}, {})",
                        entry, mapId, phaseId, x, y, z, o, resp, wander, (uint32)mt
                    );
                    continue;
                }

                Creature* c = new Creature();
                ObjectGuid::LowType low = map->GenerateLowGuid<HighGuid::Unit>();
                if (!c->Create(low, map, phaseId, entry, 0, x, y, z, o)) { delete c; continue; }

                c->SetRespawnDelay(resp);
                c->SetWanderDistance(wander);
                c->SetDefaultMovementType(MovementGeneratorType(mt));

                // uložit do DB
                c->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseId);
                uint32 spawnId = c->GetSpawnId();

                // pojistka
                WorldDatabase.Execute(
                    "UPDATE creature SET spawntimesecs = {}, wander_distance = {}, MovementType = {} WHERE guid = {}",
                    resp, wander, (uint32)mt, spawnId
                );

                // reload + grid
                c->CleanupsBeforeDelete(); delete c;
                c = new Creature();
                if (!c->LoadCreatureFromDB(spawnId, map, /*addToMap=*/true)) { delete c; continue; }
                sObjectMgr->AddCreatureToGrid(spawnId, sObjectMgr->GetCreatureData(spawnId));
            }
            while (cr->NextRow());
        }

        if (QueryResult go = WorldDatabase.Query(
            "SELECT entry, map, position_x, position_y, position_z, orientation, rotation0, rotation1, rotation2, rotation3, spawntimesecs "
            "FROM customs.gv_expansion_gameobjects WHERE expansion_key='{}'", key))
        {
            do
            {
                Field* f = go->Fetch();
                uint32 entry=f[0].Get<uint32>(), mapId=f[1].Get<uint32>();
                float x=f[2].Get<float>(), y=f[3].Get<float>(), z=f[4].Get<float>(), o=f[5].Get<float>();
                float r0=f[6].Get<float>(), r1=f[7].Get<float>(), r2=f[8].Get<float>(), r3=f[9].Get<float>();
                int32 st=f[10].Get<int32>();

                Map* map = sMapMgr->FindMap(mapId, 0);
                if (!map)
                {
                    WorldDatabase.Execute(
                        "INSERT INTO gameobject (id,map,spawnMask,phaseMask,position_x,position_y,position_z,orientation,rotation0,rotation1,rotation2,rotation3,spawntimesecs) "
                        "VALUES ({}, {}, 1, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
                        entry, mapId, phaseId, x, y, z, o, r0, r1, r2, r3, st
                    );
                    continue;
                }

                GameObject* g = sObjectMgr->IsGameObjectStaticTransport(entry) ? new StaticTransport() : new GameObject();
                ObjectGuid::LowType low = map->GenerateLowGuid<HighGuid::GameObject>();
                if (!g->Create(low, entry, map, phaseId, x, y, z, o, G3D::Quat(r0,r1,r2,r3), 0, GO_STATE_READY))
                { delete g; continue; }
                g->SetRespawnTime(st);
                g->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseId);
                uint32 spawnId = g->GetSpawnId();

                // pojistka: propsat spawntimesecs
                WorldDatabase.Execute(
                    "UPDATE gameobject SET spawntimesecs = {} WHERE guid = {}",
                    st, spawnId
                );

                // reload + grid
                g->CleanupsBeforeDelete(); delete g;
                g = sObjectMgr->IsGameObjectStaticTransport(entry) ? new StaticTransport() : new GameObject();
                if (!g->LoadGameObjectFromDB(spawnId, map, true)) { delete g; continue; }
                sObjectMgr->AddGameobjectToGrid(spawnId, sObjectMgr->GetGameObjectData(spawnId));
            }
            while (go->NextRow());
        }

        WorldDatabase.Execute(
            "INSERT INTO customs.gv_upgrades (guildId, expansion_key, purchased_at) "
            "VALUES ({}, '{}', UNIX_TIMESTAMP())",
            guildId, key
        );
        return true;
    }

    // helper na batch mazání respawnů z characters DB
    static void DeleteRespawnsByGuids(std::string const& table, std::vector<uint32> const& guids, size_t batch = 500)
    {
        if (guids.empty())
            return;

        for (size_t i = 0; i < guids.size(); i += batch)
        {
            size_t j = std::min(i + batch, guids.size());
            std::ostringstream inlist;
            for (size_t k = i; k < j; ++k)
            {
                if (k != i)
                    inlist << ',';
                inlist << guids[k];
            }

            CharacterDatabase.Execute("DELETE FROM " + table + " WHERE guid IN (" + inlist.str() + ")");
        }
    }

    // ===== Helper: kompletní wipe =====
    static void CleanupVillageForGuild(uint32 guildId)
    {
        // zjistit phaseId pro guildu
        uint32 phaseId = 0;
        if (QueryResult r = WorldDatabase.Query("SELECT phase FROM customs.gv_guild WHERE guild={}", guildId))
            phaseId = (*r)[0].Get<uint32>();
        if (!phaseId)
            phaseId = PhaseIdForGuild(guildId); // fallback

        // 0) smazat měny / upgrady / produkci / expedice / tp bod
        WorldDatabase.Execute("DELETE FROM customs.gv_currency           WHERE guildId={}", guildId);
        WorldDatabase.Execute("DELETE FROM customs.gv_upgrades           WHERE guildId={}", guildId);
        WorldDatabase.Execute("DELETE FROM customs.gv_production_active  WHERE guildId={}", guildId);
        WorldDatabase.Execute("DELETE FROM customs.gv_production_upgrade WHERE guildId={}", guildId);
        WorldDatabase.Execute("DELETE FROM customs.gv_expedition_active  WHERE guildId={}", guildId);
        WorldDatabase.Execute("DELETE FROM customs.gv_expedition_loot    WHERE guildId={}", guildId);
        WorldDatabase.Execute("DELETE FROM customs.gv_expedition_guild   WHERE guildId={}", guildId);
		WorldDatabase.Execute("DELETE FROM customs.gv_guild_quests       WHERE guildId={}", guildId);
		WorldDatabase.Execute("DELETE FROM customs.gv_teleport_player    WHERE guild={}", guildId);
		

        {
            // seber GUIDy z world.creature
            std::vector<uint32> creatureGuids;
            if (QueryResult qc = WorldDatabase.Query(
                    "SELECT guid FROM creature WHERE map={} AND phaseMask={}", DefMap(), phaseId))
            {
                do
                {
                    creatureGuids.emplace_back(qc->Fetch()[0].Get<uint32>());
                }
                while (qc->NextRow());
            }

            // seber GUIDy z world.gameobject
            std::vector<uint32> goGuids;
            if (QueryResult qg = WorldDatabase.Query(
                    "SELECT guid FROM gameobject WHERE map={} AND phaseMask={}", DefMap(), phaseId))
            {
                do
                {
                    goGuids.emplace_back(qg->Fetch()[0].Get<uint32>());
                }
                while (qg->NextRow());
            }

            // smazat odpovídající respawny v characters DB
            DeleteRespawnsByGuids("creature_respawn", creatureGuids);
            DeleteRespawnsByGuids("gameobject_respawn", goGuids);

            LOG_INFO("modules",
                "GV: Cleanup respawns for guild {} (phaseId={}, map={}, creatures={}, gos={})",
                guildId, phaseId, DefMap(), creatureGuids.size(), goGuids.size());
        }

        // 1) smazat samotné spawny z world.*
        WorldDatabase.Execute("DELETE FROM creature   WHERE map={} AND phaseMask={}", DefMap(), phaseId);
        WorldDatabase.Execute("DELETE FROM gameobject WHERE map={} AND phaseMask={}", DefMap(), phaseId);

        // 2) a nakonec záznam vesnice
        WorldDatabase.Execute("DELETE FROM customs.gv_guild WHERE guild={}", guildId);

        LOG_INFO("modules", "GV: Cleanup done for guild {} (phaseId={})", guildId, phaseId);
    }

    // ===== PUBLIC API pro GM =====
    bool GuildHasVillage(uint32 guildId) { return LoadVillage(guildId).has_value(); }

    bool CreateVillageForGuild_GM(uint32 guildId, bool ignoreCapacity)
    {
        if (GuildHasVillage(guildId))
            return false;

        if (!ignoreCapacity)
        {
            if (!VillagesUnlimited() && CountVillages() >= VillagesLimit())
                return false;
        }

        uint32 phaseId = PhaseIdForGuild(guildId);
        WorldDatabase.Execute(
            "INSERT INTO customs.gv_guild (guild, phase, map, positionx, positiony, positionz, orientation, last_update) "
            "VALUES ({}, {}, {}, {}, {}, {}, {}, NOW())",
            guildId, phaseId, DefMap(), DefX(), DefY(), DefZ(), DefO()
        );

        EnsureCurrencyRow(guildId);
        EnsureExpeditionGuildRow(guildId);
        InstallBaseLayout(guildId, phaseId, "base");
        return true;
    }

    bool DeleteVillageForGuild_GM(uint32 guildId)
    {
        if (!GuildHasVillage(guildId)) return false;
        CleanupVillageForGuild(guildId);
        return true;
    }

    // ===================== NÁKUP VESNICE (hráči) =====================
    enum GossipAction : uint32 { ACT_TELEPORT = 1001, ACT_BUY, ACT_CONFIRM_BUY };

    static bool CreateVillageForGuild(Player* player)
    {
        ChatHandler ch(player->GetSession());
        Guild* g = player->GetGuild();
        if (!g) return false;

        uint32 guildId = g->GetId();

        if (LoadVillage(guildId).has_value())
        {
            ch.SendSysMessage(T("Tvoje guilda už vesnici vlastní.", "Your guild already owns a village."));
            return false;
        }

        if (VillagesDisabled())
        {
            ch.SendSysMessage(T("Nákup vesnice je momentálně zakázán.", "Village purchase is currently disabled."));
            return false;
        }

        if (!VillagesUnlimited() && CountVillages() >= VillagesLimit())
        {
            ch.SendSysMessage(T("Kapacita vesnic je plná. Počkej na uvolnění slotu.",
                                "Village capacity is full. Please wait for a slot to free up."));
            return false;
        }

        uint64 needCopper = (uint64)PriceGold() * 10000ULL;
        if (PriceGold() > 0 && player->GetMoney() < needCopper)
        { ch.SendSysMessage(T("Nemáš dost zlata.", "You don't have enough gold.")); return false; }

        if (PriceItemId() > 0 && PriceItemCount() > 0 &&
            player->GetItemCount(PriceItemId(), true) < PriceItemCount())
        { ch.SendSysMessage(T("Chybí požadované itemy.", "Missing required items.")); return false; }

        if (needCopper > 0) player->ModifyMoney(-(int64)needCopper);
        if (PriceItemId() > 0 && PriceItemCount() > 0) player->DestroyItemCount(PriceItemId(), PriceItemCount(), true);

        uint32 phaseId = PhaseIdForGuild(guildId);
        WorldDatabase.Execute(
            "INSERT INTO customs.gv_guild "
            "(guild, phase, map, positionx, positiony, positionz, orientation, last_update) "
            "VALUES ({}, {}, {}, {}, {}, {}, {}, NOW())",
            guildId, phaseId, DefMap(), DefX(), DefY(), DefZ(), DefO()
        );

        EnsureCurrencyRow(guildId);
        EnsureExpeditionGuildRow(guildId);
        InstallBaseLayout(guildId, phaseId, "base");

        ch.SendSysMessage(T("Gratuluji! Tvoje guilda zakoupila guildovní vesnici.",
                            "Congratulations! Your guild has purchased a village."));
        return true;
    }

    static void CleanupInactiveVillages()
    {
        uint32 days = CleanupDays();
        if (!days)
            return;

        if (QueryResult r = CharacterDatabase.Query(
            "SELECT g.guildid "
            "FROM guild AS g "
            "JOIN customs.gv_guild AS v ON v.guild = g.guildid "
            "LEFT JOIN characters AS c ON c.guid = g.leaderGuid "
            "WHERE ("
            "      c.guid IS NULL "
            "   OR (c.logout_time > 0 "
            "       AND c.logout_time < UNIX_TIMESTAMP() - {}*24*3600)"
            ")",
            days))
        {
            do
            {
                uint32 gid = r->Fetch()[0].Get<uint32>();
                CleanupVillageForGuild(gid);
            }
            while (r->NextRow());
        }
    }

    // ===================== GOSSIP: SELLER =====================
    static void ShowMain(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        Guild* g = player->GetGuild();
        if (!g)
        {
            if (ShowCapacityMsg())
            {
                // neukazovat nic když je unlimited (-1)
                if (!VillagesUnlimited())
                {
                    if (VillagesDisabled())
                    {
                        ChatHandler(player->GetSession()).SendSysMessage(
                            T("Nákup vesnic je zakázán.", "Village purchase is disabled.")
                        );
                    }
                    else
                    {
                        ChatHandler(player->GetSession()).SendSysMessage(
                            Acore::StringFormat("{}{} / {} {}",
                                T("Obsazeno vesnic: ", "Villages in use: "),
                                CountVillages(),
                                VillagesLimit(),
                                T("(sloty se uvolní po disband/neaktivitě).",
                                "(slots free up after disband/inactivity).")
                            ).c_str()
                        );
                    }
                }
            }

            ChatHandler(player->GetSession()).SendSysMessage(
                T("Nejsi v žádné guildě.", "You are not in a guild.")
            );
            SendGossipMenuFor(player, 1, creature->GetGUID());
            return;
        }

        auto row = LoadVillage(g->GetId());

        if (!row.has_value() && ShowCapacityMsg())
        {
            if (!VillagesUnlimited())
            {
                if (VillagesDisabled())
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Nákup vesnic je zakázán.", "Village purchase is disabled.")
                    );
                }
                else
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        Acore::StringFormat("{}{} / {} {}",
                            T("Obsazeno vesnic: ", "Villages in use: "),
                            CountVillages(),
                            VillagesLimit(),
                            T("(sloty se uvolní po disband/neaktivitě).",
                            "(slots free up after disband/inactivity).")
                        ).c_str()
                    );
                }
            }
        }

        if (row.has_value())
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                            T("Teleportovat do guild vesnice", "Teleport to guild village"),
                            GOSSIP_SENDER_MAIN, 1001);
        }
        else
        {
            if (!VillagesDisabled())
            {
                AddGossipItemFor(player, GOSSIP_ICON_VENDOR,
                                T("Zakoupit guild vesnici", "Purchase guild village"),
                                GOSSIP_SENDER_MAIN, 1002);
            }
        }

        SendGossipMenuFor(player, 1, creature->GetGUID());
    }

    class npc_guild_village_seller : public CreatureScript
    {
    public:
        npc_guild_village_seller() : CreatureScript("npc_guild_village_seller") { }

        bool OnGossipHello(Player* player, Creature* creature) override
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Vítej! Tady vyřídíš guildovní vesnici.", "Welcome! Manage your guild village here."));
            ShowMain(player, creature);
            return true;
        }

        bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
        {
            if (sender != GOSSIP_SENDER_MAIN)
                return false;

            Guild* g = player->GetGuild();
            if (!g)
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Nejsi v žádné guildě.", "You are not in a guild."));
                CloseGossipMenuFor(player);
                return true;
            }

            switch (action)
            {
                case 1001: // ACT_TELEPORT
                {
                    auto row = LoadVillage(g->GetId());
                    if (!row.has_value())
                    {
                        ChatHandler(player->GetSession()).SendSysMessage(
                            T("Tvoje guilda vesnici nevlastní.", "Your guild does not own a village."));
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    auto* stash = player->CustomData.GetDefault<GVPhaseData>("gv_phase");
                    stash->phaseMask = row->phase;

                    player->TeleportTo(row->map, row->x, row->y, row->z, row->o);

                    CloseGossipMenuFor(player);
                    return true;
                }

                case 1002: // ACT_BUY
                {
                    ClearGossipMenuFor(player);

                    std::string priceInfo = T("Cena: ", "Price: ");
                    bool empty = true;

                    if (PriceGold() > 0)
                    {
                        priceInfo += Acore::StringFormat("{}g", PriceGold());
                        empty = false;
                    }

                    if (PriceItemId() > 0 && PriceItemCount() > 0)
                    {
                        if (!empty) priceInfo += " + ";

                        std::string itemName;
                        if (auto const* proto = sObjectMgr->GetItemTemplate(PriceItemId()))
                            itemName = proto->Name1;
                        else
                            itemName = Acore::StringFormat("Item {}", PriceItemId());

                        priceInfo += Acore::StringFormat("{}x {}", PriceItemCount(), itemName);
                        empty = false;
                    }

                    if (empty)
                        priceInfo += T("zdarma", "free");

                    ChatHandler(player->GetSession()).SendSysMessage(priceInfo.c_str());

                    AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                                    T("Ano, chci koupit vesnici", "Yes, purchase the village"),
                                    GOSSIP_SENDER_MAIN, 1003);
                    SendGossipMenuFor(player, 1, creature->GetGUID());
                    return true;
                }

                case 1003: // ACT_CONFIRM_BUY
                {
                    if (g->GetLeaderGUID() != player->GetGUID())
                    {
                        ChatHandler(player->GetSession()).SendSysMessage(
                            T("Pouze Guild Master může provést nákup.", "Only the Guild Master can purchase."));
                        CloseGossipMenuFor(player);
                        return true;
                    }

                    if (CreateVillageForGuild(player))
                        ChatHandler(player->GetSession()).SendSysMessage(T("Nákup dokončen.", "Purchase complete."));

                    CloseGossipMenuFor(player);
                    return true;
                }
            }

            CloseGossipMenuFor(player);
            return true;
        }
    };

    // ===================== PHASING ENFORCER (globální) =====================
    class guild_village_Global : public GlobalScript
    {
    public:
        guild_village_Global() : GlobalScript("guild_village_Global") {}

        void OnBeforeWorldObjectSetPhaseMask(WorldObject const* worldObject,
                                             uint32& /*oldPhaseMask*/, uint32& newPhaseMask,
                                             bool& useCombinedPhases, bool& /*update*/) override
        {
            if (worldObject->GetMapId() == DefMap() ||
                worldObject->GetZoneId() == 268 ||
                worldObject->GetAreaId() == 268)
            {
                useCombinedPhases = false;

                if (Player const* p = worldObject->ToPlayer())
                {
                    if (uint32 gid = p->GetGuildId())
                        if (QueryResult r = WorldDatabase.Query("SELECT phase FROM customs.gv_guild WHERE guild={}", gid))
                            if (uint32 phaseId = (*r)[0].Get<uint32>())
                                newPhaseMask = phaseId;
                }
                return;
            }

            useCombinedPhases = true;
        }
    };

} // namespace GuildVillage

class guild_village_World : public WorldScript
{
public:
    guild_village_World() : WorldScript("guild_village_World") { }

    // zavolá se při startu worldserveru
    void OnStartup() override
    {
        LOG_INFO("modules", "GV: Startup cleanup check begin.");
        GuildVillage::CleanupInactiveVillages();
        LOG_INFO("modules", "GV: Startup cleanup check done.");

        _cleanupTimerMs = CLEANUP_INTERVAL_MS;
    }

    // volá se pořád, diff = kolik ms uběhlo od minula
    void OnUpdate(uint32 diff) override
    {
        if (_cleanupTimerMs <= diff)
        {
            LOG_INFO("modules", "GV: Periodic cleanup check begin.");
            GuildVillage::CleanupInactiveVillages();
            LOG_INFO("modules", "GV: Periodic cleanup check done.");

            _cleanupTimerMs = CLEANUP_INTERVAL_MS;
        }
        else
        {
            _cleanupTimerMs -= diff;
        }
    }

private:
    static constexpr uint32 CLEANUP_INTERVAL_MS = 60 * 60 * 1000; // 1 hodina
    uint32 _cleanupTimerMs = CLEANUP_INTERVAL_MS;
};

// ===== REGISTRACE =====
void RegisterGuildVillageCreate()
{
    new GuildVillage::npc_guild_village_seller();
    new GuildVillage::guild_village_Global();
    new guild_village_World();
}
