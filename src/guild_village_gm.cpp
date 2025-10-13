// modules/mod-guild-village/src/guild_village_gm.cpp

#include "ScriptMgr.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "WorldSession.h"
#include "Player.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "StringFormat.h"
#include "Config.h" // << přidáno pro LangOpt()

#include "Maps/MapMgr.h"
#include "Map.h"
#include "GameObject.h"
#include "Transport.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "Creature.h"

#include <string>
#include <algorithm>
#include <cctype>
#include <limits>
#include <vector>

static inline uint32 DefMap() { return 37; }   // GV mapa

namespace
{
    // === Locale handling (cs|en) – varianta B ===
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

    static std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
        return s;
    }

    static bool ParseFaction(std::string s, uint32& out)
    {
        s = ToLower(Trim(std::move(s)));
        if (s == "0" || s == "both") { out = 0; return true; }
        if (s == "1" || s == "alliance" || s == "ally" || s == "all") { out = 1; return true; }
        if (s == "2" || s == "horde") { out = 2; return true; }
        // povol i čistě číselný vstup
        char* e = nullptr; unsigned long v = strtoul(s.c_str(), &e, 10);
        if (e && *e == '\0' && v <= 2) { out = static_cast<uint32>(v); return true; }
        return false;
    }

    // --- Despawn všech creature/GO pro daný phaseMask na mapId (okamžitě ze světa) ---
    static void DespawnPhaseObjects(uint32 mapId, uint32 phaseMask, Player* /*ctx*/)
    {
        std::vector<uint32> cGuids;
        std::vector<uint32> gGuids;

        if (QueryResult qc = WorldDatabase.Query(
                "SELECT guid FROM creature WHERE map = {} AND phaseMask = {}", mapId, phaseMask))
        {
            do { cGuids.emplace_back((*qc)[0].Get<uint32>()); } while (qc->NextRow());
        }

        if (QueryResult qg = WorldDatabase.Query(
                "SELECT guid FROM gameobject WHERE map = {} AND phaseMask = {}", mapId, phaseMask))
        {
            do { gGuids.emplace_back((*qg)[0].Get<uint32>()); } while (qg->NextRow());
        }

        Map* map = sMapMgr->FindMap(mapId, 0);
        if (!map)
            return;

        // Vyhledej v interních storech dle spawnId a dej remove-list
        {
            auto& cStore = map->GetCreatureBySpawnIdStore();           // unordered_map<LowType, Creature*>
            for (uint32 spawnId : cGuids)
            {
                auto it = cStore.find(spawnId);
                if (it != cStore.end() && it->second)
                    it->second->AddObjectToRemoveList();
            }
        }
        {
            auto& gStore = map->GetGameObjectBySpawnIdStore();         // unordered_map<LowType, GameObject*>
            for (uint32 spawnId : gGuids)
            {
                auto it = gStore.find(spawnId);
                if (it != gStore.end() && it->second)
                    it->second->AddObjectToRemoveList();
            }
        }

        map->RemoveAllObjectsInRemoveList();
    }

    // === Live instalace BASE layoutu (převzato z guild_village_create.cpp) ===
    static void InstallBaseLayout_Live(uint32 /*guildId*/, uint32 phaseMask, std::string const& layout_key = "base")
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

                c->CleanupsBeforeDelete();
                delete c;

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

                    g->CleanupsBeforeDelete();
                    delete g;

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
                        "VALUES ({}, {}, 1, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
                        entry, mapId, phaseMask, x, y, z, o, r0, r1, r2, r3, spawntime
                    );
                    ++goCount;
                }
            }
            while (go->NextRow());
        }

        LOG_INFO("modules", "GV: (GM reset) Installed base layout '{}' -> creatures={}, gameobjects={}, phaseMask={}",
                 layout_key, cCount, goCount, phaseMask);
    }

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
                "Použití: .gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS] | .gv object <ENTRY> [SPAWNTIMESECS] | .gv excreature <EXPANSIONKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS] | .gv exobject <EXPANSIONKEY> <ENTRY> <FACTION> [SPAWNTIMESECS] | .gv reset <GUILDID>",
                "Usage: .gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS] | .gv object <ENTRY> [SPAWNTIMESECS] | .gv excreature <EXPANSIONKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS] | .gv exobject <EXPANSIONKEY> <ENTRY> <FACTION> [SPAWNTIMESECS] | .gv reset <GUILDID>"));
            return true;
        }

        size_t sp = a.find(' ');
        std::string cmd = sp == std::string::npos ? a : a.substr(0, sp);
        std::string rest = sp == std::string::npos ? "" : Trim(a.substr(sp + 1));
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c){ return std::tolower(c); });

        // ===== .gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS] =====
        if (cmd == "creature")
        {
            if (rest.empty())
            {
                handler->SendSysMessage(T("Použití: .gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]",
                                          "Usage: .gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]"));
                return true;
            }

            // tokenize
            std::vector<std::string> tok;
            { size_t pos=0,nxt; while ((nxt=rest.find(' ',pos))!=std::string::npos){ tok.emplace_back(rest.substr(pos,nxt-pos)); pos=nxt+1; } if (pos<rest.size()) tok.emplace_back(rest.substr(pos)); }

            if (tok.size() != 1 && tok.size() != 4)
            {
                handler->SendSysMessage(T("Neplatné parametry. Použití: .gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]",
                                          "Invalid parameters. Usage: .gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]"));
                return true;
            }

            auto parseU32 = [](std::string const& s, uint32& out)->bool{
                char* e=nullptr; unsigned long v=strtoul(s.c_str(), &e, 10);
                if (!e || *e!='\0' || v>std::numeric_limits<uint32>::max()) return false;
                out = static_cast<uint32>(v); return true;
            };

            uint32 entry=0, movementtype=1, spawndist_u32=15, spawntimesecs=300; // defaulty pro běžné creature
            if (!parseU32(tok[0], entry) || entry==0)
            { handler->SendSysMessage(T("Neplatné ENTRY (kladné číslo).", "Invalid ENTRY (positive number expected).")); return true; }

            if (tok.size()==4)
            {
                if (!parseU32(tok[1], movementtype)) { handler->SendSysMessage(T("Neplatný MOVEMENTTYPE.", "Invalid MOVEMENTTYPE.")); return true; }
                if (!parseU32(tok[2], spawndist_u32)) { handler->SendSysMessage(T("Neplatný SPAWNDIST.", "Invalid SPAWNDIST.")); return true; }
                if (!parseU32(tok[3], spawntimesecs)) { handler->SendSysMessage(T("Neplatný SPAWNTIMESECS.", "Invalid SPAWNTIMESECS.")); return true; }
            }

            float spawndist = static_cast<float>(spawndist_u32);

            // Pozice hráče
            float x = plr->GetPositionX();
            float y = plr->GetPositionY();
            float z = plr->GetPositionZ();
            float o = plr->GetOrientation();

            uint32 map            = DefMap();
            std::string layout    = "base";
            std::string comment   = "Village mob";

            // nové id
            uint32 newId = 1;
            if (QueryResult r = WorldDatabase.Query("SELECT COALESCE(MAX(id),0)+1 FROM customs.gv_creature_template"))
                newId = (*r)[0].Get<uint32>();

            WorldDatabase.DirectExecute(Acore::StringFormat(
                "INSERT INTO customs.gv_creature_template "
                "(id, layout_key, entry, map, position_x, position_y, position_z, orientation, spawntimesecs, spawndist, movementtype, comment) "
                "VALUES ({}, '{}', {}, {}, {}, {}, {}, {}, {}, {}, {}, '{}')",
                newId, layout, entry, map, x, y, z, o, spawntimesecs, spawndist, movementtype, comment).c_str());

            handler->SendSysMessage(Acore::StringFormat(
                T("|cff00ff00[GV-GM]|r Přidán creature template: entry={} -> gv_creature_template.id={} (map={}, x={}, y={}, z={}, o={}, resp={}s, dist={}, move={})",
                  "|cff00ff00[GV-GM]|r Added creature template: entry={} -> gv_creature_template.id={} (map={}, x={}, y={}, z={}, o={}, resp={}s, dist={}, move={})"),
                entry, newId, map, x, y, z, o, spawntimesecs, spawndist_u32, movementtype).c_str());

            if (plr->GetMapId() != DefMap())
                handler->SendSysMessage(T("|cffffaa00Upozornění: stojíš mimo mapu 37 – v DB je map=37.|r",
                                          "|cffffaa00Warning: you are outside map 37 — DB uses map=37.|r"));

            return true;
        }

        // ===== .gv object <ENTRY> [SPAWNTIMESECS] =====
        if (cmd == "object")
        {
            if (rest.empty())
            {
                handler->SendSysMessage(T("Použití: .gv object <ENTRY> [SPAWNTIMESECS]",
                                          "Usage: .gv object <ENTRY> [SPAWNTIMESECS]"));
                return true;
            }

            // tokenize
            std::vector<std::string> tok;
            { size_t pos=0,nxt; while ((nxt=rest.find(' ',pos))!=std::string::npos){ tok.emplace_back(rest.substr(pos,nxt-pos)); pos=nxt+1; } if (pos<rest.size()) tok.emplace_back(rest.substr(pos)); }

            if (tok.size() != 1 && tok.size() != 2)
            {
                handler->SendSysMessage(T("Neplatné parametry. Použití: .gv object <ENTRY> [SPAWNTIMESECS]",
                                          "Invalid parameters. Usage: .gv object <ENTRY> [SPAWNTIMESECS]"));
                return true;
            }

            auto parseU32 = [](std::string const& s, uint32& out)->bool{
                char* e=nullptr; unsigned long v=strtoul(s.c_str(), &e, 10);
                if (!e || *e!='\0' || v>std::numeric_limits<uint32>::max()) return false;
                out = static_cast<uint32>(v); return true;
            };

            uint32 entry=0;
            if (!parseU32(tok[0], entry) || entry==0)
            { handler->SendSysMessage(T("Neplatné ENTRY (kladné číslo).", "Invalid ENTRY (positive number expected).")); return true; }

            uint32 spawntimesecs = 0; // default
            if (tok.size()==2)
            {
                if (!parseU32(tok[1], spawntimesecs))
                { handler->SendSysMessage(T("Neplatný SPAWNTIMESECS.", "Invalid SPAWNTIMESECS.")); return true; }
            }

            // Pozice hráče
            float x = plr->GetPositionX();
            float y = plr->GetPositionY();
            float z = plr->GetPositionZ();
            float o = plr->GetOrientation();

            uint32 map            = DefMap();
            std::string layout    = "base";
            std::string comment   = "village object";

            // nové id = MAX(id)+1
            uint32 newId = 1;
            if (QueryResult r = WorldDatabase.Query("SELECT COALESCE(MAX(id),0)+1 FROM customs.gv_gameobject_template"))
                newId = (*r)[0].Get<uint32>();

            // rotation0..3 = 0, spawntimesecs = z parametru / 0
            WorldDatabase.DirectExecute(Acore::StringFormat(
                "INSERT INTO customs.gv_gameobject_template "
                "(id, layout_key, entry, map, position_x, position_y, position_z, orientation, rotation0, rotation1, rotation2, rotation3, spawntimesecs, comment) "
                "VALUES ({}, '{}', {}, {}, {}, {}, {}, {}, 0, 0, 0, 0, {}, '{}')",
                newId, layout, entry, map, x, y, z, o, spawntimesecs, comment).c_str());

            handler->SendSysMessage(Acore::StringFormat(
                T("|cff00ff00[GV-GM]|r Přidán object template: entry={} -> gv_gameobject_template.id={} (map={}, x={}, y={}, z={}, o={}, rot=0/0/0/0, resp={})",
                  "|cff00ff00[GV-GM]|r Added object template: entry={} -> gv_gameobject_template.id={} (map={}, x={}, y={}, z={}, o={}, rot=0/0/0/0, resp={})"),
                entry, newId, map, x, y, z, o, spawntimesecs).c_str());

            if (plr->GetMapId() != DefMap())
                handler->SendSysMessage(T("|cffffaa00Upozornění: stojíš mimo mapu 37 – v DB je map=37.|r",
                                          "|cffffaa00Warning: you are outside map 37 — DB uses map=37.|r"));

            return true;
        }

        // ===== .gv excreature <EXPANSIONKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS] =====
        if (cmd == "excreature")
        {
            if (rest.empty())
            {
                handler->SendSysMessage(T("Použití: .gv excreature <EXPANSIONKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]",
                                          "Usage: .gv excreature <EXPANSIONKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]"));
                return true;
            }

            // tokenize
            std::vector<std::string> tok;
            { size_t pos=0,nxt; while ((nxt=rest.find(' ',pos))!=std::string::npos){ tok.emplace_back(rest.substr(pos,nxt-pos)); pos=nxt+1; } if (pos<rest.size()) tok.emplace_back(rest.substr(pos)); }

            if (tok.size() != 3 && tok.size() != 6)
            {
                handler->SendSysMessage(T("Neplatné parametry. Použití: .gv excreature <EXPANSIONKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]",
                                          "Invalid parameters. Usage: .gv excreature <EXPANSIONKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]"));
                return true;
            }

            std::string expansionKey = tok[0];
            auto parseU32 = [](std::string const& s, uint32& out)->bool{
                char* e=nullptr; unsigned long v=strtoul(s.c_str(), &e, 10);
                if (!e || *e!='\0' || v>std::numeric_limits<uint32>::max()) return false;
                out = static_cast<uint32>(v); return true;
            };

            uint32 entry=0;
            if (!parseU32(tok[1], entry) || entry==0)
            { handler->SendSysMessage(T("Neplatné ENTRY (kladné číslo).", "Invalid ENTRY (positive number expected).")); return true; }

            uint32 faction=0;
            if (!ParseFaction(tok[2], faction))
            { handler->SendSysMessage(T("Neplatná FACTION (očekávám 0/1/2 nebo both/alliance/horde).",
                                        "Invalid FACTION (expected 0/1/2 or both/alliance/horde).")); return true; }

            // defaulty pro expansion-varianty
            uint32 movementtype = 0;
            uint32 spawndist_u32 = 0;
            uint32 spawntimesecs = 300;

            if (tok.size()==6)
            {
                if (!parseU32(tok[3], movementtype))   { handler->SendSysMessage(T("Neplatný MOVEMENTTYPE.", "Invalid MOVEMENTTYPE.")); return true; }
                if (!parseU32(tok[4], spawndist_u32))  { handler->SendSysMessage(T("Neplatný SPAWNDIST.", "Invalid SPAWNDIST.")); return true; }
                if (!parseU32(tok[5], spawntimesecs))  { handler->SendSysMessage(T("Neplatný SPAWNTIMESECS.", "Invalid SPAWNTIMESECS.")); return true; }
            }

            float spawndist = static_cast<float>(spawndist_u32);

            // pozice hráče
            float x = plr->GetPositionX();
            float y = plr->GetPositionY();
            float z = plr->GetPositionZ();
            float o = plr->GetOrientation();

            uint32 map = DefMap();

            // nové id
            uint32 newId = 1;
            if (QueryResult r = WorldDatabase.Query("SELECT COALESCE(MAX(id),0)+1 FROM customs.gv_expansion_creatures"))
                newId = (*r)[0].Get<uint32>();

            WorldDatabase.DirectExecute(Acore::StringFormat(
                "INSERT INTO customs.gv_expansion_creatures "
                "(id, expansion_key, entry, faction, map, position_x, position_y, position_z, orientation, spawntimesecs, spawndist, movementtype) "
                "VALUES ({}, '{}', {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
                newId, expansionKey, entry, faction, map, x, y, z, o, spawntimesecs, spawndist, movementtype).c_str());

            handler->SendSysMessage(Acore::StringFormat(
                T("|cff00ff00[GV-GM]|r Přidán EX-creature template: key='{}', entry={}, faction={}, id={}, map={}, pos=({}, {}, {} @ {}), resp={}s, dist={}, move={}",
                  "|cff00ff00[GV-GM]|r Added EX-creature template: key='{}', entry={}, faction={}, id={}, map={}, pos=({}, {}, {} @ {}), resp={}s, dist={}, move={}"),
                expansionKey, entry, faction, newId, map, x, y, z, o, spawntimesecs, spawndist_u32, movementtype).c_str());

            if (plr->GetMapId() != DefMap())
                handler->SendSysMessage(T("|cffffaa00Upozornění: stojíš mimo mapu 37 – v DB je map=37.|r",
                                          "|cffffaa00Warning: you are outside map 37 — DB uses map=37.|r"));

            return true;
        }

        // ===== .gv exobject <EXPANSIONKEY> <ENTRY> <FACTION> [SPAWNTIMESECS] =====
        if (cmd == "exobject")
        {
            if (rest.empty())
            {
                handler->SendSysMessage(T("Použití: .gv exobject <EXPANSIONKEY> <ENTRY> <FACTION> [SPAWNTIMESECS]",
                                          "Usage: .gv exobject <EXPANSIONKEY> <ENTRY> <FACTION> [SPAWNTIMESECS]"));
                return true;
            }

            // tokenize
            std::vector<std::string> tok;
            { size_t pos=0,nxt; while ((nxt=rest.find(' ',pos))!=std::string::npos){ tok.emplace_back(rest.substr(pos,nxt-pos)); pos=nxt+1; } if (pos<rest.size()) tok.emplace_back(rest.substr(pos)); }

            if (tok.size() != 3 && tok.size() != 4)
            {
                handler->SendSysMessage(T("Neplatné parametry. Použití: .gv exobject <EXPANSIONKEY> <ENTRY> <FACTION> [SPAWNTIMESECS]",
                                          "Invalid parameters. Usage: .gv exobject <EXPANSIONKEY> <ENTRY> <FACTION> [SPAWNTIMESECS]"));
                return true;
            }

            std::string expansionKey = tok[0];

            auto parseU32 = [](std::string const& s, uint32& out)->bool{
                char* e=nullptr; unsigned long v=strtoul(s.c_str(), &e, 10);
                if (!e || *e!='\0' || v>std::numeric_limits<uint32>::max()) return false;
                out = static_cast<uint32>(v); return true;
            };

            uint32 entry=0;
            if (!parseU32(tok[1], entry) || entry==0)
            { handler->SendSysMessage(T("Neplatné ENTRY (kladné číslo).", "Invalid ENTRY (positive number expected).")); return true; }

            uint32 faction=0;
            if (!ParseFaction(tok[2], faction))
            { handler->SendSysMessage(T("Neplatná FACTION (očekávám 0/1/2 nebo both/alliance/horde).",
                                        "Invalid FACTION (expected 0/1/2 or both/alliance/horde).")); return true; }

            // volitelný respawn (default 0)
            uint32 spawntimesecs = 0;
            if (tok.size()==4)
            {
                if (!parseU32(tok[3], spawntimesecs))
                { handler->SendSysMessage(T("Neplatný SPAWNTIMESECS.", "Invalid SPAWNTIMESECS.")); return true; }
            }

            // pozice hráče
            float x = plr->GetPositionX();
            float y = plr->GetPositionY();
            float z = plr->GetPositionZ();
            float o = plr->GetOrientation();

            uint32 map = DefMap();

            // nové id
            uint32 newId = 1;
            if (QueryResult r = WorldDatabase.Query("SELECT COALESCE(MAX(id),0)+1 FROM customs.gv_expansion_gameobjects"))
                newId = (*r)[0].Get<uint32>();

            WorldDatabase.DirectExecute(Acore::StringFormat(
                "INSERT INTO customs.gv_expansion_gameobjects "
                "(id, expansion_key, entry, faction, map, position_x, position_y, position_z, orientation, rotation0, rotation1, rotation2, rotation3, spawntimesecs) "
                "VALUES ({}, '{}', {}, {}, {}, {}, {}, {}, {}, 0, 0, 0, 0, {})",
                newId, expansionKey, entry, faction, map, x, y, z, o, spawntimesecs).c_str());

            handler->SendSysMessage(Acore::StringFormat(
                T("|cff00ff00[GV-GM]|r Přidán EX-object template: key='{}', entry={}, faction={}, id={}, map={}, pos=({}, {}, {} @ {}), rot=0/0/0/0, resp={}",
                  "|cff00ff00[GV-GM]|r Added EX-object template: key='{}', entry={}, faction={}, id={}, map={}, pos=({}, {}, {} @ {}), rot=0/0/0/0, resp={}"),
                expansionKey, entry, faction, newId, map, x, y, z, o, spawntimesecs).c_str());

            if (plr->GetMapId() != DefMap())
                handler->SendSysMessage(T("|cffffaa00Upozornění: stojíš mimo mapu 37 – v DB je map=37.|r",
                                          "|cffffaa00Warning: you are outside map 37 — DB uses map=37.|r"));

            return true;
        }

        // ===== .gv reset <GUILDID> =====
        if (cmd == "reset")
        {
            if (rest.empty())
            {
                handler->SendSysMessage(T("Použití: .gv reset <GUILDID>",
                                          "Usage: .gv reset <GUILDID>"));
                return true;
            }

            char* endp = nullptr;
            unsigned long gval = strtoul(rest.c_str(), &endp, 10);
            if (!endp || *endp != '\0' || gval == 0 || gval > std::numeric_limits<uint32>::max())
            {
                handler->SendSysMessage(T("Neplatné GUILDID (očekávám kladné číslo).",
                                          "Invalid GUILDID (positive number expected)."));
                return true;
            }
            uint32 guildId = static_cast<uint32>(gval);

            // Načti phaseMask
            uint32 phaseMask = 0;
            uint32 mapId = DefMap();
            if (QueryResult pr = WorldDatabase.Query(
                    "SELECT phase FROM customs.gv_guild WHERE guild = {} LIMIT 1", guildId))
            {
                phaseMask = (*pr)[0].Get<uint32>();
            }
            else
            {
                handler->SendSysMessage(T("|cffff5555[GV-GM]|r Guilda nenalezena v customs.gv_guild (sloupec 'guild').",
                                          "|cffff5555[GV-GM]|r Guild not found in customs.gv_guild (column 'guild')."));
                return true;
            }

            // informativně
            uint64 cntUp = 0, cntCr = 0, cntGo = 0, cntCur = 0;
            if (QueryResult r1 = WorldDatabase.Query("SELECT COUNT(*) FROM customs.gv_upgrades WHERE guildid = {}", guildId))
                cntUp = (*r1)[0].Get<uint64>();
            if (QueryResult r2 = WorldDatabase.Query("SELECT COUNT(*) FROM creature WHERE map = {} AND phaseMask = {}", mapId, phaseMask))
                cntCr = (*r2)[0].Get<uint64>();
            if (QueryResult r3 = WorldDatabase.Query("SELECT COUNT(*) FROM gameobject WHERE map = {} AND phaseMask = {}", mapId, phaseMask))
                cntGo = (*r3)[0].Get<uint64>();
            if (QueryResult r4 = WorldDatabase.Query("SELECT COUNT(*) FROM customs.gv_currency WHERE guildid = {}", guildId))
                cntCur = (*r4)[0].Get<uint64>();

            handler->SendSysMessage(Acore::StringFormat(
                T("|cffffd000[GV-GM]|r Reset guildu {} (phaseMask={}). Odstraním: upgrades={}, creatures={}, gameobjects={}, currencyRows={}",
                  "|cffffd000[GV-GM]|r Resetting guild {} (phaseMask={}). Will remove: upgrades={}, creatures={}, gameobjects={}, currencyRows={}"),
                guildId, phaseMask, cntUp, cntCr, cntGo, cntCur).c_str());

            // 1) Okamžitý despawn ze světa
            DespawnPhaseObjects(mapId, phaseMask, plr);

            // 2) Wipe v DB + vynulování měn
            WorldDatabase.DirectExecute("START TRANSACTION");
            WorldDatabase.Execute("DELETE FROM customs.gv_upgrades WHERE guildid = {}", guildId);
            WorldDatabase.Execute("DELETE FROM creature  WHERE map = {} AND phaseMask = {}", mapId, phaseMask);
            WorldDatabase.Execute("DELETE FROM gameobject WHERE map = {} AND phaseMask = {}", mapId, phaseMask);
            WorldDatabase.Execute("UPDATE customs.gv_currency SET timber=0, stone=0, iron=0, crystal=0 WHERE guildid = {}", guildId);
            WorldDatabase.DirectExecute("COMMIT");

            handler->SendSysMessage(T("|cff00ff00[GV-GM]|r Smazáno (včetně živých objektů). Obnovuji base layout…",
                                      "|cff00ff00[GV-GM]|r Deleted (including live objects). Reinstalling base layout…"));

            // 3) Live reinstalace
            InstallBaseLayout_Live(guildId, phaseMask, "base");

            handler->SendSysMessage(T("|cff00ff00[GV-GM]|r Hotovo – vše je znovu nainstalováno bez duplicit.",
                                      "|cff00ff00[GV-GM]|r Done – base layout reinstalled without duplicates."));
            return true;
        }

        handler->SendSysMessage(T(
            "Použití: .gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS] | .gv object <ENTRY> [SPAWNTIMESECS] | .gv excreature <EXPANSIONKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS] | .gv exobject <EXPANSIONKEY> <ENTRY> <FACTION> [SPAWNTIMESECS] | .gv reset <GUILDID>",
            "Usage: .gv creature <ENTRY> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS] | .gv object <ENTRY> [SPAWNTIMESECS] | .gv excreature <EXPANSIONKEY> <ENTRY> <FACTION> [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS] | .gv exobject <EXPANSIONKEY> <ENTRY> <FACTION> [SPAWNTIMESECS] | .gv reset <GUILDID>"));
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

            std::vector<ChatCommandBuilder> out;
            out.emplace_back(gv);
            return out;
        }
    };
}

// Export registrace
void RegisterGuildVillageGM()
{
    new GuildVillageGM_Command();
}
