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

#include <string>
#include <optional>
#include <algorithm>
#include <unordered_set>

namespace GuildVillage
{
    // ===================== KONFIG =====================
    static inline uint32 PriceGold()      { return sConfigMgr->GetOption<uint32>("GuildVillage.Price.Gold", 0); } // gold v G
    static inline uint32 PriceItemId()    { return sConfigMgr->GetOption<uint32>("GuildVillage.Price.ItemId", 0); }
    static inline uint32 PriceItemCount() { return sConfigMgr->GetOption<uint32>("GuildVillage.Price.ItemCount", 0); }

    static inline uint32 DefMap()   { return sConfigMgr->GetOption<uint32>("GuildVillage.Default.Map", 37); }
    static inline float  DefX()     { return sConfigMgr->GetOption<float>("GuildVillage.Default.X", 1026.7292f); }
    static inline float  DefY()     { return sConfigMgr->GetOption<float>("GuildVillage.Default.Y", 289.9494f); }
    static inline float  DefZ()     { return sConfigMgr->GetOption<float>("GuildVillage.Default.Z", 332.66083f); }
    static inline float  DefO()     { return sConfigMgr->GetOption<float>("GuildVillage.Default.O", 3.4305837f); }
    static inline uint32 MaxVillages()    { return sConfigMgr->GetOption<uint32>("GuildVillage.MaxVillages", 30); }
    static inline uint32 CleanupDays()    { return sConfigMgr->GetOption<uint32>("GuildVillage.Inactivity.CleanupDays", 90); } // 0 = vypnuto
    static inline bool   ShowCapacityMsg(){ return sConfigMgr->GetOption<bool>("GuildVillage.ShowCapacityMessage", true); }

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

    // Najde první volný bit v rozsahu 2..31 podle obsahu customs.gv_guild
    static uint32 AllocatePhaseMask()
    {
        std::unordered_set<uint32> used;
        if (QueryResult r = WorldDatabase.Query("SELECT phase FROM customs.gv_guild"))
        {
            do { used.insert(r->Fetch()[0].Get<uint32>()); } while (r->NextRow());
        }

        for (uint32 bit = 2; bit <= 31; ++bit)
        {
            uint32 mask = (1u << bit);
            if (!used.count(mask))
                return mask;
        }
        return 0;
    }

    static inline uint32 CalcGuildPhaseMask(uint32 guildId)
    {
        uint32 bitIndex = (guildId % 30) + 1; // => 2..31
        return (1u << bitIndex);
    }

    // Aktuální počet vesnic (obsazených slotů)
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
            "(guildId, timber, stone, iron, crystal, last_update) "
            "VALUES ({}, 0, 0, 0, 0, NOW())",
            guildId
        );
    }

    // ===================== LIVE INSTALL: BASE LAYOUT & UPGRADES =====================
    static void InstallBaseLayout(uint32 /*guildId*/, uint32 phaseMask, std::string const& layout_key = "base")
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
                uint32 entry        = f[0].Get<uint32>();
                uint32 mapId        = f[1].Get<uint32>();
                float  x            = f[2].Get<float>();
                float  y            = f[3].Get<float>();
                float  z            = f[4].Get<float>();
                float  o            = f[5].Get<float>();
                uint32 respawnSecs  = f[6].Get<uint32>();
                float  wander       = f[7].Get<float>();
                uint8  moveType     = f[8].Get<uint8>();

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
                        entry, mapId, phaseMask, x, y, z, o, respawnSecs, wander, (uint32)moveType
                    );
                    ++cCount;
                    continue;
                }

                Creature* c = new Creature();
                ObjectGuid::LowType low = map->GenerateLowGuid<HighGuid::Unit>();
                if (!c->Create(low, map, phaseMask, entry, 0, x, y, z, o))
                { delete c; continue; }

                c->SetRespawnTime(respawnSecs);
                c->SetWanderDistance(wander);
                c->SetDefaultMovementType(MovementGeneratorType(moveType));

                c->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseMask);
                uint32 spawnId = c->GetSpawnId();

                c->CleanupsBeforeDelete(); delete c;

                c = new Creature();
                if (!c->LoadCreatureFromDB(spawnId, map, /*addToMap=*/true)) { delete c; continue; }
                sObjectMgr->AddCreatureToGrid(spawnId, sObjectMgr->GetCreatureData(spawnId));
                c->SetRespawnDelay(respawnSecs);
                map->AddToMap(c);

                WorldDatabase.Execute(
                    "UPDATE creature "
                    "SET spawntimesecs = {}, wander_distance = {}, MovementType = {}, Comment = 'Village mob' "
                    "WHERE guid = {}",
                    respawnSecs, wander, (uint32)moveType, spawnId
                );

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
                uint32 entry       = f[0].Get<uint32>();
                uint32 mapId       = f[1].Get<uint32>();
                float  x           = f[2].Get<float>();
                float  y           = f[3].Get<float>();
                float  z           = f[4].Get<float>();
                float  o           = f[5].Get<float>();
                float  r0          = f[6].Get<float>();
                float  r1          = f[7].Get<float>();
                float  r2          = f[8].Get<float>();
                float  r3          = f[9].Get<float>();
                int32  spawntime   = f[10].Get<int32>();

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

                    if (!g->Create(low, entry, map, phaseMask, x, y, z, o, G3D::Quat(r0, r1, r2, r3), 0, GO_STATE_READY))
                    { delete g; continue; }

                    g->SetRespawnTime(spawntime);
                    g->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseMask);
                    uint32 spawnId = g->GetSpawnId();

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
                        "(id, map, spawnMask, phaseMask, position_x, position_y, position_z, orientation, rotation0, rotation1, rotation2, rotation3, spawntimesecs) "
                        "VALUES ({}, {}, 1, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
                        entry, mapId, phaseMask, x, y, z, o, r0, r1, r2, r3, spawntime
                    );
                    ++goCount;
                }
            }
            while (go->NextRow());
        }

        LOG_INFO("modules", "GV: Installed base layout '{}' -> creatures={}, gameobjects={}, phaseMask={}",
                 layout_key, cCount, goCount, phaseMask);
    }

    static bool InstallExpansionForGuild(uint32 guildId, uint32 phaseMask, std::string const& key)
    {
        if (QueryResult q = WorldDatabase.Query(
                "SELECT 1 FROM customs.gv_upgrades WHERE guildId={} AND expansion_key='{}'",
                guildId, key))
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
                        entry, mapId, phaseMask, x, y, z, o, resp, wander, (uint32)mt
                    );
                    continue;
                }

                Creature* c = new Creature();
                ObjectGuid::LowType low = map->GenerateLowGuid<HighGuid::Unit>();
                if (!c->Create(low, map, phaseMask, entry, 0, x, y, z, o))
                { delete c; continue; }
                c->SetRespawnTime(resp); c->SetWanderDistance(wander); c->SetDefaultMovementType(MovementGeneratorType(mt));
                c->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseMask);
                uint32 spawnId = c->GetSpawnId();
                c->CleanupsBeforeDelete(); delete c;
                c = new Creature(); if (!c->LoadCreatureFromDB(spawnId, map, /*addToMap=*/true)) { delete c; continue; }
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
                        entry, mapId, phaseMask, x, y, z, o, r0, r1, r2, r3, st
                    );
                    continue;
                }

                GameObject* g = sObjectMgr->IsGameObjectStaticTransport(entry) ? new StaticTransport() : new GameObject();
                ObjectGuid::LowType low = map->GenerateLowGuid<HighGuid::GameObject>();
                if (!g->Create(low, entry, map, phaseMask, x, y, z, o, G3D::Quat(r0,r1,r2,r3), 0, GO_STATE_READY))
                { delete g; continue; }
                g->SetRespawnTime(st); g->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseMask);
                uint32 spawnId = g->GetSpawnId();
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

    // ===================== TELEPORT =====================
    static void TeleportToVillage(Player* player, VillageRow const& v)
    {
        player->SetPhaseMask(v.phase, true);
        player->TeleportTo(v.map, v.x, v.y, v.z, v.o);
        player->SetPhaseMask(v.phase, true);
    }

    // ===== Helper: kompletní wipe (stejně jako disband/inaktivita) =====
    static void CleanupVillageForGuild(uint32 guildId)
    {
        // Fáze z DB (preferovaná), jinak fallback výpočet
        uint32 phaseMask = 0;
        if (QueryResult r = WorldDatabase.Query("SELECT phase FROM customs.gv_guild WHERE guild={}", guildId))
            phaseMask = (*r)[0].Get<uint32>();
        if (!phaseMask)
            phaseMask = CalcGuildPhaseMask(guildId);

        // customs
        WorldDatabase.Execute("DELETE FROM customs.gv_currency WHERE guildId={}", guildId);
        WorldDatabase.Execute("DELETE FROM customs.gv_upgrades WHERE guildId={}", guildId);

        // spawny na mapě vesnice
        WorldDatabase.Execute("DELETE FROM creature  WHERE map={} AND phaseMask={}", DefMap(), phaseMask);
        WorldDatabase.Execute("DELETE FROM gameobject WHERE map={} AND phaseMask={}", DefMap(), phaseMask);

        // gv_guild nakonec
        WorldDatabase.Execute("DELETE FROM customs.gv_guild WHERE guild={}", guildId);

        LOG_INFO("modules", "GV: Cleanup (wipe) done for guild {} (phaseMask={})", guildId, phaseMask);
    }

    // ===== PUBLIC API pro GM – forwardy pro gm.cpp =====
    bool GuildHasVillage(uint32 guildId)
    {
        return LoadVillage(guildId).has_value();
    }

    bool CreateVillageForGuild_GM(uint32 guildId, bool ignoreCapacity)
    {
        if (GuildHasVillage(guildId))
            return false;

        if (!ignoreCapacity)
        {
            uint32 used = CountVillages();
            if (used >= MaxVillages())
                return false;
        }

        uint32 phaseMask = AllocatePhaseMask();
        if (phaseMask == 0)
            return false;

        WorldDatabase.Execute(
            "INSERT INTO customs.gv_guild "
            "(guild, phase, map, positionx, positiony, positionz, orientation, last_update) VALUES "
            "({}, {}, {}, {}, {}, {}, {}, NOW())",
            guildId, phaseMask, DefMap(), DefX(), DefY(), DefZ(), DefO()
        );

        EnsureCurrencyRow(guildId);
        InstallBaseLayout(guildId, phaseMask, "base");
        return true;
    }

    bool DeleteVillageForGuild_GM(uint32 guildId)
    {
        if (!GuildHasVillage(guildId))
            return false;
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

        // 1) gilda už vesnici má?
        if (LoadVillage(guildId).has_value())
        {
            ch.SendSysMessage(T("Tvoje gilda už vesnici vlastní.", "Your guild already owns a village."));
            return false;
        }

        // 2) limit
        uint32 used = CountVillages();
        uint32 maxv = MaxVillages();
        if (used >= maxv)
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                Acore::StringFormat("{}{} / {}. {}",
                    T("Kapacita vesnic je plná: ", "Village capacity is full: "),
                    used, maxv,
                    T("Počkej na uvolnění slotu (rozpuštění/neaktivita).",
                      "Please wait for a slot to free up (disband/inactivity).")
                ).c_str()
            );
            return false;
        }

        // 3) cena
        uint64 needCopper = static_cast<uint64>(PriceGold()) * 10000ULL;
        if (PriceGold() > 0 && player->GetMoney() < needCopper)
        {
            std::string msg = Acore::StringFormat("{}{}{}",
                T("Nemáš dost zlata (potřeba ", "You don't have enough gold (need "),
                PriceGold(), T(" g).", " g)."));
            ch.SendSysMessage(msg.c_str());
            return false;
        }
        if (PriceItemId() > 0 && PriceItemCount() > 0 &&
            player->GetItemCount(PriceItemId(), true) < PriceItemCount())
        {
            std::string msg = Acore::StringFormat("{}{} × {}",
                T("Chybí požadované itemy: ID ", "Missing required items: ID "),
                PriceItemId(), PriceItemCount());
            ch.SendSysMessage(msg.c_str());
            return false;
        }

        // 4) zaplať
        if (needCopper > 0) player->ModifyMoney(-static_cast<int64>(needCopper));
        if (PriceItemId() > 0 && PriceItemCount() > 0) player->DestroyItemCount(PriceItemId(), PriceItemCount(), true);

        // 5) alokace fáze
        uint32 phaseMask = AllocatePhaseMask();
        if (phaseMask == 0)
        {
            LOG_WARN("modules", "GV: AllocatePhaseMask() returned 0 (no free bit) during purchase for guild {}", guildId);
            return false;
        }

        WorldDatabase.Execute(
            "INSERT INTO customs.gv_guild "
            "(guild, phase, map, positionx, positiony, positionz, orientation, last_update) VALUES "
            "({}, {}, {}, {}, {}, {}, {}, NOW())",
            guildId, phaseMask, DefMap(), DefX(), DefY(), DefZ(), DefO()
        );

        EnsureCurrencyRow(guildId);

        // 6) layout
        InstallBaseLayout(guildId, phaseMask, "base");

        // 7) jen úspěch (bez obsazenosti)
        ch.SendSysMessage(T("Gratuluji! Tvoje gilda zakoupila guildovní vesnici.",
                            "Congratulations! Your guild has purchased a village."));

        return true;
    }

    // ===================== GOSSIP: SELLER =====================
    static void ShowMain(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        // Auto-úklid neaktivních gild (pokud povolen v .conf)
        uint32 days = CleanupDays();
        if (days)
        {
            if (QueryResult r = CharacterDatabase.Query(
                    "SELECT g.guildid "
                    "FROM guild AS g "
                    "JOIN customs.gv_guild AS v ON v.guild = g.guildid "
                    "LEFT JOIN characters AS c ON c.guid = g.leaderGuid "
                    "WHERE (c.logout_time IS NULL OR c.logout_time = 0 OR c.logout_time < UNIX_TIMESTAMP() - {}*24*3600)",
                    days))
            {
                do
                {
                    uint32 gid = r->Fetch()[0].Get<uint32>();
                    // lokální wipe (stejná logika jako při disband)
                    CleanupVillageForGuild(gid);
                } while (r->NextRow());
            }
        }

        Guild* g = player->GetGuild();
        if (!g)
        {
            if (ShowCapacityMsg())
            {
                uint32 used = CountVillages();
                uint32 maxv = MaxVillages();
                ChatHandler(player->GetSession()).SendSysMessage(
                    Acore::StringFormat("{}{} / {} {}",
                        T("Obsazeno vesnic: ", "Villages in use: "),
                        used, maxv,
                        T("(pokud je plno, uvolní se po rozpuštění/neaktivitě).",
                          "(if full, slots free up after disband/inactivity).")
                    ).c_str()
                );
            }

            ChatHandler(player->GetSession()).SendSysMessage(
                T("Nejsi v žádné gildě.", "You are not in a guild."));
            SendGossipMenuFor(player, 1, creature->GetGUID());
            return;
    }

        auto row = LoadVillage(g->GetId());

        // Obsazenost jen pro guildy bez vesnice
        if (!row.has_value() && ShowCapacityMsg())
        {
            uint32 used = CountVillages();
            uint32 maxv = MaxVillages();
            ChatHandler(player->GetSession()).SendSysMessage(
                Acore::StringFormat("{}{} / {} {}",
                    T("Obsazeno vesnic: ", "Villages in use: "),
                    used, maxv,
                    T("(pokud je plno, uvolní se po rozpuštění/neaktivitě).",
                      "(if full, slots free up after disband/inactivity).")
                ).c_str()
            );
        }

        if (row.has_value())
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                             T("Teleportovat do guild vesnice", "Teleport to guild village"),
                             GOSSIP_SENDER_MAIN, 1001);
        }
        else
        {
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR,
                             T("Zakoupit guild vesnici", "Purchase guild village"),
                             GOSSIP_SENDER_MAIN, 1002);
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
                    T("Nejsi v žádné gildě.", "You are not in a guild."));
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
                            T("Tvoje gilda vesnici nevlastní.", "Your guild does not own a village."));
                        CloseGossipMenuFor(player);
                        return true;
                    }
                    TeleportToVillage(player, *row);
                    CloseGossipMenuFor(player);
                    return true;
                }
                case 1002: // ACT_BUY
                {
                    ClearGossipMenuFor(player);

                    std::string priceInfo;
                    if (PriceGold() > 0)
                        priceInfo += Acore::StringFormat("{}{}{}", T("Cena: ", "Price: "), PriceGold(), T(" g", " g"));
                    if (PriceItemId() > 0 && PriceItemCount() > 0)
                    {
                        if (!priceInfo.empty()) priceInfo += " + ";
                        priceInfo += Acore::StringFormat("{}{} × {}", T("Item ", "Item "), PriceItemId(), PriceItemCount());
                    }
                    if (priceInfo.empty())
                        priceInfo = T("Cena: zdarma", "Price: free");

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
                        ChatHandler(player->GetSession()).SendSysMessage(
                            T("Nákup dokončen.", "Purchase complete."));

                    CloseGossipMenuFor(player);
                    return true;
                }
            }

            CloseGossipMenuFor(player);
            return true;
        }
    };

    // ===================== PHASING ENFORCER =====================
    class guild_village_Global : public GlobalScript
    {
    public:
        guild_village_Global() : GlobalScript("guild_village_Global") {}

        void OnBeforeWorldObjectSetPhaseMask(WorldObject const* worldObject,
                                             uint32& /*oldPhaseMask*/, uint32& newPhaseMask,
                                             bool& useCombinedPhases, bool& /*update*/) override
        {
            if (worldObject->GetMapId() != DefMap())
            {
                useCombinedPhases = true;
                return;
            }

            useCombinedPhases = false;

            if (Player const* p = worldObject->ToPlayer())
            {
                uint32 guildId = p->GetGuildId();
                if (!guildId) return;

                if (QueryResult r = WorldDatabase.Query(
                        "SELECT phase FROM customs.gv_guild WHERE guild={}", guildId))
                {
                    uint32 phaseMask = (*r)[0].Get<uint32>();
                    if (phaseMask != 0)
                        newPhaseMask = phaseMask;
                }
            }
        }
    };

    // ===================== PLAYER HOOK: udrž phase po login/teleport/změně zóny =====================
    class guild_village_Player : public PlayerScript
    {
    public:
        guild_village_Player() : PlayerScript("guild_village_Player") {}

        static uint32 GetGuildPhaseFor(Player* p)
        {
            uint32 gid = p->GetGuildId();
            if (!gid) return 0;

            if (QueryResult r = WorldDatabase.Query("SELECT phase FROM customs.gv_guild WHERE guild={}", gid))
                return (*r)[0].Get<uint32>();
            return 0;
        }

        static uint32 GetNormalPhase(Player* p)
        {
            if (p->IsGameMaster())
                return PHASEMASK_ANYWHERE;

            uint32 auras = p->GetPhaseByAuras();
            return auras ? auras : PHASEMASK_NORMAL;
        }

        static void ApplyProperPhase(Player* p)
        {
            if (p->GetMapId() == DefMap())
            {
                uint32 ph = GetGuildPhaseFor(p);
                if (ph != 0)
                {
                    p->SetPhaseMask(ph, true);
                    return;
                }
            }
            p->SetPhaseMask(GetNormalPhase(p), true);
        }

        void OnPlayerLogin(Player* player) override
        {
            ApplyProperPhase(player);
        }

        void OnPlayerMapChanged(Player* player) override
        {
            ApplyProperPhase(player);
        }

        void OnPlayerUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override
        {
            ApplyProperPhase(player);
        }
    };

} // namespace GuildVillage

// ===== REGISTRACE =====
void RegisterGuildVillageCreate()
{
    new GuildVillage::npc_guild_village_seller();
    new GuildVillage::guild_village_Global();
    new GuildVillage::guild_village_Player();
}
