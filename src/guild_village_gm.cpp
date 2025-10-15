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
#include "DataMap.h" // per-player stash

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
    // per-player stash stejného jména/typu jako jinde (.commands/.respawn…)
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
    // Pozn.: používáme "phaseId" = přesné ID fáze (žádná bitová maska).
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
                uint32 entry = f[0].Get<uint32>();
                uint32 mapId = f[1].Get<uint32>();
                float x = f[2].Get<float>();
                float y = f[3].Get<float>();
                float z = f[4].Get<float>();
                float o = f[5].Get<float>();
                uint32 respawnSecs = f[6].Get<uint32>();
                float wander = f[7].Get<float>();
                uint8 moveType = f[8].Get<uint8>();

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
                        entry, mapId, phaseId, x, y, z, o, respawnSecs, wander, (uint32)moveType
                    );
                    ++cCount;
                    continue;
                }

                Creature* c = new Creature();
                ObjectGuid::LowType low = map->GenerateLowGuid<HighGuid::Unit>();
                if (!c->Create(low, map, phaseId, entry, 0, x, y, z, o))
                { delete c; continue; }

                c->SetRespawnTime(respawnSecs);
                c->SetWanderDistance(wander);
                c->SetDefaultMovementType(MovementGeneratorType(moveType));

                c->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseId);
                uint32 spawnId = c->GetSpawnId();

                c->CleanupsBeforeDelete(); delete c;

                c = new Creature();
                if (!c->LoadCreatureFromDB(spawnId, map, /*addToMap=*/true)) { delete c; continue; }
                sObjectMgr->AddCreatureToGrid(spawnId, sObjectMgr->GetCreatureData(spawnId));
                c->SetRespawnDelay(respawnSecs);
                map->AddToMap(c);

                WorldDatabase.Execute(
                    "UPDATE creature SET spawntimesecs = {}, wander_distance = {}, MovementType = {}, Comment='Village mob' WHERE guid = {}",
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
                uint32 entry = f[0].Get<uint32>();
                uint32 mapId = f[1].Get<uint32>();
                float x = f[2].Get<float>();
                float y = f[3].Get<float>();
                float z = f[4].Get<float>();
                float o = f[5].Get<float>();
                int32 spawntime = f[10].Get<int32>();

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

                    g->SetRespawnTime(spawntime);
                    g->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseId);
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
                        "(id, map, spawnMask, phaseMask, position_x, position_y, position_z, orientation, "
                        " rotation0, rotation1, rotation2, rotation3, spawntimesecs) "
                        "VALUES ({}, {}, 1, {}, {}, {}, {}, {}, 0, 0, 0, 0, {})",
                        entry, mapId, phaseId, x, y, z, o, spawntime
                    );
                    ++goCount;
                }
            }
            while (go->NextRow());
        }

        LOG_INFO("modules", "GV: (GM reset) Installed base layout '{}' -> creatures={}, gameobjects={}, phaseId={}",
                 layout_key, cCount, goCount, phaseId);
    }

    // Pomocné: stránkovaný výpis z customs.gv_guild (jména dotáhneme z characters.guild)
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

        // vyzvedni guild ID a phase (limit/offset)
        std::vector<uint32> guildIds;
        if (QueryResult rl = WorldDatabase.Query(
                "SELECT guild FROM customs.gv_guild ORDER BY guild LIMIT {} OFFSET {}",
                kPageSize, offset))
        {
            do { guildIds.emplace_back(rl->Fetch()[0].Get<uint32>()); } while (rl->NextRow());
        }

        handler->SendSysMessage(Acore::StringFormat(T("Stránka {}/{}", "Page {}/{}"),
                                                    page, totalPages).c_str());

        // pro každé guildId načti jméno z characters.guild
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
  |cff00ff00.gv create [GUILDID] [ignorecap]|r - vytvoří vesnici pro gildy
  |cff00ff00.gv delete <GUILDID>|r           - kompletní odstranění vesnice
  |cff00ff00.gv reset <GUILDID>|r            - wipe + reinstall base layout
  |cff00ff00.gv list [PAGE]|r                - vypíše 10 vesnic na stránku
  |cff00ff00.gv teleport <GUILDID>|r         - portne tě do vesnice dané gildy (alias: .gv tp)
  |cff00ff00.gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]|r
  |cff00ff00.gv object <ENTRY> [SPAWNTIMESECS]|r
  |cff00ff00.gv excreature <EXPKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]|r
  |cff00ff00.gv exobject <EXPKEY> <ENTRY> <FACTION> [SPAWNTIMESECS]|r)",
                R"(|cffffd000[GV]|r Available commands:
  |cff00ff00.gv create [GUILDID] [ignorecap]|r - create village
  |cff00ff00.gv delete <GUILDID>|r           - remove village completely
  |cff00ff00.gv reset <GUILDID>|r            - wipe + reinstall base layout
  |cff00ff00.gv list [PAGE]|r                - list 10 villages per page
  |cff00ff00.gv teleport <GUILDID>|r         - teleport you to that guild's village (alias: .gv tp)
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
                    handler->SendSysMessage(T("|cffff5555[GV-GM]|r Nejsi v gildě a GUILDID nebylo zadáno.",
                                              "|cffff5555[GV-GM]|r You are not in a guild and GUILDID was not provided."));
                    return true;
                }
            }

            if (GuildVillage::GuildHasVillage(guildId))
            {
                handler->SendSysMessage(T("|cffffaa00[GV-GM]|r Tato gilda už vesnici má.",
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
            uint32 phaseId = 0;
            if (QueryResult pr = WorldDatabase.Query("SELECT phase FROM customs.gv_guild WHERE guild = {} LIMIT 1", guildId))
                phaseId = (*pr)[0].Get<uint32>();

            if (!GuildVillage::GuildHasVillage(guildId))
            {
                handler->SendSysMessage(T("|cffffaa00[GV-GM]|r Tato gilda nemá vesnici.",
                                          "|cffffaa00[GV-GM]|r This guild does not have a village."));
                return true;
            }

            if (phaseId)
                DespawnPhaseObjects(DefMap(), phaseId);

            bool ok = GuildVillage::DeleteVillageForGuild_GM(guildId);
            handler->SendSysMessage(ok ?
                T("|cff00ff00[GV-GM]|r Vesnice odstraněna (DB + despawn).",
                  "|cff00ff00[GV-GM]|r Village removed (DB + despawn).") :
                T("|cffff5555[GV-GM]|r Odstranění selhalo.",
                  "|cffff5555[GV-GM]|r Delete failed."));
            return true;
        }

        // === Default help ===
        handler->SendSysMessage(T(
            R"(|cffffd000[GV]|r Dostupné příkazy:
  |cff00ff00.gv create [GUILDID] [ignorecap]|r
  |cff00ff00.gv delete <GUILDID>|r
  |cff00ff00.gv reset <GUILDID>|r
  |cff00ff00.gv list [PAGE]|r
  |cff00ff00.gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]|r
  |cff00ff00.gv object <ENTRY> [SPAWNTIMESECS]|r
  |cff00ff00.gv excreature <EXPKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNTIMESECS]|r
  |cff00ff00.gv exobject <EXPKEY> <ENTRY> <FACTION> [SPAWNTIMESECS]|r)",
            R"(|cffffd000[GV]|r Available commands:
  |cff00ff00.gv create [GUILDID] [ignorecap]|r
  |cff00ff00.gv delete <GUILDID>|r
  |cff00ff00.gv reset <GUILDID>|r
  |cff00ff00.gv list [PAGE]|r
  |cff00ff00.gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]|r
  |cff00ff00.gv object <ENTRY> [SPAWNTIMESECS]|r
  |cff00ff00.gv excreature <EXPKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNTIMESECS]|r
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
