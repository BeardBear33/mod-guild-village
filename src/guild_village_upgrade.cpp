// modules/mod-guild-village/src/guild_village_upgrade.cpp

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
#include "Log.h"
#include "Maps/MapMgr.h"
#include "GameObject.h"
#include "Transport.h"
#include "EventProcessor.h"
#include "gv_names.h"
#include "guild_village_production.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <algorithm>
#include <unordered_set>

namespace GuildVillage
{
    // ---------- Lokalizace ----------
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
	
	// Caps
	static inline bool CapsEnabled()
	{
		return sConfigMgr->GetOption<bool>("GuildVillage.CurrencyCap.Enabled", true);
	}
	static inline uint32 CapMaterial1()  { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material1",   1000); }
	static inline uint32 CapMaterial2()   { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material2",    1000); }
	static inline uint32 CapMaterial3()    { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material3",     1000); }
	static inline uint32 CapMaterial4() { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material4",  1000); }
	
	static void SendCurrencyStatusToChat(Player* player, uint32 guildId)
	{
		if (!player) return;
	
		uint64 mat1=0, mat2=0, mat3=0, mat4=0;
		if (QueryResult r = WorldDatabase.Query(
				"SELECT material1, material2, material3, material4 FROM customs.gv_currency WHERE guildId={}", guildId))
		{
			Field* f = r->Fetch();
			mat1 = f[0].Get<uint64>();
			mat2 = f[1].Get<uint64>();
			mat3 = f[2].Get<uint64>();
			mat4 = f[3].Get<uint64>();
		}
		else
		{
			ChatHandler(player->GetSession()).SendSysMessage(
				T("Tvoje guilda nevlastní vesnici (žádná měnová tabulka).",
				"Your guild does not own a village (no currency table)."));
			return;
		}
	
		auto const& N = GuildVillage::Names::Get();
		ChatHandler h(player->GetSession());
		h.SendSysMessage(T("|cff00ff00[Guildovní vesnice]|r – materiály",
						"|cff00ff00[Guild Village]|r – materials"));
	
		auto send = [&](std::string const& name, uint64 val, uint32 cap)
		{
			std::string line = "|cff00ffff" + name + ":|r " + std::to_string(val);
			if (CapsEnabled())
			{
				if (cap == 0) line += " / ∞";
				else          line += " / " + std::to_string(cap);
			}
			h.SendSysMessage(line.c_str());
		};
	
		send(N.status.material1,  mat1, CapMaterial1());
		send(N.status.material2,   mat2, CapMaterial2());
		send(N.status.material3,    mat3, CapMaterial3());
		send(N.status.material4, mat4, CapMaterial4());
	}

    // ---------- Konfigurace práv ----------
    static inline bool Cfg_PurchaseGMOnly()
    {
        return sConfigMgr->GetOption<bool>("GuildVillage.RequireGuildMasterForPurchase", false);
    }
    static inline bool Cfg_HidePurchaseForNonGM()
    {
        return sConfigMgr->GetOption<bool>("GuildVillage.HidePurchaseMenuForNonGM", false);
    }

    // Je hráč leader své guildy? (bere i Officera - rid=1)
    static inline bool IsGuildLeader(Player* player)
    {
        if (Guild* g = player->GetGuild())
        {
            if (g->GetLeaderGUID() == player->GetGUID())
                return true; // GM (rid=0)
            if (auto m = g->GetMember(player->GetGUID()))
                return m->GetRankId() == 1; // Officer (rid=1)
        }
        return false;
    }

    // ---------- Základ vesnice ----------
    static inline uint32 DefMap() { return sConfigMgr->GetOption<uint32>("GuildVillage.Default.Map", 37); }

    static std::optional<uint32> LoadVillagePhase(uint32 guildId)
    {
        if (QueryResult r = WorldDatabase.Query("SELECT phase FROM customs.gv_guild WHERE guild={}", guildId))
            return (*r)[0].Get<uint32>();
        return std::nullopt;
    }

    // ---------- Live instalace expanze (CREATURES/GO) s filtrem podle frakce ----------
    // factionFilter: 0=oboje, 1=Alliance, 2=Horde
    static bool ApplyUpgradeByKey(uint32 guildId, uint32 phaseId, std::string const& key, uint8 factionFilter)
    {
        // Duplicitní nákup blokuj
        if (QueryResult q = WorldDatabase.Query(
                "SELECT 1 FROM customs.gv_upgrades WHERE guildId={} AND expansion_key='{}'", guildId, key))
            return false;

        // --- CREATURES ---
        if (QueryResult cr = WorldDatabase.Query(
                "SELECT entry, map, position_x, position_y, position_z, orientation, spawntimesecs, spawndist, movementtype "
                "FROM customs.gv_expansion_creatures WHERE expansion_key='{}' AND (faction=0 OR faction={})",
                key, (uint32)factionFilter))
        {
            do
            {
                Field* f = cr->Fetch();
                uint32 entry=f[0].Get<uint32>(), mapId=f[1].Get<uint32>();
                float x=f[2].Get<float>(), y=f[3].Get<float>(), z=f[4].Get<float>(), o=f[5].Get<float>();
                uint32 resp=f[6].Get<uint32>(); float wander=f[7].Get<float>(); uint8 mt=f[8].Get<uint8>();

                Map* map = sMapMgr->FindMap(mapId, 0);
                if (!map) { sMapMgr->CreateBaseMap(mapId); map = sMapMgr->FindMap(mapId, 0); }

				if (map)
				{
					Creature* c = new Creature();
					ObjectGuid::LowType low = map->GenerateLowGuid<HighGuid::Unit>();
					if (!c->Create(low, map, phaseId, entry, 0, x, y, z, o))
					{ delete c; continue; }
				
					// defaultní delay, ne absolutní čas
					c->SetRespawnDelay(resp);
					c->SetWanderDistance(wander);
					c->SetDefaultMovementType(MovementGeneratorType(mt));
				
					// uložit do DB
					c->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseId);
					uint32 spawnId = c->GetSpawnId();
				
					// pojistka – propsat hodnoty do tabulky
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
				else
				{
					WorldDatabase.Execute(
						"INSERT INTO creature (id1,map,spawnMask,phaseMask,position_x,position_y,position_z,orientation,spawntimesecs,wander_distance,MovementType) "
						"VALUES ({}, {}, 1, {}, {}, {}, {}, {}, {}, {}, {})",
						entry, mapId, phaseId, x, y, z, o, resp, wander, (uint32)mt
					);
				}
            }
            while (cr->NextRow());
        }

        // --- GAMEOBJECTS ---
        if (QueryResult go = WorldDatabase.Query(
                "SELECT entry, map, position_x, position_y, position_z, orientation, rotation0, rotation1, rotation2, rotation3, spawntimesecs "
                "FROM customs.gv_expansion_gameobjects WHERE expansion_key='{}' AND (faction=0 OR faction={})",
                key, (uint32)factionFilter))
        {
            do
            {
                Field* f = go->Fetch();
                uint32 entry=f[0].Get<uint32>(), mapId=f[1].Get<uint32>();
                float x=f[2].Get<float>(), y=f[3].Get<float>(), z=f[4].Get<float>(), o=f[5].Get<float>();
                float r0=f[6].Get<float>(), r1=f[7].Get<float>(), r2=f[8].Get<float>(), r3=f[9].Get<float>();
                int32 st=f[10].Get<int32>();

                Map* map = sMapMgr->FindMap(mapId, 0);
                if (!map) { sMapMgr->CreateBaseMap(mapId); map = sMapMgr->FindMap(mapId, 0); }

                if (map)
                {
                    GameObject* g = sObjectMgr->IsGameObjectStaticTransport(entry) ? new StaticTransport() : new GameObject();
                    ObjectGuid::LowType low = map->GenerateLowGuid<HighGuid::GameObject>();
                    if (!g->Create(low, entry, map, phaseId, x, y, z, o, G3D::Quat(r0,r1,r2,r3), 0, GO_STATE_READY))
                    { delete g; continue; }
                    g->SetRespawnTime(st);
                    g->SaveToDB(map->GetId(), (1 << map->GetSpawnMode()), phaseId);
                    uint32 spawnId = g->GetSpawnId();
                    g->CleanupsBeforeDelete(); delete g;
                    g = sObjectMgr->IsGameObjectStaticTransport(entry) ? new StaticTransport() : new GameObject();
                    if (!g->LoadGameObjectFromDB(spawnId, map, true)) { delete g; continue; }
                    sObjectMgr->AddGameobjectToGrid(spawnId, sObjectMgr->GetGameObjectData(spawnId));
                }
                else
                {
                    WorldDatabase.Execute(
                        "INSERT INTO gameobject (id,map,spawnMask,phaseMask,position_x,position_y,position_z,orientation,rotation0,rotation1,rotation2,rotation3,spawntimesecs) "
                        "VALUES ({}, {}, 1, {}, {}, {}, {}, {}, {}, {}, {}, {}, {})",
                        entry, mapId, phaseId, x, y, z, o, r0, r1, r2, r3, st
                    );
                }
            }
            while (go->NextRow());
        }

        WorldDatabase.DirectExecute(
            "INSERT INTO customs.gv_upgrades (guildId, expansion_key, purchased_at) "
            "VALUES ({}, '{}', UNIX_TIMESTAMP())",
            guildId, key
        );
        return true;
    }

    // ---------- Katalog položek ----------
    // faction v šablonách (0=oboje, 1=Alliance, 2=Horde). Katalog zůstává společný.
    enum class Cat : uint8 { Trainers=1, Professions, Vendor, Portal, Objects, Others };

    struct CatalogRow
    {
        uint32    id;
        Cat       cat;
        std::string key;
        std::string label_cs;
        std::string label_en;
        uint32 cost_mat1=0, cost_mat2=0, cost_mat3=0, cost_mat4=0;
        uint8  sort=0;
    };

    static std::vector<CatalogRow> LoadCatalog(Cat cat)
    {
        std::vector<CatalogRow> out;
        char const* catName = "";
        switch (cat)
        {
            case Cat::Trainers:    catName = "trainers"; break;
            case Cat::Professions: catName = "professions"; break;
            case Cat::Vendor:      catName = "vendor"; break;
            case Cat::Portal:      catName = "portal"; break;
            case Cat::Objects:     catName = "objects"; break;
            case Cat::Others:      catName = "others"; break;
        }

        if (QueryResult r = WorldDatabase.Query(
                "SELECT id, expansion_key, label_cs, label_en, cost_material1, cost_material2, cost_material3, cost_material4, sort_order "
                "FROM customs.gv_upgrade_catalog WHERE category='{}' ORDER BY sort_order, id", catName))
        {
            do
            {
                Field* f = r->Fetch();
                CatalogRow c;
                c.id        = f[0].Get<uint32>();
                c.key       = f[1].Get<std::string>();
                c.label_cs  = f[2].Get<std::string>();
                c.label_en  = f[3].Get<std::string>();
                c.cost_mat1  = f[4].Get<uint32>();
                c.cost_mat2  = f[5].Get<uint32>();
                c.cost_mat3  = f[6].Get<uint32>();
                c.cost_mat4 = f[7].Get<uint32>();
                c.sort      = f[8].Get<uint8>();
                c.cat       = cat;
                out.push_back(std::move(c));
            }
            while (r->NextRow());
        }
        return out;
    }

    static std::string CostLine(CatalogRow const& c)
	{
		return GuildVillage::Names::CostLine(c.cost_mat1, c.cost_mat2, c.cost_mat3, c.cost_mat4);
	}


    // ---------- Odečet měny ----------
    static bool TryDeductCurrency(uint32 guildId, CatalogRow const& c)
    {
        // 1) načíst aktuální stav
        uint32 mat1=0, mat2=0, mat3=0, mat4=0;
        if (QueryResult q = WorldDatabase.Query(
                "SELECT material1, material2, material3, material4 FROM customs.gv_currency WHERE guildId={}", guildId))
        {
            Field* f = q->Fetch();
            mat1=f[0].Get<uint32>(); mat2=f[1].Get<uint32>(); mat3=f[2].Get<uint32>(); mat4=f[3].Get<uint32>();
        }
        else
        {
            return false;
        }

        // 2) kontrola
        if (mat1 < c.cost_mat1 || mat2 < c.cost_mat2 || mat3 < c.cost_mat3 || mat4 < c.cost_mat4)
            return false;

        // 3) odečet
        WorldDatabase.Execute(
            "UPDATE customs.gv_currency SET "
            "material1 = material1 - {}, material2 = material2 - {}, material3 = material3 - {}, material4 = material4 - {}, last_update = NOW() "
            "WHERE guildId = {}",
            c.cost_mat1, c.cost_mat2, c.cost_mat3, c.cost_mat4, guildId
        );
        return true;
    }

    // ---------- Menu stav ----------
    struct MenuState
    {
        Cat cat;
        std::vector<CatalogRow> items;
    };
    static std::unordered_map<ObjectGuid::LowType, MenuState> s_menu;

    // Vrátí množinu už zakoupených klíčů (používá se pro skrytí již koupených)
    static std::unordered_set<std::string> LoadPurchasedKeys(uint32 guildId)
    {
        std::unordered_set<std::string> out;
        if (QueryResult r = WorldDatabase.Query(
                "SELECT expansion_key FROM customs.gv_upgrades WHERE guildId={}", guildId))
        {
            do { out.insert(r->Fetch()[0].Get<std::string>()); }
            while (r->NextRow());
        }
        return out;
    }

    // Dostupnost obsahu pro frakci (pro zobrazení i nákup)
    static bool HasFactionContent(std::string const& key, uint8 factionFilter)
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT 1 FROM customs.gv_expansion_creatures "
                "WHERE expansion_key='{}' AND (faction=0 OR faction={}) LIMIT 1",
                key, (uint32)factionFilter))
            return true;

        if (QueryResult r2 = WorldDatabase.Query(
                "SELECT 1 FROM customs.gv_expansion_gameobjects "
                "WHERE expansion_key='{}' AND (faction=0 OR faction={}) LIMIT 1",
                key, (uint32)factionFilter))
            return true;

        return false;
    }

    // ---------- Gossip akce (bez WHERE) ----------
    enum GossipAction : uint32
    {
        ACT_ROOT = 100,
        ACT_CAT_TRAINERS,
        ACT_CAT_PROFESSIONS,
        ACT_CAT_VENDOR,
        ACT_CAT_PORTAL,
        ACT_CAT_OBJECTS,
        ACT_CAT_OTHERS,

        ACT_ITEM_BASE    = 1000, // ACT_ITEM_BASE + index
        ACT_CONFIRM_BASE = 5000, // ACT_CONFIRM_BASE + index

        ACT_BACK_CATEGORY = 9000,       // zpět na hlavní rozcestník
        ACT_BACK_TO_CATEGORY = 9001,     // zpět do poslední otevřené kategorie (po potvrzení)
		ACT_SHOW_STATUS = 9100,
		ACT_SEPARATOR        = 9199
    };

    static void ShowRoot(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        const bool allowPurchaseMenu = !Cfg_HidePurchaseForNonGM() || IsGuildLeader(player);

        if (allowPurchaseMenu)
        {
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE,     T("Trenér", "Trainers"),      GOSSIP_SENDER_MAIN, ACT_CAT_TRAINERS);
            AddGossipItemFor(player, GOSSIP_ICON_TRAINER,    T("Profese", "Professions"),  GOSSIP_SENDER_MAIN, ACT_CAT_PROFESSIONS);
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR,     T("Vendor", "Vendors"),       GOSSIP_SENDER_MAIN, ACT_CAT_VENDOR);
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, T("Portály", "Portal"),       GOSSIP_SENDER_MAIN, ACT_CAT_PORTAL);
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, T("Objekty", "Objects"),      GOSSIP_SENDER_MAIN, ACT_CAT_OBJECTS);
            AddGossipItemFor(player, GOSSIP_ICON_TABARD,     T("Ostatní", "Others"),       GOSSIP_SENDER_MAIN, ACT_CAT_OTHERS);
			AddGossipItemFor(player, 0, "|cff808080---------------------------|r",         GOSSIP_SENDER_MAIN, ACT_SEPARATOR);
			AddGossipItemFor(player, 0, T("|cff5519FAZobrazit stav materiálu:|r", "|cff5519FAShow currency status:|r"), GOSSIP_SENDER_MAIN, ACT_SHOW_STATUS);
        }
        else
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Nákup spravují pouze Guild Master a Zástupce.",
                  "Purchases are managed by the Guild Master and Officers only."));
        }

        SendGossipMenuFor(player, 1, creature->GetGUID());
    }

    static void ShowCategory(Player* player, Creature* creature, Cat cat)
    {
        ClearGossipMenuFor(player);

        Guild* g = player->GetGuild();
        if (!g)
        {
            SendGossipMenuFor(player, 1, creature->GetGUID());
            return;
        }

        // 1=Alliance, 2=Horde
        uint8 factionFilter = (player->GetTeamId() == TEAM_ALLIANCE) ? 1 : 2;

        // Načíst katalog + set již zakoupených klíčů
        auto listAll = LoadCatalog(cat);
        auto purchased = LoadPurchasedKeys(g->GetId());

        // Filtrování: musí existovat obsah pro frakci a NESMÍ být již zakoupeno
        std::vector<CatalogRow> list;
        list.reserve(listAll.size());
        for (auto const& c : listAll)
        {
            if (!HasFactionContent(c.key, factionFilter))
                continue;
            if (purchased.find(c.key) != purchased.end())
                continue; // už vlastněno → nezobrazovat
            list.push_back(c);
        }

        s_menu[player->GetGUID().GetCounter()] = MenuState{ cat, list };

        if (list.empty())
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("V této kategorii teď nemáš co kupovat.", "No available upgrades left in this category."));
            // Zpět na rozcestník
            AddGossipItemFor(player, GOSSIP_ICON_TAXI, T("Zpet", "Back"), GOSSIP_SENDER_MAIN, ACT_BACK_CATEGORY);
            SendGossipMenuFor(player, 1, creature->GetGUID());
            return;
        }

        ChatHandler(player->GetSession()).SendSysMessage(
            T("Vyber položku k zakoupení:", "Select an upgrade to purchase:"));

        for (uint32 i = 0; i < list.size(); ++i)
        {
            auto const& c = list[i];
            std::string label = (LangOpt() == Lang::EN ? c.label_en : c.label_cs);
            std::string line = Acore::StringFormat("{} — {}", label, CostLine(c));
            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, line, GOSSIP_SENDER_MAIN, ACT_ITEM_BASE + i);
        }

        // Tlačítko Zpět (jen v kategoriích)
        AddGossipItemFor(player, GOSSIP_ICON_TAXI, T("Zpet", "Back"), GOSSIP_SENDER_MAIN, ACT_BACK_CATEGORY);
        SendGossipMenuFor(player, 1, creature->GetGUID());
    }

	// --- Po nákupu: zavřít gossip a po 50ms znovu otevřít stejnou kategorii ---
	class ReopenCategoryEvent : public BasicEvent
	{
	public:
		ReopenCategoryEvent(Player* p, ObjectGuid npc, Cat c)
			: _player(p), _npcGuid(npc), _cat(c) {}
	
		bool Execute(uint64 /*time*/, uint32 /*diff*/) override
		{
			if (!_player)
				return true;
	
			if (Creature* np = ObjectAccessor::GetCreature(*_player, _npcGuid))
				ShowCategory(_player, np, _cat);
			return true;
		}
	
	private:
		Player*   _player;
		ObjectGuid _npcGuid;
		Cat        _cat;
	};
	
	static void ReopenCategoryAfterPurchase(Player* player, Creature* creature, Cat cat)
	{
		if (!player || !creature)
			return;
	
		ObjectGuid npcGuid = creature->GetGUID();
		CloseGossipMenuFor(player);
		// 50 ms odklad
		player->m_Events.AddEvent(new ReopenCategoryEvent(player, npcGuid, cat),
								player->m_Events.CalculateTime(50));
	}

    static void ShowConfirm(Player* player, Creature* creature, CatalogRow const& c)
    {
        ClearGossipMenuFor(player);
        std::string label = (LangOpt()==Lang::EN ? c.label_en : c.label_cs);
        ChatHandler(player->GetSession()).SendSysMessage(
            Acore::StringFormat("{} {}", T("Potvrdit nákup:", "Confirm purchase:"), label).c_str());
        ChatHandler(player->GetSession()).SendSysMessage(
            Acore::StringFormat("{} {}", T("Cena:", "Cost:"), CostLine(c)).c_str());

        auto& state = s_menu[player->GetGUID().GetCounter()];
        auto it = std::find_if(state.items.begin(), state.items.end(),
                               [&](CatalogRow const& x){ return x.id == c.id; });
        uint32 idx = (it==state.items.end()) ? 0u : uint32(std::distance(state.items.begin(), it));

        if (Cfg_PurchaseGMOnly() && !IsGuildLeader(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("|cffff4444Tento nákup mohou provést pouze Guild Master a Zástupce.|r",
                  "|cffff4444Only the Guild Master and Officers can perform this purchase.|r"));
        }

        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, T("Ano, zakoupit", "Yes, purchase"), GOSSIP_SENDER_MAIN, ACT_CONFIRM_BASE + idx);
        AddGossipItemFor(player, GOSSIP_ICON_TAXI, T("Ne, vrátit se zpátky", "No, go back"), GOSSIP_SENDER_MAIN, ACT_BACK_TO_CATEGORY);
        SendGossipMenuFor(player, 1, creature->GetGUID());
    }

    class npc_gv_upgrades : public CreatureScript
    {
    public:
        npc_gv_upgrades() : CreatureScript("npc_gv_upgrades") { }

        bool OnGossipHello(Player* player, Creature* creature) override
        {
            Guild* g = player->GetGuild();
            if (!g)
            {
                ChatHandler(player->GetSession()).SendSysMessage(T("Nejsi v guildě.", "You are not in a guild."));
                return true;
            }
            if (!LoadVillagePhase(g->GetId()).has_value())
            {
                ChatHandler(player->GetSession()).SendSysMessage(T("Tvoje guilda nevlastní vesnici.", "Your guild does not own a village."));
                return true;
            }
            ChatHandler(player->GetSession()).SendSysMessage(T("Správa rozšíření vesnice.", "Village upgrades manager."));
            ShowRoot(player, creature);
            return true;
        }

        bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
        {
            if (sender != GOSSIP_SENDER_MAIN)
                return false;

            Guild* g = player->GetGuild();
            if (!g) { CloseGossipMenuFor(player); return true; }
            auto phaseOpt = LoadVillagePhase(g->GetId());
            if (!phaseOpt) { CloseGossipMenuFor(player); return true; }
            uint32 phaseId = *phaseOpt; // jediné číslo fáze

            // frakce pro šablony: 1=Alliance, 2=Horde
            uint8 factionFilter = (player->GetTeamId() == TEAM_ALLIANCE) ? 1 : 2;
            bool isLeader = IsGuildLeader(player);

            switch (action)
            {
                // --- NÁKUP ---
                case ACT_CAT_TRAINERS:
                case ACT_CAT_PROFESSIONS:
                case ACT_CAT_VENDOR:
                case ACT_CAT_PORTAL:
                case ACT_CAT_OBJECTS:
                case ACT_CAT_OTHERS:
                {
                    // Pokud je menu skryté pro ne-GM, nepustím dál
                    if (Cfg_HidePurchaseForNonGM() && !isLeader)
                    {
                        ChatHandler(player->GetSession()).SendSysMessage(
                            T("Nákup spravují pouze Guild Master a Zástupce.",
                              "Purchases are managed by the Guild Master and Officers only."));
                        ShowRoot(player, creature);
                        return true;
                    }

                    Cat cat = Cat::Trainers;
                    if (action == ACT_CAT_PROFESSIONS) cat = Cat::Professions;
                    else if (action == ACT_CAT_VENDOR)  cat = Cat::Vendor;
                    else if (action == ACT_CAT_PORTAL)  cat = Cat::Portal;
                    else if (action == ACT_CAT_OBJECTS) cat = Cat::Objects;
                    else if (action == ACT_CAT_OTHERS)  cat = Cat::Others;

                    ShowCategory(player, creature, cat);
                    return true;
                }
				
				case ACT_SHOW_STATUS:
				{
					if (!player->GetGuild())
					{
						ChatHandler(player->GetSession()).SendSysMessage(
							T("Nejsi v guildě.", "You are not in a guild."));
						ShowRoot(player, creature);
						return true;
					}
				
					GuildVillageProduction::SyncGuildProduction(player->GetGuildId());
				
					SendCurrencyStatusToChat(player, player->GetGuildId());
				
					ShowRoot(player, creature);
					return true;
				}
				
				case ACT_SEPARATOR:
				{
					SendGossipMenuFor(player, 1, creature->GetGUID());
					return true;
				}

                case ACT_BACK_CATEGORY:
                {
                    ShowRoot(player, creature);
                    return true;
                }

                case ACT_BACK_TO_CATEGORY:
                {
                    auto it = s_menu.find(player->GetGUID().GetCounter());
                    if (it != s_menu.end())
                        ShowCategory(player, creature, it->second.cat);
                    else
                        ShowRoot(player, creature); // fallback
                    return true;
                }

                default: break;
            }

            // Výběr položky (nákup)
            if (action >= ACT_ITEM_BASE && action < ACT_ITEM_BASE + 2000)
            {
                uint32 idx = action - ACT_ITEM_BASE;
                auto it = s_menu.find(player->GetGUID().GetCounter());
                if (it == s_menu.end() || idx >= it->second.items.size())
                { ShowRoot(player, creature); return true; }

                ShowConfirm(player, creature, it->second.items[idx]);
                return true;
            }

            // Potvrzení nákupu
            if (action >= ACT_CONFIRM_BASE && action < ACT_CONFIRM_BASE + 2000)
            {
                uint32 idx = action - ACT_CONFIRM_BASE;
                auto it = s_menu.find(player->GetGUID().GetCounter());
                if (it == s_menu.end() || idx >= it->second.items.size())
                { ShowRoot(player, creature); return true; }

                CatalogRow const& c = it->second.items[idx];

                // práva
                if (Cfg_PurchaseGMOnly() && !isLeader)
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Zakoupit mohou pouze Guild Master a Zástupce.",
                          "Only the Guild Master and Officers can purchase this."));
                    ShowCategory(player, creature, it->second.cat);
                    return true;
                }

                // nebylo mezitím zakoupeno?
                if (WorldDatabase.Query(
                        "SELECT 1 FROM customs.gv_upgrades WHERE guildId={} AND expansion_key='{}' LIMIT 1",
                        g->GetId(), c.key))
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Už nainstalováno, nákup zrušen.", "Already installed, purchase canceled."));
                    ShowCategory(player, creature, it->second.cat);
                    return true;
                }

                // validace frakce
                if (!HasFactionContent(c.key, factionFilter))
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Tento upgrade není dostupný pro tvou frakci.", "This upgrade is not available for your faction."));
                    ShowCategory(player, creature, it->second.cat);
                    return true;
                }

                // 1) odečet měny
                if (!TryDeductCurrency(g->GetId(), c))
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Nedostatek materiálu.", "Not enough materials."));
                    ShowCategory(player, creature, it->second.cat);
                    return true;
                }

                // 2) instalace
				bool ok = ApplyUpgradeByKey(g->GetId(), phaseId, c.key, factionFilter);
				if (!ok)
				{
					ChatHandler(player->GetSession()).SendSysMessage(
						T("Už nainstalováno, nákup zrušen.", "Already installed, purchase canceled."));
					ShowCategory(player, creature, it->second.cat);
					return true;
				}
				
				ChatHandler(player->GetSession()).SendSysMessage(
					T("Upgrade nainstalován.", "Upgrade installed."));
				
				ReopenCategoryAfterPurchase(player, creature, it->second.cat);
				return true;
            }

            CloseGossipMenuFor(player);
            return true;
        }
    };
} // namespace GuildVillage

// ---------- Registrace ----------
void RegisterGuildVillageUpgrade()
{
    new GuildVillage::npc_gv_upgrades();
}
