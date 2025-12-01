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
#include "GameTime.h"
#include "EventProcessor.h"

#include "gv_production.h"
#include "gv_names.h"

#include <string>
#include <vector>
#include <optional>
#include <algorithm>
#include <cmath>

namespace GuildVillageProduction
{
    // =========================
    // Lokalizace
    // =========================

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

    // =========================
    // Práva guildy
    // =========================

    static inline bool IsGuildLeader(Player* player)
    {
        if (Guild* g = player->GetGuild())
        {
            if (g->GetLeaderGUID() == player->GetGUID())
                return true; // Guild Master

            if (auto m = g->GetMember(player->GetGUID()))
                return m->GetRankId() == 1; // Officer
        }
        return false;
    }

    // =========================
    // Má guilda vesnici?
    // =========================

    static std::optional<uint32> LoadVillagePhase(uint32 guildId)
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT phase FROM customs.gv_guild WHERE guild={}", guildId))
        {
            return (*r)[0].Get<uint32>();
        }
        return std::nullopt;
    }

    // =========================
    // Config helpers
    // =========================

    static inline uint32 BaseAmount()
    {
        return sConfigMgr->GetOption<uint32>("GuildVillage.Production.BaseAmount", 2);
    }

    static inline float BasePeriodHours()
    {
        return sConfigMgr->GetOption<float>("GuildVillage.Production.BasePeriodHours", 3.0f);
    }

    static inline uint32 AmountBonusForRank(uint8 rank)
    {
        switch (rank)
        {
            case 1:
                return sConfigMgr->GetOption<uint32>("GuildVillage.Production.AmountBonus1", 1);
            case 2:
                return sConfigMgr->GetOption<uint32>("GuildVillage.Production.AmountBonus2", 3);
            case 3:
                return sConfigMgr->GetOption<uint32>("GuildVillage.Production.AmountBonus3", 6);
            default:
                return 0;
        }
    }

    static inline float SpeedMultForRank(uint8 rank)
    {
        switch (rank)
        {
            case 1:
                return sConfigMgr->GetOption<float>("GuildVillage.Production.SpeedMult1", 0.75f);
            case 2:
                return sConfigMgr->GetOption<float>("GuildVillage.Production.SpeedMult2", 0.50f);
            case 3:
                return sConfigMgr->GetOption<float>("GuildVillage.Production.SpeedMult3", 0.25f);
            default:
                return 1.0f;
        }
    }

    static inline uint32 CapMaterial1()
    {
        return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material1", 1000);
    }

    static inline uint32 CapMaterial2()
    {
        return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material2", 1000);
    }

    static inline uint32 CapMaterial3()
    {
        return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material3", 1000);
    }

    static inline uint32 CapMaterial4()
    {
        return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material4", 1000);
    }

    static inline uint32 WorldTickMinutes()
    {
        return sConfigMgr->GetOption<uint32>("GuildVillage.Production.WorldTickMinutes", 15);
    }

    // =========================
    // DB struktury
    // =========================

    struct UpgradeRanks
    {
        uint8 amountRank = 0;
        uint8 speedRank  = 0;
    };

    static UpgradeRanks GetOrInitUpgradeRanks(uint32 guildId, uint8 materialId)
    {
        UpgradeRanks out;

        if (QueryResult r = WorldDatabase.Query(
                "SELECT amount_rank, speed_rank "
                "FROM customs.gv_production_upgrade "
                "WHERE guildId={} AND material_id={}",
                guildId, (uint32)materialId))
        {
            Field* f = r->Fetch();
            out.amountRank = f[0].Get<uint8>();
            out.speedRank  = f[1].Get<uint8>();
        }
        else
        {
            WorldDatabase.Execute(
                "INSERT INTO customs.gv_production_upgrade "
                "(guildId, material_id, amount_rank, speed_rank) "
                "VALUES ({}, {}, 0, 0)",
                guildId, (uint32)materialId);

            out.amountRank = 0;
            out.speedRank  = 0;
        }

        return out;
    }

    struct ActiveProductionRow
    {
        uint32 guildId;
        uint8  materialId;
        uint32 startedAt;
        uint32 lastTick;
    };

    static std::vector<ActiveProductionRow> LoadActiveProduction(uint32 guildId)
    {
        std::vector<ActiveProductionRow> out;

        if (QueryResult r = WorldDatabase.Query(
                "SELECT guildId, material_id, started_at, last_tick "
                "FROM customs.gv_production_active "
                "WHERE guildId={}",
                guildId))
        {
            do
            {
                Field* f = r->Fetch();
                ActiveProductionRow row;
                row.guildId    = f[0].Get<uint32>();
                row.materialId = f[1].Get<uint8>();
                row.startedAt  = f[2].Get<uint32>();
                row.lastTick   = f[3].Get<uint32>();
                out.push_back(row);
            }
            while (r->NextRow());
        }

        return out;
    }

    static bool IsMaterialActive(uint32 guildId, uint8 materialId)
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT 1 FROM customs.gv_production_active "
                "WHERE guildId={} AND material_id={} LIMIT 1",
                guildId, (uint32)materialId))
        {
            return true;
        }

        return false;
    }

    static void StartMaterial(uint32 guildId, uint8 materialId)
    {
        uint32 now = (uint32)GameTime::GetGameTime().count();

        if (!IsMaterialActive(guildId, materialId))
        {
            WorldDatabase.Execute(
                "INSERT INTO customs.gv_production_active "
                "(guildId, material_id, started_at, last_tick) "
                "VALUES ({}, {}, {}, {})",
                guildId, (uint32)materialId, now, now);
        }
    }

    static void StopMaterial(uint32 guildId, uint8 materialId)
    {
        WorldDatabase.DirectExecute(
            "DELETE FROM customs.gv_production_active "
            "WHERE guildId={} AND material_id={}",
            guildId, (uint32)materialId);
    }

    struct CatalogProdRow
    {
        uint32      id;
        uint8       materialId;   // 1..4
        uint8       upgradeType;  // 1=amount, 2=speed
        uint8       rank;         // 1..3
        std::string label_cs;
        std::string label_en;
        uint32      cost_mat1;
        uint32      cost_mat2;
        uint32      cost_mat3;
        uint32      cost_mat4;
    };

    static std::optional<CatalogProdRow> LoadProductionCatalog(uint8 materialId, uint8 upgradeType, uint8 rankToBuy)
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT id, material_id, upgrade_type, `rank`, label_cs, label_en, "
                "cost_material1, cost_material2, cost_material3, cost_material4 "
                "FROM customs.gv_production_catalog "
                "WHERE material_id={} AND upgrade_type={} AND `rank`={} "
                "LIMIT 1",
                (uint32)materialId, (uint32)upgradeType, (uint32)rankToBuy))
        {
            Field* f = r->Fetch();

            CatalogProdRow c;
            c.id          = f[0].Get<uint32>();
            c.materialId  = f[1].Get<uint8>();
            c.upgradeType = f[2].Get<uint8>();
            c.rank        = f[3].Get<uint8>();
            c.label_cs    = f[4].Get<std::string>();
            c.label_en    = f[5].Get<std::string>();
            c.cost_mat1   = f[6].Get<uint32>();
            c.cost_mat2   = f[7].Get<uint32>();
            c.cost_mat3   = f[8].Get<uint32>();
            c.cost_mat4   = f[9].Get<uint32>();

            return c;
        }

        return std::nullopt;
    }

    static std::string CostLineProd(CatalogProdRow const& c)
    {
        return GuildVillage::Names::CostLine(
            c.cost_mat1, c.cost_mat2, c.cost_mat3, c.cost_mat4);
    }

    static std::optional<GuildCurrency> LoadGuildCurrencyNow(uint32 guildId)
    {
        if (QueryResult res = WorldDatabase.Query(
                "SELECT material1, material2, material3, material4 "
                "FROM customs.gv_currency WHERE guildId={}",
                guildId))
        {
            Field* f = res->Fetch();
            GuildCurrency c;
            c.material1 = f[0].Get<uint64>();
            c.material2 = f[1].Get<uint64>();
            c.material3 = f[2].Get<uint64>();
            c.material4 = f[3].Get<uint64>();
            return c;
        }

        return std::nullopt;
    }

    // =========================
    // Měna / cap
    // =========================

    static bool AddCurrencyWithCap(uint32 guildId, uint8 materialId, uint32 amountToAdd)
    {
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

        auto applyCapRet = [&](uint64 cur, uint32 add, uint32 cap, bool& hitCapOut) -> uint64
        {
            if (cap == 0)
            {
                hitCapOut = false;
                return cur + add;
            }

            uint64 wanted = cur + add;
            if (wanted >= cap)
            {
                hitCapOut = true;
                return cap;
            }

            hitCapOut = false;
            return wanted;
        };

        bool hitCapMat = false;

        switch (materialId)
        {
            case 1:
            {
                bool hc = false;
                uint64 newVal = applyCapRet(m1, amountToAdd, CapMaterial1(), hc);
                m1 = newVal;
                hitCapMat = hc;
                break;
            }
            case 2:
            {
                bool hc = false;
                uint64 newVal = applyCapRet(m2, amountToAdd, CapMaterial2(), hc);
                m2 = newVal;
                hitCapMat = hc;
                break;
            }
            case 3:
            {
                bool hc = false;
                uint64 newVal = applyCapRet(m3, amountToAdd, CapMaterial3(), hc);
                m3 = newVal;
                hitCapMat = hc;
                break;
            }
            case 4:
            {
                bool hc = false;
                uint64 newVal = applyCapRet(m4, amountToAdd, CapMaterial4(), hc);
                m4 = newVal;
                hitCapMat = hc;
                break;
            }
            default:
                break;
        }

        WorldDatabase.DirectExecute(
            "UPDATE customs.gv_currency "
            "SET material1={}, material2={}, material3={}, material4={}, last_update=NOW() "
            "WHERE guildId={}",
            (uint64)m1, (uint64)m2, (uint64)m3, (uint64)m4, guildId);

        return hitCapMat;
    }

    static bool TryDeductCurrencyForProdUpgrade(uint32 guildId, CatalogProdRow const& c)
    {
        uint32 mat1 = 0, mat2 = 0, mat3 = 0, mat4 = 0;

        if (QueryResult q = WorldDatabase.Query(
                "SELECT material1, material2, material3, material4 "
                "FROM customs.gv_currency WHERE guildId={}",
                guildId))
        {
            Field* f = q->Fetch();
            mat1 = f[0].Get<uint32>();
            mat2 = f[1].Get<uint32>();
            mat3 = f[2].Get<uint32>();
            mat4 = f[3].Get<uint32>();
        }
        else
        {
            return false;
        }

        if (mat1 < c.cost_mat1 ||
            mat2 < c.cost_mat2 ||
            mat3 < c.cost_mat3 ||
            mat4 < c.cost_mat4)
        {
            return false;
        }

        WorldDatabase.Execute(
            "UPDATE customs.gv_currency SET "
            "material1 = material1 - {}, "
            "material2 = material2 - {}, "
            "material3 = material3 - {}, "
            "material4 = material4 - {}, "
            "last_update = NOW() "
            "WHERE guildId = {}",
            c.cost_mat1, c.cost_mat2, c.cost_mat3, c.cost_mat4, guildId);

        return true;
    }

    // =========================
    // Lazy tick výroby
    // =========================

    static void ProcessTicksForGuild(uint32 guildId)
    {
        uint32 now = (uint32)GameTime::GetGameTime().count();

        auto rows = LoadActiveProduction(guildId);
        for (auto& row : rows)
        {
            UpgradeRanks ur = GetOrInitUpgradeRanks(guildId, row.materialId);

            uint32 finalAmount = BaseAmount() + AmountBonusForRank(ur.amountRank);

            float baseHours = BasePeriodHours();
            float speedMul  = SpeedMultForRank(ur.speedRank);
            float realHours = baseHours * speedMul;
            if (realHours < 0.001f)
                realHours = 0.001f;

            uint32 periodSec = (uint32)std::floor(realHours * 3600.0f);
            if (periodSec == 0)
                periodSec = 1;

            if (now > row.lastTick)
            {
                uint32 elapsed = now - row.lastTick;
                uint32 ticks   = elapsed / periodSec;

                if (ticks > 0)
                {
                    uint32 totalAdd = ticks * finalAmount;
                    bool hitCap     = AddCurrencyWithCap(guildId, row.materialId, totalAdd);

                    uint32 newLast = row.lastTick + ticks * periodSec;

                    WorldDatabase.DirectExecute(
                        "UPDATE customs.gv_production_active "
                        "SET last_tick={} "
                        "WHERE guildId={} AND material_id={}",
                        newLast, guildId, (uint32)row.materialId);

                    if (hitCap)
                    {
                        StopMaterial(guildId, row.materialId);
                        LOG_INFO("guildvillage",
                                 "GuildVillageProduction: guild {} reached cap for material {} -> production auto-stopped.",
                                 guildId, (uint32)row.materialId);
                    }
                }
            }
        }
    }

    // =========================
    // Gossip akce
    // =========================

    enum GossipAction : uint32
    {
        ACT_BACK_ROOT            = 20000,

        ACT_SEP_ROOT             = 20001,
        ACT_SEP_UPGRADE_BASE     = 20010,

        ACT_PROD_TOGGLE_BASE     = 21000,
        ACT_UPGRADE_MENU_BASE    = 22000,

        ACT_UPGRADE_AM_MENU_BASE = 23000,
        ACT_UPGRADE_SP_MENU_BASE = 24000,

        ACT_BUY_AMOUNT_BASE      = 25000,
        ACT_BUY_SPEED_BASE       = 26000
    };


    // =========================
    // Názvy materiálů
    // =========================

    static std::string MaterialName(uint8 materialId)
    {
        auto const& N = GuildVillage::Names::Get();
        switch (materialId)
        {
            case 1: return N.status.material1;
            case 2: return N.status.material2;
            case 3: return N.status.material3;
            case 4: return N.status.material4;
        }
        return "Unknown";
    }

    static void ShowRoot(Player* player, Creature* creature);
    static void ShowUpgradeMenu(Player* player, Creature* creature, uint8 materialId);
    static void ShowAmountConfirmMenu(Player* player, Creature* creature, uint8 materialId);
    static void ShowSpeedConfirmMenu(Player* player, Creature* creature, uint8 materialId);

    // =========================
    // Po start/stop produkce refresh gossipu
    // =========================

    class ReopenRootEvent : public BasicEvent
    {
    public:
        ReopenRootEvent(Player* p, ObjectGuid npc)
            : _player(p), _npcGuid(npc) { }

        bool Execute(uint64 /*time*/, uint32 /*diff*/) override
        {
            if (!_player)
                return true;

            if (Creature* np = ObjectAccessor::GetCreature(*_player, _npcGuid))
                ShowRoot(_player, np);

            return true;
        }

    private:
        Player*    _player;
        ObjectGuid _npcGuid;
    };

    static void ReopenRootAfterChange(Player* player, Creature* creature)
    {
        if (!player || !creature)
            return;

        ObjectGuid npcGuid = creature->GetGUID();

        CloseGossipMenuFor(player);

        player->m_Events.AddEvent(
            new ReopenRootEvent(player, npcGuid),
            player->m_Events.CalculateTime(50));
    }

    // =========================
    // Hlavní menu
    // =========================

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

        ProcessTicksForGuild(g->GetId());

        auto activeList = LoadActiveProduction(g->GetId());
        bool anyActive  = !activeList.empty();

        if (anyActive)
        {
            for (auto const& row : activeList)
            {
                uint8 m = row.materialId;
                std::string line = Acore::StringFormat(
                    T("Zastavit produkci: {}", "Stop production: {}"),
                    MaterialName(m));

                AddGossipItemFor(
                    player,
                    GOSSIP_ICON_CHAT,
                    line,
                    GOSSIP_SENDER_MAIN,
                    ACT_PROD_TOGGLE_BASE + m);
            }

            SendGossipMenuFor(player, 1, creature->GetGUID());
            return;
        }


        for (uint8 m = 1; m <= 4; ++m)
        {
            std::string line = Acore::StringFormat(
                T("Zahájit produkci: {}", "Start production: {}"),
                MaterialName(m));

            AddGossipItemFor(
                player,
                GOSSIP_ICON_INTERACT_1,
                line,
                GOSSIP_SENDER_MAIN,
                ACT_PROD_TOGGLE_BASE + m);
        }

        AddGossipItemFor(
            player,
            0,
            SeparatorLine(),
            GOSSIP_SENDER_MAIN,
            ACT_SEP_ROOT);


        for (uint8 m = 1; m <= 4; ++m)
        {
            std::string line = Acore::StringFormat(
                T("Vylepšit produkci: {}", "Upgrade production: {}"),
                MaterialName(m));

            AddGossipItemFor(
                player,
                GOSSIP_ICON_VENDOR,
                line,
                GOSSIP_SENDER_MAIN,
                ACT_UPGRADE_MENU_BASE + m);
        }

        SendGossipMenuFor(player, 1, creature->GetGUID());
    }

	// =========================
	// Submenu upgradu pro konkrétní materiál
	// (jen výběr: množství / rychlost)
	// =========================
	
	static void ShowUpgradeMenu(Player* player, Creature* creature, uint8 materialId)
	{
		ClearGossipMenuFor(player);
	
		Guild* g = player->GetGuild();
		if (!g)
		{
			SendGossipMenuFor(player, 1, creature->GetGUID());
			return;
		}
	
		ProcessTicksForGuild(g->GetId());
	
		UpgradeRanks ur = GetOrInitUpgradeRanks(g->GetId(), materialId);
	
		bool hasOption = false;
	
		if (ur.amountRank < 3)
		{
			uint8 nextRank = ur.amountRank + 1;
			auto catOpt = LoadProductionCatalog(materialId, /*upgradeType=*/1, nextRank);
			if (catOpt)
			{
				std::string labelBase = (LangOpt() == Lang::EN
										? catOpt->label_en
										: catOpt->label_cs);
	
				AddGossipItemFor(
					player,
					GOSSIP_ICON_MONEY_BAG,
					labelBase,
					GOSSIP_SENDER_MAIN,
					ACT_UPGRADE_AM_MENU_BASE + materialId);
	
				hasOption = true;
			}
		}
	
		// speed upgrade (Zrychlit produkci)
		if (ur.speedRank < 3)
		{
			uint8 nextRank = ur.speedRank + 1;
			auto catOpt = LoadProductionCatalog(materialId, /*upgradeType=*/2, nextRank);
			if (catOpt)
			{
				std::string labelBase = (LangOpt() == Lang::EN
										? catOpt->label_en
										: catOpt->label_cs);
	
				AddGossipItemFor(
					player,
					GOSSIP_ICON_MONEY_BAG,
					labelBase,
					GOSSIP_SENDER_MAIN,
					ACT_UPGRADE_SP_MENU_BASE + materialId);
	
				hasOption = true;
			}
		}
	
		if (!hasOption)
		{
			ChatHandler(player->GetSession()).SendSysMessage(
				T("Žádné další upgrady nejsou dostupné.", "No further upgrades available."));
			ShowRoot(player, creature);
			return;
		}
	
		// separator nad "Zpátky"
        AddGossipItemFor(
            player,
            0,
            SeparatorLine(),
            GOSSIP_SENDER_MAIN,
            ACT_SEP_UPGRADE_BASE + materialId);
	
		// Zpátky → root
		AddGossipItemFor(
			player,
			GOSSIP_ICON_TAXI,
			T("Zpátky", "Back"),
			GOSSIP_SENDER_MAIN,
			ACT_BACK_ROOT);
	
		SendGossipMenuFor(player, 1, creature->GetGUID());
	}

    // =========================
    // Submenu: potvrzení nákupu (množství)
    // =========================

    static void ShowAmountConfirmMenu(Player* player, Creature* creature, uint8 materialId)
    {
        ClearGossipMenuFor(player);

        Guild* g = player->GetGuild();
        if (!g)
        {
            SendGossipMenuFor(player, 1, creature->GetGUID());
            return;
        }

        ProcessTicksForGuild(g->GetId());

        UpgradeRanks ur = GetOrInitUpgradeRanks(g->GetId(), materialId);

        if (ur.amountRank >= 3)
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Máš již maximální rank množství.", "You already have maximum amount rank."));
            ShowUpgradeMenu(player, creature, materialId);
            return;
        }

        uint8 nextRank = ur.amountRank + 1;
        auto catOpt = LoadProductionCatalog(materialId, /*upgradeType=*/1, nextRank);
        if (!catOpt)
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Upgrade nenalezen v katalogu.", "Upgrade not found in catalog."));
            ShowUpgradeMenu(player, creature, materialId);
            return;
        }

        // 1) blok jen "Cena:"
        std::string pretty = CostLineProd(*catOpt);
        if (pretty.empty())
            pretty = T("zdarma", "free");

        std::string multi = pretty;
        std::string::size_type pos = 0;
        while ((pos = multi.find(" + ", pos)) != std::string::npos)
            multi.replace(pos, 3, "\n");

        std::string info;
        info += T("Cena:", "Cost:");
        info += "\n";
        info += multi;

        AddGossipItemFor(
            player,
            GOSSIP_ICON_MONEY_BAG,
            info,
            GOSSIP_SENDER_MAIN,
            ACT_UPGRADE_AM_MENU_BASE + materialId);

        // 2) separator
        AddGossipItemFor(
            player,
            0,
            SeparatorLine(),
            GOSSIP_SENDER_MAIN,
            ACT_UPGRADE_AM_MENU_BASE + materialId);

        // 3) Ano, zakoupit
        AddGossipItemFor(
            player,
            GOSSIP_ICON_TRAINER,
            T("Ano, zakoupit", "Confirm purchase"),
            GOSSIP_SENDER_MAIN,
            ACT_BUY_AMOUNT_BASE + (materialId * 10 + nextRank));

        // 4) Zpátky -> zpět na submenu materiálu (množství/rychlost)
        AddGossipItemFor(
            player,
            GOSSIP_ICON_TAXI,
            T("Zpátky", "Back"),
            GOSSIP_SENDER_MAIN,
            ACT_UPGRADE_MENU_BASE + materialId);

        SendGossipMenuFor(player, 1, creature->GetGUID());
    }

    // =========================
    // Submenu: potvrzení nákupu (rychlost)
    // =========================

    static void ShowSpeedConfirmMenu(Player* player, Creature* creature, uint8 materialId)
    {
        ClearGossipMenuFor(player);

        Guild* g = player->GetGuild();
        if (!g)
        {
            SendGossipMenuFor(player, 1, creature->GetGUID());
            return;
        }

        ProcessTicksForGuild(g->GetId());

        UpgradeRanks ur = GetOrInitUpgradeRanks(g->GetId(), materialId);

        if (ur.speedRank >= 3)
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Máš již maximální rank rychlosti.", "You already have maximum speed rank."));
            ShowUpgradeMenu(player, creature, materialId);
            return;
        }

        uint8 nextRank = ur.speedRank + 1;
        auto catOpt = LoadProductionCatalog(materialId, /*upgradeType=*/2, nextRank);
        if (!catOpt)
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Upgrade nenalezen v katalogu.", "Upgrade not found in catalog."));
            ShowUpgradeMenu(player, creature, materialId);
            return;
        }

        // 1) blok jen "Cena:"
        std::string pretty = CostLineProd(*catOpt);
        if (pretty.empty())
            pretty = T("zdarma", "free");

        std::string multi = pretty;
        std::string::size_type pos = 0;
        while ((pos = multi.find(" + ", pos)) != std::string::npos)
            multi.replace(pos, 3, "\n");

        std::string info;
        info += T("Cena:", "Cost:");
        info += "\n";
        info += multi;

        AddGossipItemFor(
            player,
            GOSSIP_ICON_MONEY_BAG,
            info,
            GOSSIP_SENDER_MAIN,
            ACT_UPGRADE_SP_MENU_BASE + materialId);

        // 2) separator
        AddGossipItemFor(
            player,
            0,
            SeparatorLine(),
            GOSSIP_SENDER_MAIN,
            ACT_UPGRADE_SP_MENU_BASE + materialId);

        // 3) Ano, zakoupit
        AddGossipItemFor(
            player,
            GOSSIP_ICON_TRAINER,
            T("Ano, zakoupit", "Confirm purchase"),
            GOSSIP_SENDER_MAIN,
            ACT_BUY_SPEED_BASE + (materialId * 10 + nextRank));

        // 4) Zpátky -> zpět na submenu materiálu
        AddGossipItemFor(
            player,
            GOSSIP_ICON_TAXI,
            T("Zpátky", "Back"),
            GOSSIP_SENDER_MAIN,
            ACT_UPGRADE_MENU_BASE + materialId);

        SendGossipMenuFor(player, 1, creature->GetGUID());
    }

    // =========================
    // NPC Script
    // =========================

    class npc_gv_production : public CreatureScript
    {
    public:
        npc_gv_production() : CreatureScript("npc_gv_production") { }

        bool OnGossipHello(Player* player, Creature* creature) override
        {
            Guild* g = player->GetGuild();
            if (!g)
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Nejsi v guildě.", "You are not in a guild."));
                return true;
            }

            if (!LoadVillagePhase(g->GetId()).has_value())
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Tvoje guilda nevlastní vesnici.", "Your guild does not own a village."));
                return true;
            }

            ShowRoot(player, creature);
            return true;
        }

        bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
        {
            if (sender != GOSSIP_SENDER_MAIN)
                return false;

            Guild* g = player->GetGuild();
            if (!g)
            {
                CloseGossipMenuFor(player);
                return true;
            }

            if (!LoadVillagePhase(g->GetId()).has_value())
            {
                CloseGossipMenuFor(player);
                return true;
            }

            bool leader = IsGuildLeader(player);

            // root separator
            if (action == ACT_SEP_ROOT)
            {
                ShowRoot(player, creature);
                return true;
            }

            // separator v submenu upgradu (materiál)
            if (action >= ACT_SEP_UPGRADE_BASE && action < ACT_SEP_UPGRADE_BASE + 100)
            {
                uint8 materialId = action - ACT_SEP_UPGRADE_BASE;
                ShowUpgradeMenu(player, creature, materialId);
                return true;
            }

            // Zpátky do rootu
            if (action == ACT_BACK_ROOT)
            {
                ShowRoot(player, creature);
                return true;
            }

            // otevření submenu upgradu
            if (action >= ACT_UPGRADE_MENU_BASE && action < ACT_UPGRADE_MENU_BASE + 100)
            {
                uint8 materialId = action - ACT_UPGRADE_MENU_BASE;

                if (!leader)
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Spravovat produkci mohou pouze Guild Master a Zástupce.",
                          "Only Guild Master and Officers can manage production."));
                    ShowRoot(player, creature);
                    return true;
                }

                ShowUpgradeMenu(player, creature, materialId);
                return true;
            }

            // submenu "Zvýšit množství"
            if (action >= ACT_UPGRADE_AM_MENU_BASE && action < ACT_UPGRADE_AM_MENU_BASE + 100)
            {
                uint8 materialId = action - ACT_UPGRADE_AM_MENU_BASE; // 1..4

                if (!leader)
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Spravovat produkci mohou pouze Guild Master a Zástupce.",
                          "Only Guild Master and Officers can manage production."));
                    ShowRoot(player, creature);
                    return true;
                }

                ShowAmountConfirmMenu(player, creature, materialId);
                return true;
            }

            // submenu "Zrychlit produkci"
            if (action >= ACT_UPGRADE_SP_MENU_BASE && action < ACT_UPGRADE_SP_MENU_BASE + 100)
            {
                uint8 materialId = action - ACT_UPGRADE_SP_MENU_BASE; // 1..4

                if (!leader)
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Spravovat produkci mohou pouze Guild Master a Zástupce.",
                          "Only Guild Master and Officers can manage production."));
                    ShowRoot(player, creature);
                    return true;
                }

                ShowSpeedConfirmMenu(player, creature, materialId);
                return true;
            }

            // start/stop produkce
            if (action >= ACT_PROD_TOGGLE_BASE && action < ACT_PROD_TOGGLE_BASE + 100)
            {
                uint8 materialId = action - ACT_PROD_TOGGLE_BASE; // 1..4

                if (!leader)
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Spravovat produkci mohou pouze Guild Master a Zástupce.",
                          "Only Guild Master and Officers can manage production."));
                    ShowRoot(player, creature);
                    return true;
                }

                bool active = IsMaterialActive(g->GetId(), materialId);
                if (active)
                {
                    StopMaterial(g->GetId(), materialId);
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Produkce materiálu zastavena.", "Production stopped."));
                }
                else
                {
                    StartMaterial(g->GetId(), materialId);
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Spouštím produkci materiálu.", "Production started."));
                }

                // refresh přes event
                ReopenRootAfterChange(player, creature);
                return true;
            }

            // nákup ranku množství
            if (action >= ACT_BUY_AMOUNT_BASE && action < ACT_BUY_AMOUNT_BASE + 1000)
            {
                if (!leader)
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Spravovat produkci mohou pouze Guild Master a Zástupce.",
                          "Only Guild Master and Officers can manage production."));
                    ShowRoot(player, creature);
                    return true;
                }

                uint32 diff = action - ACT_BUY_AMOUNT_BASE;
                uint8 materialId = diff / 10;
                uint8 rankToBuy  = diff % 10;

                UpgradeRanks ur = GetOrInitUpgradeRanks(g->GetId(), materialId);

                if (rankToBuy != ur.amountRank + 1 || rankToBuy > 3)
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Tento rank už máš nebo je neplatný.",
                          "You already own this rank or it's invalid."));
                    ShowUpgradeMenu(player, creature, materialId);
                    return true;
                }

                auto catOpt = LoadProductionCatalog(materialId, /*upgradeType=*/1, rankToBuy);
                if (!catOpt)
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Upgrade nenalezen v katalogu.", "Upgrade not found in catalog."));
                    ShowUpgradeMenu(player, creature, materialId);
                    return true;
                }

                if (!TryDeductCurrencyForProdUpgrade(g->GetId(), *catOpt))
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Nedostatek materiálu.", "Not enough materials."));
                    ShowAmountConfirmMenu(player, creature, materialId);
                    return true;
                }

                WorldDatabase.DirectExecute(
                    "UPDATE customs.gv_production_upgrade "
                    "SET amount_rank = {} "
                    "WHERE guildId={} AND material_id={}",
                    (uint32)rankToBuy, g->GetId(), (uint32)materialId);

                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Upgrade množství zakoupen.", "Amount upgrade purchased."));

                ShowUpgradeMenu(player, creature, materialId);
                return true;
            }

            // nákup ranku rychlosti
            if (action >= ACT_BUY_SPEED_BASE && action < ACT_BUY_SPEED_BASE + 1000)
            {
                if (!leader)
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Spravovat produkci mohou pouze Guild Master a Zástupce.",
                          "Only Guild Master and Officers can manage production."));
                    ShowRoot(player, creature);
                    return true;
                }

                uint32 diff = action - ACT_BUY_SPEED_BASE;
                uint8 materialId = diff / 10;
                uint8 rankToBuy  = diff % 10;

                UpgradeRanks ur = GetOrInitUpgradeRanks(g->GetId(), materialId);

                if (rankToBuy != ur.speedRank + 1 || rankToBuy > 3)
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Tento rank už máš nebo je neplatný.",
                          "You already own this rank or it's invalid."));
                    ShowUpgradeMenu(player, creature, materialId);
                    return true;
                }

                auto catOpt = LoadProductionCatalog(materialId, /*upgradeType=*/2, rankToBuy);
                if (!catOpt)
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Upgrade nenalezen v katalogu.", "Upgrade not found in catalog."));
                    ShowUpgradeMenu(player, creature, materialId);
                    return true;
                }

                if (!TryDeductCurrencyForProdUpgrade(g->GetId(), *catOpt))
                {
                    ChatHandler(player->GetSession()).SendSysMessage(
                        T("Nedostatek materiálu.", "Not enough materials."));
                    ShowSpeedConfirmMenu(player, creature, materialId);
                    return true;
                }

                WorldDatabase.DirectExecute(
                    "UPDATE customs.gv_production_upgrade "
                    "SET speed_rank = {} "
                    "WHERE guildId={} AND material_id={}",
                    (uint32)rankToBuy, g->GetId(), (uint32)materialId);

                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Upgrade rychlosti zakoupen.", "Speed upgrade purchased."));

                ShowUpgradeMenu(player, creature, materialId);
                return true;
            }

            CloseGossipMenuFor(player);
            return true;
        }
    };

    // ===== helper: projede všechny guildy, které právě něco produkují
    static void TickAllGuilds()
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT DISTINCT guildId FROM customs.gv_production_active"))
        {
            do
            {
                uint32 guildId = r->Fetch()[0].Get<uint32>();
                ProcessTicksForGuild(guildId);
            }
            while (r->NextRow());
        }
    }

    // ===== world-level periodic updater
    class GVProductionWorldUpdate : public WorldScript
    {
    public:
        GVProductionWorldUpdate() : WorldScript("GVProductionWorldUpdate") { }

        void OnUpdate(uint32 diff) override
        {
            static uint32 acc = 0;
            acc += diff;

            uint32 intervalMs = WorldTickMinutes() * 60 * 1000;

            if (intervalMs < 1000)
                intervalMs = 1000;

            if (acc >= intervalMs)
            {
                acc = 0;
                TickAllGuilds();
            }
        }
    };

    ProdStatusForMat GetProductionStatus(uint32 guildId, uint8 materialId)
    {
        ProdStatusForMat st;
        st.active        = IsMaterialActive(guildId, materialId);

        UpgradeRanks ur = GetOrInitUpgradeRanks(guildId, materialId);

        // kolik kusů za tick
        uint32 baseAmount  = BaseAmount();
        uint32 bonusAmount = AmountBonusForRank(ur.amountRank);
        st.amountPerTick   = baseAmount + bonusAmount;

        // perioda v hodinách
        float baseHours  = BasePeriodHours();
        float speedMul   = SpeedMultForRank(ur.speedRank);
        float realHours  = baseHours * speedMul;
        if (realHours < 0.001f)
            realHours = 0.001f;
        st.hoursPerTick = realHours;

        return st;
    }

    uint8 GetCurrentlyActiveMaterial(uint32 guildId)
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT material_id FROM customs.gv_production_active "
                "WHERE guildId={} LIMIT 1",
                guildId))
        {
            return r->Fetch()[0].Get<uint8>(); // 1..4
        }
        return 0;
    }

    // === Public API pro ostatní moduly ===

    std::optional<GuildCurrency> SyncGuildProduction(uint32 guildId)
    {
        ProcessTicksForGuild(guildId);
        return LoadGuildCurrencyNow(guildId);
    }

} // namespace GuildVillageProduction

// =========================
// Registrace do loaderu
// =========================

void RegisterGuildVillageProduction()
{
    new GuildVillageProduction::npc_gv_production();
    new GuildVillageProduction::GVProductionWorldUpdate();
}
