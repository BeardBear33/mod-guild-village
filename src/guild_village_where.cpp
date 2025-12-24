// modules/mod-guild-village/src/guild_village_where.cpp

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

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>
#include <algorithm>
#include <cctype>

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

    // ---------- phase pro guildu (pro kontrolu, že vesnici má) ----------
    static std::optional<uint32> LoadVillagePhase(uint32 guildId)
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT phase FROM customs.gv_guild WHERE guild={}", guildId))
            return (*r)[0].Get<uint32>();
        return std::nullopt;
    }

    // ---------- Katalog položek ----------
    enum class Cat : uint8 { Trainers=1, Professions, Vendor, Portal, Objects, Others };

    struct CatalogRow
    {
        uint32 id;
        Cat    cat;
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

    // ---------- Pomocné: převod category string -> Cat ----------
    static std::optional<Cat> CatFromString(std::string const& s)
    {
        if (s == "trainers")     return Cat::Trainers;
        if (s == "professions")  return Cat::Professions;
        if (s == "vendor")       return Cat::Vendor;
        if (s == "portal")       return Cat::Portal;
        if (s == "objects")      return Cat::Objects;
        if (s == "others")       return Cat::Others;
        return std::nullopt;
    }

    // ---------- Načtení katalogové položky podle ID ----------
    static std::optional<CatalogRow> LoadCatalogRowById(uint32 id)
    {
        if (QueryResult r = WorldDatabase.Query(
            "SELECT id, category, expansion_key, label_cs, label_en, cost_material1, cost_material2, cost_material3, cost_material4, sort_order "
            "FROM customs.gv_upgrade_catalog WHERE id={}", id))
        {
            Field* f = r->Fetch();

            std::string catStr = f[1].Get<std::string>();
            std::transform(catStr.begin(), catStr.end(), catStr.begin(), ::tolower);

            auto catOpt = CatFromString(catStr);
            if (!catOpt)
                return std::nullopt;

            CatalogRow c;
            c.id        = f[0].Get<uint32>();
            c.cat       = *catOpt;
            c.key       = f[2].Get<std::string>();
            c.label_cs  = f[3].Get<std::string>();
            c.label_en  = f[4].Get<std::string>();
            c.cost_mat1 = f[5].Get<uint32>();
            c.cost_mat2 = f[6].Get<uint32>();
            c.cost_mat3 = f[7].Get<uint32>();
            c.cost_mat4 = f[8].Get<uint32>();
            c.sort      = f[9].Get<uint8>();
            return c;
        }
        return std::nullopt;
    }
	
    // ---------- POI podpora ----------
    struct PoiRow
    {
        uint32 map; float x, y;
        uint32 icon, flags;
        uint32 poiId;             // pokud je 0, poslat fallback zprávu
        std::string name_cs, name_en;
    };

    static std::optional<PoiRow> LoadPoi(std::string const& key, uint8 factionFilter)
    {
        if (QueryResult r = WorldDatabase.Query(
            "SELECT map, pos_x, pos_y, icon, flags, poi_id, name_cs, name_en "
            "FROM customs.gv_upgrade_poi WHERE expansion_key='{}' AND faction={}",
            key, (uint32)factionFilter))
        {
            Field* f = r->Fetch(); PoiRow p;
            p.map=f[0].Get<uint32>(); p.x=f[1].Get<float>(); p.y=f[2].Get<float>();
            p.icon=f[3].Get<uint32>(); p.flags=f[4].Get<uint32>();
            p.poiId=f[5].Get<uint32>();
            p.name_cs=f[6].Get<std::string>(); p.name_en=f[7].Get<std::string>();
            return p;
        }
        if (QueryResult r = WorldDatabase.Query(
            "SELECT map, pos_x, pos_y, icon, flags, poi_id, name_cs, name_en "
            "FROM customs.gv_upgrade_poi WHERE expansion_key='{}' AND faction=0",
            key))
        {
            Field* f = r->Fetch(); PoiRow p;
            p.map=f[0].Get<uint32>(); p.x=f[1].Get<float>(); p.y=f[2].Get<float>();
            p.icon=f[3].Get<uint32>(); p.flags=f[4].Get<uint32>();
            p.poiId=f[5].Get<uint32>();
            p.name_cs=f[6].Get<std::string>(); p.name_en=f[7].Get<std::string>();
            return p;
        }
        return std::nullopt;
    }

    // ---------- Zakoupené klíče pro guildu ----------
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

    // ---------- „Kde se nachází?“ UI ----------
    enum GossipAction : uint32
    {
        ACT_BACK_CATEGORY   = 9000,
        ACT_WHERE_ROOT      = 20000,
        ACT_WHERE_TRAINERS,
        ACT_WHERE_PROFESSIONS,
        ACT_WHERE_VENDOR,
        ACT_WHERE_PORTAL,
        ACT_WHERE_OBJECTS,
        ACT_WHERE_OTHERS,

        ACT_WHERE_ITEM_BASE = 21000
    };

	static void ShowWhereRoot(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        AddGossipItemFor(player, 0, T("|cff00ff00Vyber si koho chceš najít:|r", "|cff00ff00Select who you want to find:|r"),
            GOSSIP_SENDER_MAIN, ACT_WHERE_ROOT);
        AddGossipItemFor(player, 0, "|cff808080---------------------------|r",
            GOSSIP_SENDER_MAIN, ACT_WHERE_ROOT);

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE,     T("Trenér", "Trainers"),      GOSSIP_SENDER_MAIN, ACT_WHERE_TRAINERS);
        AddGossipItemFor(player, GOSSIP_ICON_TRAINER,    T("Profese", "Professions"),  GOSSIP_SENDER_MAIN, ACT_WHERE_PROFESSIONS);
        AddGossipItemFor(player, GOSSIP_ICON_VENDOR,     T("Vendor", "Vendors"),       GOSSIP_SENDER_MAIN, ACT_WHERE_VENDOR);
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, T("Portály", "Portal"),       GOSSIP_SENDER_MAIN, ACT_WHERE_PORTAL);
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, T("Objekty", "Objects"),      GOSSIP_SENDER_MAIN, ACT_WHERE_OBJECTS);
        AddGossipItemFor(player, GOSSIP_ICON_TABARD,     T("Ostatní", "Others"),       GOSSIP_SENDER_MAIN, ACT_WHERE_OTHERS);

        SendGossipMenuFor(player, 1, creature->GetGUID());
    }
	
	static void ShowWhereCategory(Player* player, Creature* creature, Cat cat, uint8 factionFilter)
    {
        ClearGossipMenuFor(player);

        Guild* g = player->GetGuild();
        if (!g) { SendGossipMenuFor(player, 1, creature->GetGUID()); return; }

        auto purchased = LoadPurchasedKeys(g->GetId());
        if (purchased.empty())
        {
            ChatHandler(player->GetSession()).SendSysMessage(T("Zatím nic zakoupeno.", "Nothing purchased yet."));
            ShowWhereRoot(player, creature);
            return;
        }

        auto all = LoadCatalog(cat);
        std::vector<CatalogRow> list;
        list.reserve(all.size());

        for (auto const& c : all)
        {
            if (purchased.find(c.key) == purchased.end())
                continue;

            if (!LoadPoi(c.key, factionFilter).has_value())
                continue;

            list.push_back(c);
        }

        if (list.empty())
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("V této kategorii nemáš nic zakoupeno.", "You have no purchased items in this category."));
            ShowWhereRoot(player, creature);
            return;
        }

        for (auto const& c : list)
        {
            std::string label = (LangOpt() == Lang::EN ? c.label_en : c.label_cs);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, label, GOSSIP_SENDER_MAIN, ACT_WHERE_ITEM_BASE + c.id);
        }

        AddGossipItemFor(player, GOSSIP_ICON_TAXI, T("Zpět", "Back"), GOSSIP_SENDER_MAIN, ACT_WHERE_ROOT);
        SendGossipMenuFor(player, 1, creature->GetGUID());
    }

    // ---------- NPC skript jen pro „Where is it?“ ----------
    class npc_gv_where : public CreatureScript
    {
    public:
        npc_gv_where() : CreatureScript("npc_gv_where") { }

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

            ChatHandler(player->GetSession()).SendSysMessage(T("Navigace vesnice (Where is it?).", "Village navigation (Where is it?)."));
            ShowWhereRoot(player, creature);
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

            // frakce pro šablony/POI: 1=Alliance, 2=Horde
            uint8 factionFilter = (player->GetTeamId() == TEAM_ALLIANCE) ? 1 : 2;

            switch (action)
            {
                case ACT_BACK_CATEGORY:     CloseGossipMenuFor(player); return true;

                case ACT_WHERE_ROOT:        ShowWhereRoot(player, creature);                                return true;
                case ACT_WHERE_TRAINERS:    ShowWhereCategory(player, creature, Cat::Trainers,    factionFilter); return true;
                case ACT_WHERE_PROFESSIONS: ShowWhereCategory(player, creature, Cat::Professions, factionFilter); return true;
                case ACT_WHERE_VENDOR:      ShowWhereCategory(player, creature, Cat::Vendor,      factionFilter); return true;
                case ACT_WHERE_PORTAL:      ShowWhereCategory(player, creature, Cat::Portal,      factionFilter); return true;
                case ACT_WHERE_OBJECTS:     ShowWhereCategory(player, creature, Cat::Objects,     factionFilter); return true;
                case ACT_WHERE_OTHERS:      ShowWhereCategory(player, creature, Cat::Others,      factionFilter); return true;
                default: break;
            }

            // Klik na konkrétní položku – podle ID v gv_upgrade_catalog (bez menustate cache)
            if (action >= ACT_WHERE_ITEM_BASE && action < ACT_WHERE_ITEM_BASE + 1000000)
            {
                uint32 catalogId = action - ACT_WHERE_ITEM_BASE;

                Guild* g2 = player->GetGuild();
                if (!g2) { CloseGossipMenuFor(player); return true; }

                auto rowOpt = LoadCatalogRowById(catalogId);
                if (!rowOpt)
                {
                    ShowWhereRoot(player, creature);
                    return true;
                }

                CatalogRow const& c = *rowOpt;

                // Musí být zakoupeno (ochrana proti “starému” / ručně poslanému actionu)
                auto purchased = LoadPurchasedKeys(g2->GetId());
                if (purchased.find(c.key) == purchased.end())
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Tato položka není zakoupena.", "This item is not purchased."));
                    ShowWhereCategory(player, creature, c.cat, factionFilter);
                    return true;
                }

                auto poiOpt = LoadPoi(c.key, factionFilter);
                if (!poiOpt)
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Pro tuto položku není definován POI.", "No POI defined for this item."));
                    ShowWhereCategory(player, creature, c.cat, factionFilter);
                    return true;
                }

                auto const& poi = *poiOpt;

                if (player->GetMapId() == poi.map && poi.poiId != 0)
                {
                    player->PlayerTalkClass->SendPointOfInterest(poi.poiId);
                }
                else
                {
                    std::string msg = (LangOpt() == Lang::EN)
                        ? Acore::StringFormat("Target is on map {} (X: {:.1f}, Y: {:.1f})", poi.map, poi.x, poi.y)
                        : Acore::StringFormat("Cíl je na mapě {} (X: {:.1f}, Y: {:.1f})", poi.map, poi.x, poi.y);

                    ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
                }

                ShowWhereCategory(player, creature, c.cat, factionFilter);
                return true;
            }

            CloseGossipMenuFor(player);
            return true;
        }
    };
} // namespace GuildVillage

// ---------- Registrace ----------
void RegisterGuildVillageWhere()
{
    new GuildVillage::npc_gv_where();
}
