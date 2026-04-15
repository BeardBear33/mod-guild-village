// modules/mod-guild-village/src/guild_village_gm.cpp

#include "ScriptMgr.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "WorldSession.h"
#include "Player.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "StringFormat.h"
#include "Config.h"

#include "Maps/MapMgr.h"
#include "Map.h"
#include "GameObject.h"
#include "Transport.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "Creature.h"
#include "Guild.h"
#include "DataMap.h"

#include <string>
#include <algorithm>
#include <cctype>
#include <limits>
#include <vector>
#include <cmath>
#include <sstream>

// === PUBLIC API z guild_village_create.cpp ===
namespace GuildVillage {
    bool GuildHasVillage(uint32 guildId);
    bool CreateVillageForGuild_GM(uint32 guildId, bool ignoreCapacity);
    bool DeleteVillageForGuild_GM(uint32 guildId);
}

static inline uint32 DefMap() { return sConfigMgr->GetOption<uint32>("GuildVillage.Default.Map", 37); }

namespace
{
    // per-player stash stejného jména/typu (.commands/.respawn…)
    struct GVPhaseData : public DataMap::Base { uint32 phaseMask = 0; };

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

    static inline std::string Trim(std::string s)
    {
        auto ns = [](int ch){ return !std::isspace(ch); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns));
        s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end());
        return s;
    }

    // Smaže respawny pro zadané GUIDy po dávkách z characters DB
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
                if (k != i) inlist << ',';
                inlist << guids[k];
            }
            CharacterDatabase.Execute("DELETE FROM " + table + " WHERE guid IN (" + inlist.str() + ")");
        }
    }

    // --- Despawn všech creature/GO pro daný phaseId na mapId (okamžitě ze světa) ---
    static void DespawnPhaseObjects(uint32 mapId, uint32 phaseId)
    {
        std::vector<uint32> cGuids;
        std::vector<uint32> gGuids;

        if (QueryResult qc = WorldDatabase.Query(
                "SELECT guid FROM creature WHERE map = {} AND phaseMask = {}", mapId, phaseId))
        {
            do { cGuids.emplace_back((*qc)[0].Get<uint32>()); } while (qc->NextRow());
        }

        if (QueryResult qg = WorldDatabase.Query(
                "SELECT guid FROM gameobject WHERE map = {} AND phaseMask = {}", mapId, phaseId))
        {
            do { gGuids.emplace_back((*qg)[0].Get<uint32>()); } while (qg->NextRow());
        }

        Map* map = sMapMgr->FindMap(mapId, 0);
        if (!map)
            return;

        {
            auto& cStore = map->GetCreatureBySpawnIdStore();
            for (uint32 spawnId : cGuids)
            {
                auto it = cStore.find(spawnId);
                if (it != cStore.end() && it->second)
                    it->second->AddObjectToRemoveList();
            }
        }
        {
            auto& gStore = map->GetGameObjectBySpawnIdStore();
            for (uint32 spawnId : gGuids)
            {
                auto it = gStore.find(spawnId);
                if (it != gStore.end() && it->second)
                    it->second->AddObjectToRemoveList();
            }
        }

        map->RemoveAllObjectsInRemoveList();
    }

    // === Live instalace BASE layoutu (převzato z create.cpp) ===
    static void InstallBaseLayout_Live(uint32 /*guildId*/, uint32 phaseId, std::string const& layout_key = "base")
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
                uint32 entry      = f[0].Get<uint32>();
                uint32 mapId      = f[1].Get<uint32>();
                float  x          = f[2].Get<float>();
                float  y          = f[3].Get<float>();
                float  z          = f[4].Get<float>();
                float  o          = f[5].Get<float>();
                uint32 respawnSec = f[6].Get<uint32>();
                float  wander     = f[7].Get<float>();
                uint8  moveType   = f[8].Get<uint8>();

                Map* map = sMapMgr->FindMap(mapId, 0);
                if (!map)
                {
                    sMapMgr->CreateBaseMap(mapId);
                    map = sMapMgr->FindMap(mapId, 0);
                }

                if (!map)
                {
                    WorldDatabase.Execute(
                        "INSERT INTO creature "
                        "(id1, map, spawnMask, phaseMask, position_x, position_y, position_z, orientation, "
                        " spawntimesecs, wander_distance, MovementType, Comment) "
                        "VALUES ({}, {}, 1, {}, {}, {}, {}, {}, {}, {}, {}, 'Village mob')",
                        entry, mapId, phaseId, x, y, z, o, respawnSec, wander, (uint32)moveType
                    );
                    ++cCount;
                    continue;
                }

                Creature* c = new Creature();
                ObjectGuid::LowType low = map->GenerateLowGuid<HighGuid::Unit>();
                if (!c->Create(low, map, phaseId, entry, 0, x, y, z, o))
                { delete c; continue; }

                // správně: defaultní respawn delay (ne absolutní čas)
                c->SetRespawnDelay(respawnSec);
                c->SetWanderDistance(wander);
                c->SetDefaultMovementType(MovementGeneratorType(moveType));

                c->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseId);
                uint32 spawnId = c->GetSpawnId();

                // pojistka: přepsat hodnoty v DB (jinak hrozí výchozích 300)
                WorldDatabase.Execute(
                    "UPDATE creature SET spawntimesecs = {}, wander_distance = {}, MovementType = {}, Comment='Village mob' WHERE guid = {}",
                    respawnSec, wander, (uint32)moveType, spawnId
                );

                c->CleanupsBeforeDelete(); delete c;

                c = new Creature();
                if (!c->LoadCreatureFromDB(spawnId, map, /*addToMap=*/true)) { delete c; continue; }
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
                uint32 entry = f[0].Get<uint32>();
                uint32 mapId = f[1].Get<uint32>();
                float  x = f[2].Get<float>();
                float  y = f[3].Get<float>();
                float  z = f[4].Get<float>();
                float  o = f[5].Get<float>();
                int32  st = f[10].Get<int32>();

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

                    if (!g->Create(low, entry, map, phaseId, x, y, z, o, G3D::Quat(), 0, GO_STATE_READY))
                    { delete g; continue; }

                    g->SetRespawnTime(st);
                    g->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseId);
                    uint32 spawnId = g->GetSpawnId();

                    // pojistka: přepsat spawntimesecs i v DB
                    WorldDatabase.Execute(
                        "UPDATE gameobject SET spawntimesecs = {} WHERE guid = {}",
                        st, spawnId
                    );

                    g->CleanupsBeforeDelete(); delete g;

                    g = sObjectMgr->IsGameObjectStaticTransport(entry) ? new StaticTransport() : new GameObject();
                    if (!g->LoadGameObjectFromDB(spawnId, map, true)) { delete g; continue; }
                    sObjectMgr->AddGameobjectToGrid(spawnId, sObjectMgr->GetGameObjectData(spawnId));
                    ++goCount;
                }
                else
                {
                    WorldDatabase.Execute(
                        "INSERT INTO gameobject "
                        "(id, map, spawnMask, phaseMask, position_x, position_y, position_z, orientation, "
                        " rotation0, rotation1, rotation2, rotation3, spawntimesecs) "
                        "VALUES ({}, {}, 1, {}, {}, {}, {}, {}, 0, 0, 0, 0, {})",
                        entry, mapId, phaseId, x, y, z, o, st
                    );
                    ++goCount;
                }
            }
            while (go->NextRow());
        }

        LOG_INFO("modules", "GV: (GM reset) Installed base layout '{}' -> creatures={}, gameobjects={}, phaseId={}",
                 layout_key, cCount, goCount, phaseId);
    }

    // Pomocné: stránkovaný výpis z customs.gv_guild
    static void CmdListVillages(ChatHandler* handler, uint32 page)
    {
        constexpr uint32 kPageSize = 10;

        // celkový count
        uint32 total = 0;
        if (QueryResult rc = WorldDatabase.Query("SELECT COUNT(*) FROM customs.gv_guild"))
            total = (*rc)[0].Get<uint32>();

        if (total == 0)
        {
            handler->SendSysMessage(T("|cffffaa00[GV]|r Není vytvořena žádná vesnice.",
                                      "|cffffaa00[GV]|r No villages exist yet."));
            return;
        }

        uint32 totalPages = (total + kPageSize - 1) / kPageSize;
        if (page == 0) page = 1;
        if (page > totalPages) page = totalPages;

        uint32 offset = (page - 1) * kPageSize;

        std::vector<uint32> guildIds;
        if (QueryResult rl = WorldDatabase.Query(
                "SELECT guild FROM customs.gv_guild ORDER BY guild LIMIT {} OFFSET {}",
                kPageSize, offset))
        {
            do { guildIds.emplace_back(rl->Fetch()[0].Get<uint32>()); } while (rl->NextRow());
        }

        handler->SendSysMessage(Acore::StringFormat(T("Stránka {}/{}", "Page {}/{}"),
                                                    page, totalPages).c_str());

        uint32 idxOnPage = 1;
        for (uint32 gid : guildIds)
        {
            std::string name = T("<neznámá>", "<unknown>");
            if (QueryResult rn = CharacterDatabase.Query(
                    "SELECT name FROM guild WHERE guildid={}", gid))
            {
                name = rn->Fetch()[0].Get<std::string>();
            }

            handler->SendSysMessage(
                Acore::StringFormat("{}{}. {} - ID: {}",
                                    (idxOnPage < 10 ? " " : ""), // drobná kosmetika zarovnání
                                    idxOnPage, name, gid).c_str()
            );
            ++idxOnPage;
        }
    }

    // === Helper: Determine guild faction based on guildmaster ===
    static uint8 GetGuildFaction(uint32 guildId)
    {
        // Najdi guildmastera z guildY
        uint32 leaderGuid = 0;
        if (QueryResult r = CharacterDatabase.Query(
                "SELECT leaderGuid FROM guild WHERE guildid={}", guildId))
        {
            leaderGuid = r->Fetch()[0].Get<uint32>();
        }

        if (!leaderGuid) return 0; // Fallback: neutrální/neznámý

        // Vezmi faktci playera (race určuje faktci)
        // Aliance: races 1, 3, 4, 7, 11 (Human, Dwarf, Gnome, Draenei, Worgen)
        // Horda: races 2, 5, 6, 8, 9, 10 (Orc, Tauren, Undead, Troll, Blood Elf, Goblin)
        if (QueryResult r = CharacterDatabase.Query(
                "SELECT race FROM characters WHERE guid={}", leaderGuid))
        {
            uint8 race = r->Fetch()[0].Get<uint8>();
            // Aliance fact = 1, Horda faction = 2, neutrální = 0
            if (race == 1 || race == 3 || race == 4 || race == 7 || race == 11)
                return 1; // Aliance
            else if (race == 2 || race == 5 || race == 6 || race == 8 || race == 9 || race == 10)
                return 2; // Horda
        }

        return 0; // Default: neutrální
    }

    static bool CreatureSpawnExistsAtPosition(uint32 mapId, uint32 phaseId, uint32 entry, float x, float y, float z)
    {
        // Duplicitu kontrolujeme podle entry + phase + pozice (tolerance),
        // aby stejný entry mohl existovat na více různých spawn pozicích.
        if (QueryResult r = WorldDatabase.Query(
                "SELECT 1 FROM creature "
                "WHERE map={} AND phaseMask={} AND id1={} "
                "  AND ABS(position_x - {}) < 0.05 "
                "  AND ABS(position_y - {}) < 0.05 "
                "  AND ABS(position_z - {}) < 0.10 "
                "LIMIT 1",
                mapId, phaseId, entry, x, y, z))
        {
            return true;
        }
        return false;
    }

    static bool SpawnExpansionCreatureLive(
        uint32 mapId,
        uint32 phaseId,
        uint32 entry,
        float x,
        float y,
        float z,
        float o,
        uint32 respawnSec,
        float wanderDist,
        uint8 moveType)
    {
        Map* map = sMapMgr->FindMap(mapId, 0);
        if (!map)
        {
            sMapMgr->CreateBaseMap(mapId);
            map = sMapMgr->FindMap(mapId, 0);
        }

        // Fallback: jen DB insert (když mapa není aktivní)
        if (!map)
        {
            WorldDatabase.Execute(
                "INSERT INTO creature "
                "(id1, map, spawnMask, phaseMask, position_x, position_y, position_z, orientation, "
                " spawntimesecs, wander_distance, MovementType, Comment) "
                "VALUES ({}, {}, 1, {}, {}, {}, {}, {}, {}, {}, {}, 'Init mod expansion')",
                entry, mapId, phaseId, x, y, z, o,
                respawnSec, wanderDist, (uint32)moveType
            );
            return true;
        }

        Creature* c = new Creature();
        ObjectGuid::LowType low = map->GenerateLowGuid<HighGuid::Unit>();
        if (!c->Create(low, map, phaseId, entry, 0, x, y, z, o))
        {
            delete c;
            return false;
        }

        c->SetRespawnDelay(respawnSec);
        c->SetWanderDistance(wanderDist);
        c->SetDefaultMovementType(MovementGeneratorType(moveType));

        c->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseId);
        uint32 spawnId = c->GetSpawnId();

        WorldDatabase.Execute(
            "UPDATE creature "
            "SET spawntimesecs = {}, wander_distance = {}, MovementType = {}, Comment='Init mod expansion' "
            "WHERE guid = {}",
            respawnSec, wanderDist, (uint32)moveType,
            spawnId
        );

        c->CleanupsBeforeDelete();
        delete c;

        c = new Creature();
        if (!c->LoadCreatureFromDB(spawnId, map, /*addToMap=*/true))
        {
            delete c;
            return true; // DB je ok, jen se live nepodařilo načíst
        }

        sObjectMgr->AddCreatureToGrid(spawnId, sObjectMgr->GetCreatureData(spawnId));
        return true;
    }

    static bool GameObjectSpawnExistsAtPosition(uint32 mapId, uint32 phaseId, uint32 entry, float x, float y, float z)
    {
        // Stejná logika jako u creature: entry + phase + pozice s tolerancí.
        if (QueryResult r = WorldDatabase.Query(
                "SELECT 1 FROM gameobject "
                "WHERE map={} AND phaseMask={} AND id={} "
                "  AND ABS(position_x - {}) < 0.05 "
                "  AND ABS(position_y - {}) < 0.05 "
                "  AND ABS(position_z - {}) < 0.10 "
                "LIMIT 1",
                mapId, phaseId, entry, x, y, z))
        {
            return true;
        }
        return false;
    }

    static bool SpawnExpansionGameObjectLive(
        uint32 mapId,
        uint32 phaseId,
        uint32 entry,
        float x,
        float y,
        float z,
        float o,
        float r0,
        float r1,
        float r2,
        float r3,
        int32 spawntimesecs)
    {
        Map* map = sMapMgr->FindMap(mapId, 0);
        if (!map)
        {
            sMapMgr->CreateBaseMap(mapId);
            map = sMapMgr->FindMap(mapId, 0);
        }

        if (!map)
        {
            WorldDatabase.Execute(
                "INSERT INTO gameobject "
                "(id, map, spawnMask, phaseMask, position_x, position_y, position_z, orientation, "
                " rotation0, rotation1, rotation2, rotation3, spawntimesecs) "
                "VALUES ({}, {}, 1, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
                entry, mapId, phaseId, x, y, z, o, r0, r1, r2, r3, spawntimesecs
            );
            return true;
        }

        GameObject* g = sObjectMgr->IsGameObjectStaticTransport(entry) ? new StaticTransport() : new GameObject();
        ObjectGuid::LowType low = map->GenerateLowGuid<HighGuid::GameObject>();

        if (!g->Create(low, entry, map, phaseId, x, y, z, o, G3D::Quat(r0, r1, r2, r3), 0, GO_STATE_READY))
        {
            delete g;
            return false;
        }

        g->SetRespawnTime(spawntimesecs);
        g->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseId);
        uint32 spawnId = g->GetSpawnId();

        WorldDatabase.Execute(
            "UPDATE gameobject SET spawntimesecs = {} WHERE guid = {}",
            spawntimesecs, spawnId
        );

        g->CleanupsBeforeDelete();
        delete g;

        g = sObjectMgr->IsGameObjectStaticTransport(entry) ? new StaticTransport() : new GameObject();
        if (!g->LoadGameObjectFromDB(spawnId, map, true))
        {
            delete g;
            return true; // DB je ok, jen se live nepodařilo načíst
        }

        sObjectMgr->AddGameobjectToGrid(spawnId, sObjectMgr->GetGameObjectData(spawnId));
        return true;
    }

    // === Helper: Initialize village creatures based on purchased expansions ===
    static void CmdInitVillages(ChatHandler* handler, uint32 specificGuildId = 0)
    {
        std::vector<uint32> guildIds;

        if (specificGuildId > 0)
        {
            if (!GuildVillage::GuildHasVillage(specificGuildId))
            {
                handler->SendSysMessage(T("|cffffaa00[GV-INIT]|r Tato guilda nemá vesnici.",
                                          "|cffffaa00[GV-INIT]|r This guild does not have a village."));
                return;
            }
            guildIds.push_back(specificGuildId);
        }
        else
        {
            if (QueryResult rg = WorldDatabase.Query("SELECT guild FROM customs.gv_guild ORDER BY guild"))
                do { guildIds.emplace_back(rg->Fetch()[0].Get<uint32>()); } while (rg->NextRow());
        }

        if (guildIds.empty())
        {
            handler->SendSysMessage(T("|cffffaa00[GV-INIT]|r Není vytvořena žádná vesnice.",
                                      "|cffffaa00[GV-INIT]|r No villages exist."));
            return;
        }

        uint32 totalCreaturesAdded = 0;
        uint32 totalGameObjectsAdded = 0;
        uint32 totalProcessed = 0;

        for (uint32 guildId : guildIds)
        {
            uint32 phaseId = 0;
            if (QueryResult rp = WorldDatabase.Query(
                    "SELECT phase FROM customs.gv_guild WHERE guild={}", guildId))
            {
                phaseId = (*rp)[0].Get<uint32>();
            }

            if (!phaseId)
                continue;

            uint8 guildFaction = GetGuildFaction(guildId);
            uint32 guildCreaturesAdded = 0;
            uint32 guildGameObjectsAdded = 0;

            if (QueryResult rexp = WorldDatabase.Query(
                    "SELECT expansion_key FROM customs.gv_upgrades WHERE guildId={}", guildId))
            {
                do
                {
                    std::string expansionKey = rexp->Fetch()[0].Get<std::string>();

                    QueryResult rcreats = WorldDatabase.Query(
                        "SELECT entry, map, position_x, position_y, position_z, orientation, "
                        "       spawntimesecs, spawndist, movementtype "
                        "FROM customs.gv_expansion_creatures "
                        "WHERE expansion_key='{}' AND (faction=0 OR faction={})",
                        expansionKey, (uint32)guildFaction);

                    if (rcreats)
                    {
                        do
                        {
                            Field* f = rcreats->Fetch();
                            uint32 entry      = f[0].Get<uint32>();
                            uint32 mapId      = f[1].Get<uint32>();
                            float  x          = f[2].Get<float>();
                            float  y          = f[3].Get<float>();
                            float  z          = f[4].Get<float>();
                            float  o          = f[5].Get<float>();
                            uint32 respawnSec = f[6].Get<uint32>();
                            float  wanderDist = f[7].Get<float>();
                            uint8  moveType   = f[8].Get<uint8>();

                            if (CreatureSpawnExistsAtPosition(mapId, phaseId, entry, x, y, z))
                                continue;

                            if (SpawnExpansionCreatureLive(
                                    mapId, phaseId, entry, x, y, z, o,
                                    respawnSec, wanderDist, moveType))
                            {
                                ++guildCreaturesAdded;
                                ++totalCreaturesAdded;
                            }
                        }
                        while (rcreats->NextRow());
                    }

                    QueryResult rgos = WorldDatabase.Query(
                        "SELECT entry, map, position_x, position_y, position_z, orientation, "
                        "       rotation0, rotation1, rotation2, rotation3, spawntimesecs "
                        "FROM customs.gv_expansion_gameobjects "
                        "WHERE expansion_key='{}' AND (faction=0 OR faction={})",
                        expansionKey, (uint32)guildFaction);

                    if (rgos)
                    {
                        do
                        {
                            Field* f = rgos->Fetch();
                            uint32 entry = f[0].Get<uint32>();
                            uint32 mapId = f[1].Get<uint32>();
                            float x = f[2].Get<float>();
                            float y = f[3].Get<float>();
                            float z = f[4].Get<float>();
                            float o = f[5].Get<float>();
                            float r0 = f[6].Get<float>();
                            float r1 = f[7].Get<float>();
                            float r2 = f[8].Get<float>();
                            float r3 = f[9].Get<float>();
                            int32 spawntimesecs = f[10].Get<int32>();

                            if (GameObjectSpawnExistsAtPosition(mapId, phaseId, entry, x, y, z))
                                continue;

                            if (SpawnExpansionGameObjectLive(
                                    mapId, phaseId, entry,
                                    x, y, z, o,
                                    r0, r1, r2, r3,
                                    spawntimesecs))
                            {
                                ++guildGameObjectsAdded;
                                ++totalGameObjectsAdded;
                            }
                        }
                        while (rgos->NextRow());
                    }
                }
                while (rexp->NextRow());
            }

            ++totalProcessed;
            if (guildCreaturesAdded > 0 || guildGameObjectsAdded > 0)
            {
                handler->SendSysMessage(Acore::StringFormat(
                    "|cff00ff00[GV-INIT]|r Guild {}: {} creatures, {} gameobjects added.",
                    guildId, guildCreaturesAdded, guildGameObjectsAdded).c_str());
            }
            else
            {
                handler->SendSysMessage(Acore::StringFormat(
                    "|cff00ff00[GV-INIT]|r Guild {}: no new creatures/gameobjects needed.",
                    guildId).c_str());
            }
        }

        if (specificGuildId > 0)
        {
            handler->SendSysMessage(Acore::StringFormat(
                "|cff00ff00[GV-INIT]|r Initialization complete: {} creatures, {} gameobjects added.",
                totalCreaturesAdded, totalGameObjectsAdded).c_str());
        }
        else
        {
            handler->SendSysMessage(Acore::StringFormat(
                "|cff00ff00[GV-INIT]|r All villages initialized: {} guilds processed, {} creatures, {} gameobjects added.",
                totalProcessed, totalCreaturesAdded, totalGameObjectsAdded).c_str());
        }

        LOG_INFO("modules", "GV: Init command executed: {} guilds, {} creatures added, {} gameobjects added",
                 totalProcessed, totalCreaturesAdded, totalGameObjectsAdded);
    }

    // === Hlavní GM handler ===
    static bool HandleGv(ChatHandler* handler, char const* args)
    {
        WorldSession* sess = handler->GetSession();
        if (!sess)
            return true;

        if (sess->GetSecurity() < SEC_ADMINISTRATOR)
        {
            handler->SendSysMessage(T("Pouze GM (3+) mohou použít tento příkaz.",
                                      "Only GMs (3+) can use this command."));
            return true;
        }

        Player* plr = sess->GetPlayer();
        if (!plr)
            return true;

        std::string a = args ? Trim(args) : std::string();
        if (a.empty())
        {
            handler->SendSysMessage(T(
                R"(|cffffd000[GV]|r Dostupné příkazy:
  |cff00ff00.gv create [GUILDID] [ignorecap]|r      vytvoří vesnici pro guildy
  |cff00ff00.gv delete <GUILDID>|r      kompletní odstranění vesnice
  |cff00ff00.gv init [GUILDID]|r      inicializuje creatures pro koupeného rozsírení
  |cff00ff00.gv reset <GUILDID>|r      wipe + reinstall base layout
  |cff00ff00.gv list [PAGE]|r      vypíše 10 vesnic na stránku
  |cff00ff00.gv set <GUILDID> <material3> <50>|r      upraví množství materiálu
  |cff00ff00.gv teleport <GUILDID>|r      portne tě do vesnice dané guildy (alias: .gv tp)
  |cff00ff00.gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]|r
  |cff00ff00.gv object <ENTRY> [SPAWNTIMESECS]|r
  |cff00ff00.gv excreature <EXPKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]|r
  |cff00ff00.gv exobject <EXPKEY> <ENTRY> <FACTION> [SPAWNTIMESECS]|r)",
                R"(|cffffd000[GV]|r Available commands:
  |cff00ff00.gv create [GUILDID] [ignorecap]|r      create village
  |cff00ff00.gv delete <GUILDID>|r      remove village completely
  |cff00ff00.gv init [GUILDID]|r      initialize creatures for purchased expansions
  |cff00ff00.gv reset <GUILDID>|r      wipe + reinstall base layout
  |cff00ff00.gv list [PAGE]|r      list 10 villages per page
  |cff00ff00.gv set <GUILDID> <material3> <50>|r      modify material amount
  |cff00ff00.gv teleport <GUILDID>|r      teleport you to that guild's village (alias: .gv tp)
  |cff00ff00.gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]|r
  |cff00ff00.gv object <ENTRY> [SPAWNTIMESECS]|r
  |cff00ff00.gv excreature <EXPKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]|r
  |cff00ff00.gv exobject <EXPKEY> <ENTRY> <FACTION> [SPAWNTIMESECS]|r)"));
            return true;
        }

        size_t sp = a.find(' ');
        std::string cmd = sp == std::string::npos ? a : a.substr(0, sp);
        std::string rest = sp == std::string::npos ? "" : Trim(a.substr(sp + 1));
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c){ return std::tolower(c); });

        // ===== .gv list [PAGE] =====
        if (cmd == "list")
        {
            uint32 page = 1;
            if (!rest.empty())
            {
                std::stringstream ss(rest);
                ss >> page;
                if (page == 0) page = 1;
            }
            CmdListVillages(handler, page);
            return true;
        }

        // ===== .gv create [GUILDID] [ignorecap] =====
        if (cmd == "create")
        {
            uint32 guildId = 0;
            uint32 ignorecap = 0;

            if (!rest.empty())
            {
                std::stringstream ss(rest);
                ss >> guildId >> ignorecap;
            }

            if (guildId == 0)
            {
                if (Guild* gg = sess->GetPlayer()->GetGuild())
                    guildId = gg->GetId();
                else
                {
                    handler->SendSysMessage(T("|cffff5555[GV-GM]|r Nejsi v guildě a GUILDID nebylo zadáno.",
                                              "|cffff5555[GV-GM]|r You are not in a guild and GUILDID was not provided."));
                    return true;
                }
            }

            if (GuildVillage::GuildHasVillage(guildId))
            {
                handler->SendSysMessage(T("|cffffaa00[GV-GM]|r Tato guilda už vesnici má.",
                                          "|cffffaa00[GV-GM]|r This guild already has a village."));
                return true;
            }

            bool ok = GuildVillage::CreateVillageForGuild_GM(guildId, ignorecap != 0);
            handler->SendSysMessage(ok ?
                T("|cff00ff00[GV-GM]|r Vesnice vytvořena.", "|cff00ff00[GV-GM]|r Village created.") :
                T("|cffff5555[GV-GM]|r Vytvoření selhalo.", "|cffff5555[GV-GM]|r Creation failed."));
            return true;
        }

        // ===== .gv delete <GUILDID> =====
        if (cmd == "delete")
        {
            if (rest.empty())
            {
                handler->SendSysMessage(T("Použití: .gv delete <GUILDID>", "Usage: .gv delete <GUILDID>"));
                return true;
            }

            uint32 guildId = std::stoul(rest);

            // vytáhneme phaseId (kvůli despawnu ze světa a respawn tabulkám)
            uint32 phaseId = 0;
            if (QueryResult pr = WorldDatabase.Query(
                    "SELECT phase FROM customs.gv_guild WHERE guild = {} LIMIT 1", guildId))
            {
                phaseId = (*pr)[0].Get<uint32>();
            }

            if (!GuildVillage::GuildHasVillage(guildId))
            {
                handler->SendSysMessage(T("|cffffaa00[GV-GM]|r Tato guilda nemá vesnici.",
                                          "|cffffaa00[GV-GM]|r This guild does not have a village."));
                return true;
            }

            //
            // 0) Lokální pre-clean ve všech customs tabulkách vázaných na guildId
            //    (tohle je duplicitní ochrana + čitelnost; CleanupVillageForGuild to stejně smaže taky)
            //
            WorldDatabase.Execute(
                "DELETE FROM customs.gv_currency WHERE guildId={}",
                guildId
            );

            WorldDatabase.Execute(
                "DELETE FROM customs.gv_upgrades WHERE guildId={}",
                guildId
            );

            WorldDatabase.Execute(
                "DELETE FROM customs.gv_production_active WHERE guildId={}",
                guildId
            );

            WorldDatabase.Execute(
                "DELETE FROM customs.gv_production_upgrade WHERE guildId={}",
                guildId
            );

            // expedice
            WorldDatabase.Execute(
                "DELETE FROM customs.gv_expedition_active WHERE guildId={}",
                guildId
            );

            WorldDatabase.Execute(
                "DELETE FROM customs.gv_expedition_loot WHERE guildId={}",
                guildId
            );
			
			WorldDatabase.Execute(
                "DELETE FROM customs.gv_guild_quests WHERE guildId={}",
                guildId
            );

            WorldDatabase.Execute(
                "DELETE FROM customs.gv_expedition_guild WHERE guildId={}",
                guildId
            );
			
			// teleportační bod
			WorldDatabase.Execute(
				"DELETE FROM customs.gv_teleport_player WHERE guild={}",
				guildId
			);

            //
            // 1) Respawn tabulky v characters DB - aby tam nezůstaly sirotčí cooldowny a dead body
            //
            if (phaseId)
            {
                // posbírat GUIDy z world.creature (mapa vesnice + phase)
                std::vector<uint32> creatureGuids;
                if (QueryResult qc = WorldDatabase.Query(
                        "SELECT guid FROM creature WHERE map={} AND phaseMask={}", DefMap(), phaseId))
                {
                    do { creatureGuids.emplace_back(qc->Fetch()[0].Get<uint32>()); }
                    while (qc->NextRow());
                }

                // posbírat GUIDy z world.gameobject (mapa vesnice + phase)
                std::vector<uint32> goGuids;
                if (QueryResult qg = WorldDatabase.Query(
                        "SELECT guid FROM gameobject WHERE map={} AND phaseMask={}", DefMap(), phaseId))
                {
                    do { goGuids.emplace_back(qg->Fetch()[0].Get<uint32>()); }
                    while (qg->NextRow());
                }

                // smazat odpovídající respawny
                DeleteRespawnsByGuids("creature_respawn", creatureGuids);
                DeleteRespawnsByGuids("gameobject_respawn", goGuids);

                LOG_INFO("modules",
                         "GV: GM delete cleared respawns (guild={}, phaseId={}, creatures={}, gos={})",
                         guildId, phaseId, creatureGuids.size(), goGuids.size());
            }

            //
            // 2) Instantní despawn ze světa (aby objekty zmizely hráčům bez restartu)
            //
            if (phaseId)
                DespawnPhaseObjects(DefMap(), phaseId);

            //
            // 3) A nakonec nechat doběhnout oficiální mazání vesnice
            //    (tohle volá CleanupVillageForGuild uvnitř GuildVillage::DeleteVillageForGuild_GM)
            //
            bool ok = GuildVillage::DeleteVillageForGuild_GM(guildId);

            handler->SendSysMessage(ok ?
                T("|cff00ff00[GV-GM]|r Vesnice odstraněna (DB + despawn).",
                  "|cff00ff00[GV-GM]|r Village removed (DB + despawn).") :
                T("|cffff5555[GV-GM]|r Odstranění selhalo.",
                  "|cffff5555[GV-GM]|r Delete failed."));
            return true;
        }
	
	        // ===== .gv set <GUILDID> <material1|material2|material3|material4> <delta> =====
        if (cmd == "set")
        {
            if (rest.empty())
            {
                handler->SendSysMessage(T(
                    "Použití: .gv set <GUILDID> <material1|material2|material3|material4> <delta>",
                    "Usage: .gv set <GUILDID> <material1|material2|material3|material4> <delta>"));
                return true;
            }

            std::stringstream ss(rest);
            uint32 guildId = 0;
            std::string matToken;
            long long delta = 0;

            if (!(ss >> guildId >> matToken >> delta))
            {
                handler->SendSysMessage(T(
                    "Špatné parametry. Použití: .gv set <GUILDID> <material1|material2|material3|material4> <delta>",
                    "Bad parameters. Usage: .gv set <GUILDID> <material1|material2|material3|material4> <delta>"));
                return true;
            }

            // znormalizovat název materiálu
            std::transform(matToken.begin(), matToken.end(), matToken.begin(),
                           [](unsigned char c){ return std::tolower(c); });

            std::string colName;
            uint8 matIndex = 0;

            if (matToken == "material1" || matToken == "mat1" || matToken == "m1")
            {
                colName = "material1"; matIndex = 1;
            }
            else if (matToken == "material2" || matToken == "mat2" || matToken == "m2")
            {
                colName = "material2"; matIndex = 2;
            }
            else if (matToken == "material3" || matToken == "mat3" || matToken == "m3")
            {
                colName = "material3"; matIndex = 3;
            }
            else if (matToken == "material4" || matToken == "mat4" || matToken == "m4")
            {
                colName = "material4"; matIndex = 4;
            }
            else
            {
                handler->SendSysMessage(T(
                    "Neznámý materiál. Použij material1, material2, material3 nebo material4.",
                    "Unknown material. Use material1, material2, material3 or material4."));
                return true;
            }

            // volitelně: jen pro guildy, co mají vesnici
            if (!GuildVillage::GuildHasVillage(guildId))
            {
                handler->SendSysMessage(T(
                    "|cffffaa00[GV-GM]|r Tato guilda nemá vesnici (není co upravovat).",
                    "|cffffaa00[GV-GM]|r This guild does not have a village (nothing to modify)."));
                return true;
            }

            // načíst aktuální currency pro guildu (pokud existuje)
            uint64 cur1 = 0, cur2 = 0, cur3 = 0, cur4 = 0;
            bool haveRow = false;

            if (QueryResult r = WorldDatabase.Query(
                    "SELECT material1, material2, material3, material4 "
                    "FROM customs.gv_currency WHERE guildId={}", guildId))
            {
                Field* f = r->Fetch();
                cur1 = f[0].Get<uint64>();
                cur2 = f[1].Get<uint64>();
                cur3 = f[2].Get<uint64>();
                cur4 = f[3].Get<uint64>();
                haveRow = true;
            }

            uint64* target = nullptr;
            switch (matIndex)
            {
                case 1: target = &cur1; break;
                case 2: target = &cur2; break;
                case 3: target = &cur3; break;
                case 4: target = &cur4; break;
                default: break;
            }

            if (!target)
            {
                handler->SendSysMessage(T(
                    "Vnitřní chyba: target == nullptr.",
                    "Internal error: target == nullptr."));
                return true;
            }

            uint64 oldVal = *target;

            // bezpečný výpočet: old + delta, clamp na [0, UINT64_MAX]
            long double tmp = static_cast<long double>(oldVal) + static_cast<long double>(delta);
            if (tmp < 0.0L)
                tmp = 0.0L;
            if (tmp > static_cast<long double>(std::numeric_limits<uint64>::max()))
                tmp = static_cast<long double>(std::numeric_limits<uint64>::max());

            uint64 newVal = static_cast<uint64>(tmp);
            *target = newVal;

            if (haveRow)
            {
                WorldDatabase.Execute(
                    "UPDATE customs.gv_currency "
                    "SET material1={}, material2={}, material3={}, material4={} "
                    "WHERE guildId={}",
                    cur1, cur2, cur3, cur4, guildId);
            }
            else
            {
                WorldDatabase.Execute(
                    "INSERT INTO customs.gv_currency "
                    "(guildId, material1, material2, material3, material4) "
                    "VALUES ({}, {}, {}, {}, {})",
                    guildId, cur1, cur2, cur3, cur4);
            }

            // feedback
            if (LangOpt() == Lang::EN)
            {
                handler->SendSysMessage(Acore::StringFormat(
                    "|cff00ff00[GV-GM]|r Guild {}: {} changed from {} to {} ({:+d}).",
                    guildId, colName, oldVal, newVal, (int32)delta).c_str());
            }
            else
            {
                handler->SendSysMessage(Acore::StringFormat(
                    "|cff00ff00[GV-GM]|r Guilda {}: {} změněn z {} na {} ({:+d}).",
                    guildId, colName, oldVal, newVal, (int32)delta).c_str());
            }

            return true;
        }

        // ===== .gv init [GUILDID] =====
        if (cmd == "init")
        {
            if (!rest.empty())
            {
                // Konkrétní guilda
                try
                {
                    uint32 guildId = std::stoul(rest);
                    CmdInitVillages(handler, guildId);
                }
                catch (...)
                {
                    handler->SendSysMessage(T("Špatný formát GUILDID.", "Bad GUILDID format."));
                }
            }
            else
            {
                // Všechny gildy
                CmdInitVillages(handler, 0);
            }
            return true;
        }
	
        // === Default help ===
        handler->SendSysMessage(T(
            R"(|cffffd000[GV]|r Dostupné příkazy:
  |cff00ff00.gv create [GUILDID] [ignorecap]|r
  |cff00ff00.gv delete <GUILDID>|r
  |cff00ff00.gv init [GUILDID]|r
  |cff00ff00.gv reset <GUILDID>|r
  |cff00ff00.gv list [PAGE]|r
  |cff00ff00.gv set <GUILDID> <material3> <50>|r
  |cff00ff00.gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]|r
  |cff00ff00.gv object <ENTRY> [SPAWNTIMESECS]|r
  |cff00ff00.gv excreature <EXPKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNDIMESECS]|r
  |cff00ff00.gv exobject <EXPKEY> <ENTRY> <FACTION> [SPAWNTIMESECS]|r)",
            R"(|cffffd000[GV]|r Available commands:
  |cff00ff00.gv create [GUILDID] [ignorecap]|r
  |cff00ff00.gv delete <GUILDID>|r
  |cff00ff00.gv init [GUILDID]|r
  |cff00ff00.gv reset <GUILDID>|r
  |cff00ff00.gv list [PAGE]|r
  |cff00ff00.gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]|r
  |cff00ff00.gv object <ENTRY> [SPAWNTIMESECS]|r
  |cff00ff00.gv excreature <EXPKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNDIMESECS]|r
  |cff00ff00.gv exobject <EXPKEY> <ENTRY> <FACTION> [SPAWNTIMESECS]|r)"));
        return true;
    }

    class GuildVillageGM_Command : public CommandScript
    {
    public:
        GuildVillageGM_Command() : CommandScript("GuildVillageGM_Command") { }

        std::vector<Acore::ChatCommands::ChatCommandBuilder> GetCommands() const override
        {
            using namespace Acore::ChatCommands;
            auto& fn = HandleGv;
            ChatCommandBuilder gv("gv", fn, SEC_ADMINISTRATOR, Console::No);
            return { gv };
        }
    };
}

// Registrace
void RegisterGuildVillageGM()
{
    new GuildVillageGM_Command();
}
