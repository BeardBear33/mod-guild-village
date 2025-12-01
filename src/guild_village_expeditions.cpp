// modules/mod-guild-village/src/guild_village_expeditions.cpp

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
#include "gv_names.h"

#include <string>
#include <optional>
#include <algorithm>
#include <cstdint>

namespace GuildVillageExpeditions
{
    // --------------------------
    // Lokalizace
    // --------------------------
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

    static inline char const* SeparatorLine()
    {
        return "|cff808080-------------------------------|r";
    }

    // --------------------------
    // Práva (GM + Officer)
    // --------------------------
    static inline bool IsGuildLeaderOrOfficer(Player* player)
    {
        if (!player)
            return false;

        Guild* g = player->GetGuild();
        if (!g)
            return false;

        // Guild Master
        if (g->GetLeaderGUID() == player->GetGUID())
            return true;

        // Officer
        if (sConfigMgr->GetOption<bool>("GuildVillage.Expeditions.OfficersCanManage", true))
            if (auto m = g->GetMember(player->GetGUID()))
                return m->GetRankId() == 1;

        return false;
    }

    // --------------------------
    // Má guilda vesnici?
    // --------------------------
    static bool GuildHasVillage(uint32 guildId)
    {
        if (QueryResult r = WorldDatabase.Query("SELECT 1 FROM customs.gv_guild WHERE guild={}", guildId))
            return true;
        return false;
    }

    // ==========================
    // DATA GUILDY PRO EXPEDICE
    // ==========================
    struct ExpGuildRow
    {
        uint32 guildId = 0;
        uint8  owned = 0;
        uint8  onMission = 0;
        uint8  maxHeroes = 25;
        uint16 gearLevel = 182; // aktuální iLvl družiny (default baseline 182)
    };

    // vrátí / vytvoří záznam expediční gildy
    static ExpGuildRow GetOrInitGuildRow(uint32 guildId)
    {
        ExpGuildRow out;
        out.guildId = guildId;
        out.owned = 0;
        out.onMission = 0;
        out.maxHeroes = 25;
        out.gearLevel = 182;

        if (QueryResult r = WorldDatabase.Query(
                "SELECT heroes_owned, heroes_on_mission, heroes_max, gear_level "
                "FROM customs.gv_expedition_guild WHERE guildId={}", guildId))
        {
            Field* f = r->Fetch();
            out.owned = f[0].Get<uint8>();
            out.onMission = f[1].Get<uint8>();
            out.maxHeroes = f[2].Get<uint8>();
            out.gearLevel = f[3].Get<uint16>();
            return out;
        }

        WorldDatabase.DirectExecute(
            "INSERT INTO customs.gv_expedition_guild "
            "(guildId, heroes_owned, heroes_on_mission, heroes_max, gear_level, last_update) "
            "VALUES ({}, 0, 0, 25, 182, NOW())",
            guildId);

        return out;
    }

	static inline uint16 DisplayIlvl(uint16 storedIlvl)
	{
		return storedIlvl;
	}

    // ==========================
    // CENÍK HRDINŮ (sloty 1..25)
    // ==========================
    struct HeroCatalogRow
    {
        uint8       slot = 0;
        std::string label_cs;
        std::string label_en;
        uint32      cost_mat1 = 0;
        uint32      cost_mat2 = 0;
        uint32      cost_mat3 = 0;
        uint32      cost_mat4 = 0;
        uint32      cost_gold = 0;
        uint8       successChance = 0;
    };

    static std::optional<HeroCatalogRow> LoadCatalogForSlot(uint8 slot)
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT slot, label_cs, label_en, cost_mat1, cost_mat2, cost_mat3, cost_mat4, cost_gold, successchance "
                "FROM customs.gv_expedition_catalog WHERE slot={} LIMIT 1",
                (uint32)slot))
        {
            Field* f = r->Fetch();
            HeroCatalogRow c;
            c.slot           = f[0].Get<uint8>();
            c.label_cs       = f[1].Get<std::string>();
            c.label_en       = f[2].Get<std::string>();
            c.cost_mat1      = f[3].Get<uint32>();
            c.cost_mat2      = f[4].Get<uint32>();
            c.cost_mat3      = f[5].Get<uint32>();
            c.cost_mat4      = f[6].Get<uint32>();
            c.cost_gold      = f[7].Get<uint32>();
            c.successChance  = f[8].Get<uint8>();
            return c;
        }

        return std::nullopt;
    }

    // ==========================
    // GEAR TIER KATALOG
    // ==========================
    struct GearTierRow
    {
        uint16      tierIlvl;
        std::string label_cs;
        std::string label_en;
        uint32      costMat1;
        uint32      costMat2;
        uint32      costMat3;
        uint32      costMat4;
        uint32      costGold;
    };

    // najít první tier s tier_ilvl > currentTier
    static std::optional<GearTierRow> LoadNextGearTier(uint16 currentTier)
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT tier_ilvl, label_cs, label_en, cost_mat1, cost_mat2, cost_mat3, cost_mat4, cost_gold "
                "FROM customs.gv_expedition_gear_catalog "
                "WHERE enabled=1 AND tier_ilvl > {} "
                "ORDER BY tier_ilvl ASC LIMIT 1",
                (uint32)currentTier))
        {
            Field* f = r->Fetch();
            GearTierRow gt;
            gt.tierIlvl = f[0].Get<uint16>();
            gt.label_cs = f[1].Get<std::string>();
            gt.label_en = f[2].Get<std::string>();
            gt.costMat1 = f[3].Get<uint32>();
            gt.costMat2 = f[4].Get<uint32>();
            gt.costMat3 = f[5].Get<uint32>();
            gt.costMat4 = f[6].Get<uint32>();
            gt.costGold = f[7].Get<uint32>();
            return gt;
        }

        return std::nullopt;
    }

    // ==========================
    // Pomocné výpisy ceny
    // ==========================
    static std::string CostLineHero(HeroCatalogRow const& c)
    {
        std::string mats = GuildVillage::Names::CostLine(
            c.cost_mat1, c.cost_mat2, c.cost_mat3, c.cost_mat4);

        if (c.cost_gold > 0)
        {
            if (!mats.empty())
                mats += " + ";
            mats += Acore::StringFormat("{}g", c.cost_gold);
        }

        if (mats.empty())
            mats = T("zdarma", "free");

        return mats;
    }

    static std::string CostLineGear(GearTierRow const& gt)
    {
        std::string mats = GuildVillage::Names::CostLine(
            gt.costMat1, gt.costMat2, gt.costMat3, gt.costMat4);

        if (gt.costGold > 0)
        {
            if (!mats.empty())
                mats += " + ";
            mats += Acore::StringFormat("{}g", gt.costGold);
        }

        if (mats.empty())
            mats = T("zdarma", "free");

        return mats;
    }

    // ==========================
    // Odečet měny gildy + gold hráče
    // ==========================
    static bool TryDeductGuildCurrencyAndGold(
        uint32 guildId,
        Player* buyer,
        uint32 needMat1,
        uint32 needMat2,
        uint32 needMat3,
        uint32 needMat4,
        uint32 needGold)
    {
        if (!buyer)
            return false;

        // načíst guild currency
        uint64 m1 = 0, m2 = 0, m3 = 0, m4 = 0;
        if (QueryResult q = WorldDatabase.Query(
                "SELECT material1, material2, material3, material4 "
                "FROM customs.gv_currency WHERE guildId={}",
                guildId))
        {
            Field* f = q->Fetch();
            m1 = f[0].Get<uint64>();
            m2 = f[1].Get<uint64>();
            m3 = f[2].Get<uint64>();
            m4 = f[3].Get<uint64>();
        }
        else
        {
            return false;
        }

        // check mats
        if (m1 < needMat1 || m2 < needMat2 || m3 < needMat3 || m4 < needMat4)
            return false;

        // check gold playera (g -> copper)
        uint64 needCopper = (uint64)needGold * 10000ULL;
        if (buyer->GetMoney() < needCopper)
            return false;

        // odečíst currency
        WorldDatabase.DirectExecute(
            "UPDATE customs.gv_currency SET "
            "material1 = material1 - {}, "
            "material2 = material2 - {}, "
            "material3 = material3 - {}, "
            "material4 = material4 - {}, "
            "last_update = NOW() "
            "WHERE guildId = {}",
            needMat1, needMat2, needMat3, needMat4, guildId);

        // odečíst goldy
        if (needCopper > 0)
            buyer->ModifyMoney(-(int64)needCopper);

        return true;
    }

    // ==========================
    // Gossip akce
    // ==========================
    enum GossipAction : uint32
    {
        ACT_ROOT               = 40000,

        // submenu pro najmout hrdinu
        ACT_HIRE_MENU          = 40100,
        ACT_HIRE_BUY           = 40101,

        // submenu gear upgrade
        ACT_GEAR_MENU          = 40200,
        ACT_GEAR_BUY           = 40210,

        ACT_BUY_NEXT_HERO_BASE = 40500
    };

    // ==========================
    // Forward deklarace
    // ==========================
    static void ShowRoot(Player* player, Creature* creature);
    static void ShowGearMenu(Player* player, Creature* creature);
    static void ShowHireMenu(Player* player, Creature* creature);
    static void TryBuyNextHero(Player* player, Creature* creature);
    static void TryBuyNextGear(Player* player, Creature* creature);

    // ==========================
    // Hlavní menu NPC
    // ==========================
    static void ShowRoot(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        Guild* g = player->GetGuild();
        if (!g)
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Nejsi v guildě.", "You are not in a guild."));
            SendGossipMenuFor(player, 1, creature->GetGUID());
            return;
        }

        if (sConfigMgr->GetOption<bool>("GuildVillage.Expeditions.RequireVillage", true))
        {
            if (!GuildHasVillage(g->GetId()))
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Tvoje guilda nevlastní vesnici.", "Your guild does not own a village."));
                SendGossipMenuFor(player, 1, creature->GetGUID());
                return;
            }
        }

        ExpGuildRow row = GetOrInitGuildRow(g->GetId());
        uint16 shownIlvl = DisplayIlvl(row.gearLevel);

        // status řádky
        {
            // 1) Hrdinové
            std::string heroesLine = Acore::StringFormat(
                T("Hrdinové: {}/{} (na expedici: {})",
                  "Heroes: {}/{} (on mission: {})"),
                (uint32)row.owned,
                (uint32)row.maxHeroes,
                (uint32)row.onMission
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                heroesLine,
                GOSSIP_SENDER_MAIN,
                ACT_ROOT);
        }
			
		if (row.owned > 0)
        {
            // 2) Výbava
            std::string gearLine = Acore::StringFormat(
                T("Výbava družiny iLvl {}",
                  "Party gear iLvl {}"),
                (uint32)shownIlvl
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                gearLine,
                GOSSIP_SENDER_MAIN,
                ACT_ROOT);
        }

        // separator
        AddGossipItemFor(
            player,
            0,
            SeparatorLine(),
            GOSSIP_SENDER_MAIN,
            ACT_ROOT
        );

        bool canManage = IsGuildLeaderOrOfficer(player);

        // --- Najmout dalšího hrdinu (otevře samostatné submenu) ---
        if (canManage && row.owned < row.maxHeroes)
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_MONEY_BAG,
                T("Najmout hrdinu", "Hire hero"),
                GOSSIP_SENDER_MAIN,
                ACT_HIRE_MENU);
        }

        // --- Vylepšit výbavu družiny (otevře submenu gear) ---
        if (canManage && row.owned > 0)
        {
            auto nextTierOpt = LoadNextGearTier(row.gearLevel);
            if (nextTierOpt)
            {
                std::string line = T("Vylepšit výbavu družiny", "Upgrade party gear");
                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_VENDOR,
                    line,
                    GOSSIP_SENDER_MAIN,
                    ACT_GEAR_MENU);
            }
        }

        SendGossipMenuFor(player, 1, creature->GetGUID());
    }

    // ==========================
    // Submenu pro nábor nového hrdiny
    // ==========================
    static void ShowHireMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        Guild* g = player->GetGuild();
        if (!g)
        {
            SendGossipMenuFor(player, 1, creature->GetGUID());
            return;
        }

        if (!IsGuildLeaderOrOfficer(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Nákup mohou provádět pouze Guild Master a Zástupce.",
                  "Only Guild Master and Officers can purchase."));
            ShowRoot(player, creature);
            return;
        }

        ExpGuildRow row = GetOrInitGuildRow(g->GetId());

        // kontrola kapacity
        if (row.owned >= row.maxHeroes)
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Dosažen maximální počet hrdinů.",
                  "Maximum number of heroes reached."));
            ShowRoot(player, creature);
            return;
        }

        uint8 nextSlot = row.owned + 1;
        auto catOpt = LoadCatalogForSlot(nextSlot);
        if (!catOpt)
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Ceník pro dalšího hrdinu není dostupný.",
                  "Catalog entry for the next hero is not available."));
            ShowRoot(player, creature);
            return;
        }

        HeroCatalogRow const& c = *catOpt;

        // 1) cena (víceřádková)
		{
			std::string pretty = CostLineHero(c);
		
			if (!pretty.empty())
			{
				std::string multi = pretty;
				std::string::size_type pos = 0;
				while ((pos = multi.find(" + ", pos)) != std::string::npos)
				{
					multi.replace(pos, 3, "\n");
				}
				std::string costBlock = T("Cena:", "Cost:");
				costBlock += "\n";
				costBlock += multi;
		
				AddGossipItemFor(
					player,
					GOSSIP_ICON_MONEY_BAG,
					costBlock,
					GOSSIP_SENDER_MAIN,
					ACT_HIRE_MENU);
			}
		}

        // 2) separator
        AddGossipItemFor(
            player,
            0,
            SeparatorLine(),
            GOSSIP_SENDER_MAIN,
            ACT_HIRE_MENU
        );

        // 3) potvrdit nákup
        AddGossipItemFor(
            player,
            GOSSIP_ICON_TRAINER,
            T("Potvrdit nákup", "Confirm purchase"),
            GOSSIP_SENDER_MAIN,
            ACT_HIRE_BUY);

        // 4) zpátky
        AddGossipItemFor(
            player,
            GOSSIP_ICON_TAXI,
            T("Zpátky", "Back"),
            GOSSIP_SENDER_MAIN,
            ACT_ROOT);

        SendGossipMenuFor(player, 1, creature->GetGUID());
    }

    // ==========================
    // Submenu pro gear upgrade
    // ==========================
    static void ShowGearMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        Guild* g = player->GetGuild();
        if (!g)
        {
            SendGossipMenuFor(player, 1, creature->GetGUID());
            return;
        }

        ExpGuildRow row = GetOrInitGuildRow(g->GetId());
        auto nextTierOpt = LoadNextGearTier(row.gearLevel);

        if (!nextTierOpt)
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Žádný další upgrade výbavy není dostupný.",
                  "No further gear upgrade available."));
            ShowRoot(player, creature);
            return;
        }

        auto const& gt = *nextTierOpt;

        uint16 curIlvl = DisplayIlvl(row.gearLevel);
        uint16 nextIlvl = gt.tierIlvl;

        // info řádek: "iLvl 182 -> iLvl 200"
        {
            std::string hdr = Acore::StringFormat(
                T("Vybavení družiny iLvl {} -> iLvl {}",
                  "Party gear iLvl {} -> iLvl {}"),
                (uint32)curIlvl,
                (uint32)nextIlvl);

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                hdr,
                GOSSIP_SENDER_MAIN,
                ACT_GEAR_MENU);
        }

		// cena (víceřádková)
		{
			std::string pretty = CostLineGear(gt);
		
			if (!pretty.empty())
			{
				std::string multi = pretty;
				std::string::size_type pos = 0;
				while ((pos = multi.find(" + ", pos)) != std::string::npos)
				{
					multi.replace(pos, 3, "\n");
				}
		
				std::string costBlock = T("Cena:", "Cost:");
				costBlock += "\n";
				costBlock += multi;
		
				AddGossipItemFor(
					player,
					GOSSIP_ICON_MONEY_BAG,
					costBlock,
					GOSSIP_SENDER_MAIN,
					ACT_GEAR_MENU);
			}
		}

        // separator před potvrzením nákupu
        AddGossipItemFor(
            player,
            0,
            SeparatorLine(),
            GOSSIP_SENDER_MAIN,
            ACT_GEAR_MENU);

        // potvrdit nákup
        AddGossipItemFor(
            player,
            GOSSIP_ICON_TRAINER,
            T("Potvrdit nákup", "Confirm purchase"),
            GOSSIP_SENDER_MAIN,
            ACT_GEAR_BUY);

        // zpět
        AddGossipItemFor(
            player,
            GOSSIP_ICON_TAXI,
            T("Zpátky", "Back"),
            GOSSIP_SENDER_MAIN,
            ACT_ROOT);

        SendGossipMenuFor(player, 1, creature->GetGUID());
    }

    // ==========================
    // Pokus koupit dalšího hrdinu
    // ==========================
    static void TryBuyNextHero(Player* player, Creature* creature)
    {
        Guild* g = player->GetGuild();
        if (!g)
        {
            CloseGossipMenuFor(player);
            return;
        }

        if (!IsGuildLeaderOrOfficer(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Nákup mohou provádět pouze Guild Master a Zástupce.",
                  "Only Guild Master and Officers can purchase."));
            ShowRoot(player, creature);
            return;
        }

        ExpGuildRow row = GetOrInitGuildRow(g->GetId());
        if (row.owned >= row.maxHeroes)
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Dosažen maximální počet hrdinů.",
                  "Maximum number of heroes reached."));
            ShowRoot(player, creature);
            return;
        }

        uint8 expectedSlot = row.owned + 1;

        auto catOpt = LoadCatalogForSlot(expectedSlot);
        if (!catOpt)
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Ceník pro dalšího hrdinu není dostupný.",
                  "Catalog entry for the next hero is not available."));
            ShowRoot(player, creature);
            return;
        }

        HeroCatalogRow const& c = *catOpt;

        // odečíst mats + gold
        if (!TryDeductGuildCurrencyAndGold(
                g->GetId(),
                player,
                c.cost_mat1,
                c.cost_mat2,
                c.cost_mat3,
                c.cost_mat4,
                c.cost_gold))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Nedostatek materiálu nebo zlata.",
                  "Not enough materials or gold."));
            ShowHireMenu(player, creature);
            return;
        }

        // success -> heroes_owned++
        WorldDatabase.DirectExecute(
            "UPDATE customs.gv_expedition_guild "
            "SET heroes_owned = LEAST(heroes_owned + 1, heroes_max), last_update = NOW() "
            "WHERE guildId={}",
            g->GetId());

        if (sConfigMgr->GetOption<bool>("GuildVillage.Expeditions.Notify", true))
        {
            std::string label = (LangOpt() == Lang::EN) ? c.label_en : c.label_cs;
            ChatHandler(player->GetSession()).SendSysMessage(
                Acore::StringFormat(
                    T("Hrdina {} se přidal do tvých řad.", "The hero {} has joined your ranks"),
                    label).c_str());
        }

        // po potvrzení nákupu se vrátit do hlavního gossip menu
        ShowRoot(player, creature);
    }

    // ==========================
    // Pokus koupit další gear tier
    // ==========================
    static void TryBuyNextGear(Player* player, Creature* creature)
    {
        Guild* g = player->GetGuild();
        if (!g)
        {
            CloseGossipMenuFor(player);
            return;
        }

        if (!IsGuildLeaderOrOfficer(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Nákup mohou provádět pouze Guild Master a Zástupce.",
                  "Only Guild Master and Officers can purchase."));
            ShowRoot(player, creature);
            return;
        }

        ExpGuildRow row = GetOrInitGuildRow(g->GetId());
        auto nextTierOpt = LoadNextGearTier(row.gearLevel);
        if (!nextTierOpt)
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Žádný další upgrade není dostupný.",
                  "No further upgrade is available."));
            ShowRoot(player, creature);
            return;
        }

        GearTierRow const& gt = *nextTierOpt;

        // strhnout mats + gold
        if (!TryDeductGuildCurrencyAndGold(
                g->GetId(),
                player,
                gt.costMat1,
                gt.costMat2,
                gt.costMat3,
                gt.costMat4,
                gt.costGold))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Nedostatek materiálu nebo zlata.",
                  "Not enough materials or gold."));
            ShowGearMenu(player, creature);
            return;
        }

        // success -> posunout gear_level
        WorldDatabase.DirectExecute(
            "UPDATE customs.gv_expedition_guild "
            "SET gear_level = {}, last_update = NOW() "
            "WHERE guildId={}",
            (uint32)gt.tierIlvl,
            g->GetId());

        ChatHandler(player->GetSession()).SendSysMessage(
            T("Výbava družiny vylepšena!", "Party gear upgraded!"));

        // po potvrzení nákupu se vrátit do hlavního gossip menu
        ShowRoot(player, creature);
    }

    // ==========================
    // NPC
    // ==========================
    class npc_gv_expeditions : public CreatureScript
    {
    public:
        npc_gv_expeditions() : CreatureScript("npc_gv_expeditions") { }

        bool OnGossipHello(Player* player, Creature* creature) override
        {
            if (!sConfigMgr->GetOption<bool>("GuildVillage.Expeditions.Enable", true))
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Expedice jsou dočasně vypnuté.", "Expeditions are temporarily disabled."));
                return true;
            }

            Guild* g = player->GetGuild();
            if (!g)
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Nejsi v guildě.", "You are not in a guild."));
                return true;
            }

            if (sConfigMgr->GetOption<bool>("GuildVillage.Expeditions.RequireVillage", true))
            {
                if (!GuildHasVillage(g->GetId()))
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Tvoje guilda nevlastní vesnici.", "Your guild does not own a village."));
                    return true;
                }
            }

            ShowRoot(player, creature);
            return true;
        }

        bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
        {
            if (sender != GOSSIP_SENDER_MAIN)
                return false;

            // zpět / refresh root
            if (action == ACT_ROOT)
            {
                ShowRoot(player, creature);
                return true;
            }

            // otevřít submenu "Najmout dalšího hrdinu"
            if (action == ACT_HIRE_MENU)
            {
                ShowHireMenu(player, creature);
                return true;
            }

            // potvrdit nákup hrdiny
            if (action == ACT_HIRE_BUY)
            {
                TryBuyNextHero(player, creature);
                return true;
            }

            // otevřít submenu gear
            if (action == ACT_GEAR_MENU)
            {
                ShowGearMenu(player, creature);
                return true;
            }

            // potvrzení gear upgradu
            if (action == ACT_GEAR_BUY)
            {
                TryBuyNextGear(player, creature);
                return true;
            }

            // fallback
            ShowRoot(player, creature);
            return true;
        }
    };
}

// Registrace
void RegisterGuildVillageExpeditions()
{
    new GuildVillageExpeditions::npc_gv_expeditions();
}
