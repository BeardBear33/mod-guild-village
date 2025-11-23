// modules/mod-guild-village/src/guild_village_quests.cpp

#include "ScriptMgr.h"
#include "Config.h"
#include "Creature.h"
#include "ScriptedGossip.h"
#include "GossipDef.h"
#include "Player.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Chat.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"
#include "StringFormat.h"
#include "ObjectMgr.h"
#include "Map.h"
#include "gv_names.h"
#include "ObjectAccessor.h"
#include "GameObject.h"
#include "Mail.h"
#include "Item.h"
#include "ItemTemplate.h"

#include <string>
#include <vector>
#include <unordered_set>
#include <optional>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <climits>

namespace GuildVillage
{
    // =========================
    // Konfigurace & utilitky
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

    // oddělené klíče pro denní/ týdenní reset
    static std::string CFG_DAILY_RESET_TIME   = "04:00";
    static std::string CFG_WEEKLY_RESET_TIME  = "04:00";
    static std::string CFG_WEEKLY_RESET_DAY   = "Mon";
    static bool   CFG_ONLY_WOTLK_DUNGEONS = true;
	static uint32 CFG_DAILY_AMOUNT  = 1;
	static uint32 CFG_WEEKLY_AMOUNT = 1;

    static void LoadCfg()
    {
        CFG_DAILY_RESET_TIME   = sConfigMgr->GetOption<std::string>("GuildVillage.Quests.DailyReset.Time",   "04:00");
        CFG_WEEKLY_RESET_TIME  = sConfigMgr->GetOption<std::string>("GuildVillage.Quests.WeeklyReset.Time",  "04:00");
        CFG_WEEKLY_RESET_DAY   = sConfigMgr->GetOption<std::string>("GuildVillage.Quests.WeeklyReset.Day",   "Mon");
        CFG_ONLY_WOTLK_DUNGEONS = sConfigMgr->GetOption<bool>("GuildVillage.Quests.OnlyWotLKDungeons", true);
		CFG_DAILY_AMOUNT  = sConfigMgr->GetOption<uint32>("GuildVillage.Quests.DailyAmount",  1u);
		CFG_WEEKLY_AMOUNT = sConfigMgr->GetOption<uint32>("GuildVillage.Quests.WeeklyAmount", 1u);
	
		// jednoduchý clamp ať to někdo nepošle na 999
		if (CFG_DAILY_AMOUNT == 0)  CFG_DAILY_AMOUNT  = 1;
		if (CFG_WEEKLY_AMOUNT == 0) CFG_WEEKLY_AMOUNT = 1;
		if (CFG_DAILY_AMOUNT > 4)   CFG_DAILY_AMOUNT  = 4;
		if (CFG_WEEKLY_AMOUNT > 4)  CFG_WEEKLY_AMOUNT = 4;
    }

    static inline char const* SeparatorLine() { return "|cff808080---------------------------|r"; }

    // CAP měn
    static inline bool CapsEnabled()
    {
        return sConfigMgr->GetOption<bool>("GuildVillage.CurrencyCap.Enabled", true);
    }
    static inline uint32 CapMaterial1()  { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material1",  1000); }
    static inline uint32 CapMaterial2()  { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material2",  1000); }
    static inline uint32 CapMaterial3()  { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material3",  1000); }
    static inline uint32 CapMaterial4()  { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material4", 1000); }

    // Připsat s capy a vrátit reálně připsané kusy
    static std::array<uint32,4> ApplyGuildCurrencyWithCapsReport(uint32 guildId, uint32 add1, uint32 add2, uint32 add3, uint32 add4)
    {
        uint32 a1=0,a2=0,a3=0,a4=0;
        if (!CapsEnabled())
        {
            a1=add1; a2=add2; a3=add3; a4=add4;
            if (a1||a2||a3||a4)
            {
                WorldDatabase.DirectExecute(Acore::StringFormat(
                    "UPDATE customs.gv_currency SET "
                    "material1=material1+{}, material2=material2+{}, material3=material3+{}, material4=material4+{}, last_update=NOW() "
                    "WHERE guildId={}", a1,a2,a3,a4,guildId).c_str());
            }
            return {a1,a2,a3,a4};
        }

        uint32 cap1 = CapMaterial1(), cap2 = CapMaterial2(), cap3 = CapMaterial3(), cap4 = CapMaterial4();
        uint32 cur1=0,cur2=0,cur3=0,cur4=0;
        if (QueryResult q = WorldDatabase.Query(
                "SELECT material1, material2, material3, material4 FROM customs.gv_currency WHERE guildId={}", guildId))
        {
            Field* f = q->Fetch();
            cur1=f[0].Get<uint32>(); cur2=f[1].Get<uint32>(); cur3=f[2].Get<uint32>(); cur4=f[3].Get<uint32>();
        }
        auto room=[&](uint32 cur,uint32 cap)->uint32{ if(cap==0) return UINT32_MAX; if(cur>=cap) return 0u; return cap-cur; };

        a1 = std::min(add1, room(cur1, cap1));
        a2 = std::min(add2, room(cur2, cap2));
        a3 = std::min(add3, room(cur3, cap3));
        a4 = std::min(add4, room(cur4, cap4));

        if (a1||a2||a3||a4)
        {
            WorldDatabase.DirectExecute(Acore::StringFormat(
                "UPDATE customs.gv_currency SET "
                "material1=material1+{}, material2=material2+{}, material3=material3+{}, material4=material4+{}, last_update=NOW() "
                "WHERE guildId={}", a1,a2,a3,a4,guildId).c_str());
        }
        return {a1,a2,a3,a4};
    }

    // --- typy & mapování ---
    enum class ResetType : uint8 { Daily = 1, Weekly = 2 };

    static inline const char* ResetName(ResetType rt) { return (rt == ResetType::Daily) ? "daily" : "weekly"; }

    static bool GuildHasQuestsUpgrade(uint32 guildId)
	{
		return WorldDatabase.Query(
			"SELECT 1 FROM customs.gv_upgrades WHERE guildId={} AND expansion_key='quests' LIMIT 1",
			guildId) != nullptr;
	}

    // whitelist WotLK 5man dungeony
    static bool IsWotlkDungeon(uint32 mapId)
    {
        static const std::unordered_set<uint32> wotlk{
            574, 575, 576, 578, 601, 619, 600, 604, 608, 599, 602, 595, 650, 632, 658, 668
        };
        return wotlk.find(mapId) != wotlk.end();
    }

    // --- resety v C++ ---
    static bool ParseHHMM(std::string const& s, int& outH, int& outM)
    {
        outH=0; outM=0;
        if (s.size() < 4) return false;
        char colon=':'; int h=0,m=0;
        if (sscanf(s.c_str(), "%d%c%d", &h, &colon, &m) == 3 && colon==':' && h>=0 && h<=23 && m>=0 && m<=59)
        { outH=h; outM=m; return true; }
        return false;
    }
    static uint32 ToUnix(std::tm tmv)
    {
        time_t t = mktime(&tmv);
        if (t < 0) t = 0;
        return static_cast<uint32>(t);
    }
    static uint32 CalcNextDailyResetTS()
    {
        int H=4, M=0; ParseHHMM(CFG_DAILY_RESET_TIME, H, M);
        time_t nowT = time(nullptr);
        std::tm now = *localtime(&nowT);

        std::tm target = now;
        target.tm_hour = H; target.tm_min = M; target.tm_sec = 0;

        uint32 todayAt = ToUnix(target);
        if (static_cast<uint32>(nowT) <= todayAt)
            return todayAt;

        target = *localtime(&nowT);
        target.tm_mday += 1;
        target.tm_hour = H; target.tm_min = M; target.tm_sec = 0;
        return ToUnix(target);
    }

    // Rozšířený parser dne
    static int DOWFromToken(std::string day)
    {
        auto ltrim = [](std::string& s){ s.erase(0, s.find_first_not_of(" \t\r\n")); };
        auto rtrim = [](std::string& s){ s.erase(s.find_last_not_of(" \t\r\n") + 1); };
        ltrim(day); rtrim(day);
        std::transform(day.begin(), day.end(), day.begin(), ::tolower);

        // numeric: 0..6 (0=Sunday), 7 -> 0
        bool allDigits = !day.empty() && std::all_of(day.begin(), day.end(), [](char c){ return c >= '0' && c <= '9'; });
        if (allDigits)
        {
            int n = std::atoi(day.c_str());
            if (n == 7) n = 0;
            if (n >= 0 && n <= 6)
                return n;
        }

        // EN
        if (day == "sun" || day == "sunday")     return 0;
        if (day == "mon" || day == "monday")     return 1;
        if (day == "tue" || day == "tuesday")    return 2;
        if (day == "wed" || day == "wednesday")  return 3;
        if (day == "thu" || day == "thursday")   return 4;
        if (day == "fri" || day == "friday")     return 5;
        if (day == "sat" || day == "saturday")   return 6;

        // CZ
        if (day == "nedele"  || day == "neděle"  || day == "ne") return 0;
        if (day == "pondeli" || day == "pondělí" || day == "po") return 1;
        if (day == "utery"   || day == "úterý"   || day == "ut" || day == "út") return 2;
        if (day == "streda"  || day == "středa"  || day == "st") return 3;
        if (day == "ctvrtek" || day == "čtvrtek" || day == "ct" || day == "čt") return 4;
        if (day == "patek"   || day == "pátek"   || day == "pa" || day == "pá") return 5;
        if (day == "sobota"  || day == "so")                     return 6;

        // fallback: pondělí
        return 1;
    }

    static uint32 CalcNextWeeklyResetTS()
    {
        int H=4, M=0; ParseHHMM(CFG_WEEKLY_RESET_TIME, H, M);
        int tgt = DOWFromToken(CFG_WEEKLY_RESET_DAY);

        time_t nowT = time(nullptr);
        std::tm now = *localtime(&nowT);

        int delta = (tgt - now.tm_wday + 7) % 7;

        std::tm target = now;
        target.tm_hour = H; target.tm_min = M; target.tm_sec = 0;

        if (delta == 0)
        {
            uint32 todayAt = ToUnix(target);
            if (static_cast<uint32>(nowT) <= todayAt)
                return todayAt;
            delta = 7;
        }

        target = now;
        target.tm_mday += delta;
        target.tm_hour = H; target.tm_min = M; target.tm_sec = 0;
        return ToUnix(target);
    }
	
	static uint32 GetQuestAmount(ResetType rt)
	{
		return (rt == ResetType::Daily) ? CFG_DAILY_AMOUNT : CFG_WEEKLY_AMOUNT;
	}

	// --- assignment / rotation ---
	static void EnsureGuildQuestAssigned(uint32 guildId, ResetType rt)
	{
		if (!GuildHasQuestsUpgrade(guildId))
			return;
	
		uint32 now   = uint32(time(nullptr));
		uint32 count = GetQuestAmount(rt);
		if (!count)
			return;
	
		for (uint8 slot = 1; slot <= count; ++slot)
		{
			// 1) Platný quest v tomhle slotu?
			if (QueryResult r = WorldDatabase.Query(
					"SELECT next_rotation_at FROM customs.gv_guild_quests "
					"WHERE guildId={} AND reset_type={} AND slot={} LIMIT 1",
					guildId, (uint32)rt, (uint32)slot))
			{
				uint32 until = r->Fetch()[0].Get<uint32>();
				if (until > now)
					continue; // slot je OK
			}
	
			// 2) Vybrat nový quest
			if (QueryResult q = WorldDatabase.Query(
				"SELECT c.id, c.quest_count "
				"FROM customs.gv_quest_catalog c "
				"WHERE c.enabled=1 "
				"  AND c.reset_type='{}' "
				"  AND (c.required_expansion IS NULL "
				"       OR c.required_expansion='' "
				"       OR EXISTS (SELECT 1 FROM customs.gv_upgrades ug "
				"                  WHERE ug.guildId={} "
				"                    AND ug.expansion_key=c.required_expansion)) "
				// (1) zákaz stejného quest_id v tomtéž reset_type (aktivní)
				"  AND NOT EXISTS (SELECT 1 "
				"                  FROM customs.gv_guild_quests g "
				"                  WHERE g.guildId={} "
				"                    AND g.reset_type={} "
				"                    AND g.quest_id = c.id "
				"                    AND g.next_rotation_at > {}) "
				// (2) zákaz stejného quest_type v tomtéž reset_type (jiný slot, aktivní)
				"  AND NOT EXISTS (SELECT 1 "
				"                  FROM customs.gv_guild_quests g "
				"                  LEFT JOIN customs.gv_quest_catalog cc ON cc.id=g.quest_id "
				"                  WHERE g.guildId={} "
				"                    AND g.reset_type={} "
				"                    AND g.next_rotation_at > {} "
				"                    AND cc.quest_type = c.quest_type) "
				// (3) zákaz stejného quest_type v druhém reset_type (aktivní)
				"  AND NOT EXISTS (SELECT 1 "
				"                  FROM customs.gv_guild_quests g2 "
				"                  LEFT JOIN customs.gv_quest_catalog cc2 ON cc2.id=g2.quest_id "
				"                  WHERE g2.guildId={} "
				"                    AND g2.reset_type<>{} "
				"                    AND g2.next_rotation_at > {} "
				"                    AND cc2.quest_type = c.quest_type) "
				"ORDER BY RAND() LIMIT 1",
				ResetName(rt),            // {}
				guildId,                  // {}
				guildId, (uint32)rt, now, // {}, {}, {}
				guildId, (uint32)rt, now, // {}, {}, {}
				guildId, (uint32)rt, now  // {}, {}, {}
			))
			{
				Field* f = q->Fetch();
				uint32 qid  = f[0].Get<uint32>();
				uint32 goal = f[1].Get<uint32>();
	
				uint32 next = (rt == ResetType::Daily) ? CalcNextDailyResetTS()
													: CalcNextWeeklyResetTS();
				if (!next || next <= now)
					next = now + (rt == ResetType::Daily ? 86400u : 7u * 86400u);
	
				WorldDatabase.DirectExecute(Acore::StringFormat(
					"REPLACE INTO customs.gv_guild_quests "
					"(guildId, reset_type, slot, quest_id, progress, goal, completed, reward_claimed, assigned_at, next_rotation_at) "
					"VALUES ({}, {}, {}, {}, 0, {}, 0, 0, {}, {})",
					guildId, (uint32)rt, (uint32)slot, qid, goal, now, next).c_str());
			}
			else
			{
				// nic vhodného → slot prázdný
				WorldDatabase.DirectExecute(Acore::StringFormat(
					"DELETE FROM customs.gv_guild_quests "
					"WHERE guildId={} AND reset_type={} AND slot={}",
					guildId, (uint32)rt, (uint32)slot).c_str());
			}
		}
	
		// Oříznout přebytečné sloty (když se sníží amount)
		WorldDatabase.DirectExecute(Acore::StringFormat(
			"DELETE FROM customs.gv_guild_quests "
			"WHERE guildId={} AND reset_type={} AND slot>{}",
			guildId, (uint32)rt, (uint32)count).c_str());
	}
	
	// Exportovaný helper pro ostatní části modu (commands atd.)
    void GV_EnsureGuildQuestsAssignedForGuild(uint32 guildId)
    {
        EnsureGuildQuestAssigned(guildId, ResetType::Daily);
        EnsureGuildQuestAssigned(guildId, ResetType::Weekly);
    }


    // ==== výsledek připsání odměn ====
    struct RewardApplyResult
    {
        std::array<uint32,4> adds;                         // reálně připsané materiály
        std::vector<std::pair<uint32,uint32>> items;       // (itemId, count) pro mailing GM
    };

    // forward
    static RewardApplyResult GrantRewardsToGuild(uint32 guildId, uint32 questId);

	// --- helper: převod názvu creature_type (ENUM/VARCHAR) -> číslo z SharedDefines.h ---
	static uint8 ParseCreatureTypeName(std::string name)
	{
		auto trimLower = [](std::string& s){
			auto notsp = [](int ch){ return !std::isspace(ch); };
			s.erase(s.begin(), std::find_if(s.begin(), s.end(), notsp));
			s.erase(std::find_if(s.rbegin(), s.rend(), notsp).base(), s.end());
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
			for (char& c : s) if (c=='-' || c==' ') c = '_';
		};
		trimLower(name);

		if (name.empty() || name=="0" || name=="none" || name=="not_set" || name=="unknown") return 0;
		if (name=="beast")           return 1;
		if (name=="dragonkin")       return 2;
		if (name=="demon")           return 3;
		if (name=="elemental")       return 4;
		if (name=="giant")           return 5;
		if (name=="undead")          return 6;
		if (name=="humanoid")        return 7;
		if (name=="critter")         return 8;
		if (name=="mechanical")      return 9;
		if (name=="not_specified" || name=="not_specified_or_none" || name=="unspecified") return 10;
		if (name=="totem")           return 11;
		if (name=="non_combat_pet" || name=="noncombat_pet" || name=="companion")         return 12;
		if (name=="gas_cloud" || name=="gascloud")                                        return 13;

		if (std::all_of(name.begin(), name.end(), ::isdigit))
			return static_cast<uint8>(std::min(255, std::stoi(name)));

		return 0;
	}

    struct GuildQuestRow
	{
		uint32 questId=0;
		uint32 progress=0;
		uint32 goal=0;
		bool   completed=false;
		uint32 nextRotationAt=0;
		uint32 itemEntry = 0;
		uint8  slot = 0;          // NOVÉ
		
		std::string questTypeName;
		uint32 creatureEntry = 0;
		uint8  creatureType  = 0;
	
		std::string info_cs, info_en;
		std::string questName_cs, questName_en;
	};


	static std::vector<GuildQuestRow> LoadGuildQuests(uint32 guildId, ResetType rt)
	{
		std::vector<GuildQuestRow> out;
	
		if (QueryResult r = WorldDatabase.Query(
				"SELECT g.slot, g.quest_id, g.progress, g.goal, g.completed, g.next_rotation_at, "
				"       c.info_cs, c.info_en, c.quest_type, c.creature_entry, c.item_entry, c.creature_type, "
				"       c.quest_name_cs, c.quest_name_en "
				"FROM customs.gv_guild_quests g "
				"LEFT JOIN customs.gv_quest_catalog c ON c.id=g.quest_id "
				"WHERE g.guildId={} AND g.reset_type={} "
				"ORDER BY g.slot ASC",
				guildId, (uint32)rt))
		{
			do
			{
				Field* f = r->Fetch();
				GuildQuestRow row;
				row.slot          = f[0].Get<uint8>();
				row.questId       = f[1].Get<uint32>();
				row.progress      = f[2].Get<uint32>();
				row.goal          = f[3].Get<uint32>();
				row.completed     = f[4].Get<bool>();
				row.nextRotationAt= f[5].Get<uint32>();
				row.info_cs       = f[6].IsNull()? "" : f[6].Get<std::string>();
				row.info_en       = f[7].IsNull()? "" : f[7].Get<std::string>();
				row.questTypeName = f[8].IsNull()? "" : f[8].Get<std::string>();
		
				if (!f[9].IsNull())  row.creatureEntry = f[9].Get<uint32>();
				if (!f[10].IsNull()) row.itemEntry     = f[10].Get<uint32>();
		
				if (!f[11].IsNull())
				{
					std::string typeName = f[11].Get<std::string>();
					row.creatureType = ParseCreatureTypeName(typeName);
				}
		
				row.questName_cs = f[12].IsNull()? "" : f[12].Get<std::string>();
				row.questName_en = f[13].IsNull()? "" : f[13].Get<std::string>();
		
				out.push_back(row);
			}
			while (r->NextRow());
		}
		
		return out;
	}

    // --- Odměny ---
    enum class RewardKind { None, Mat1, Mat2, Mat3, Mat4, Random, Item };

    struct RewardToken
    {
        RewardKind kind = RewardKind::None;
        uint32 itemId = 0;
        uint32 count  = 0;
    };

    static RewardToken ParseRewardToken(std::string s, uint32 cnt)
    {
        RewardToken t;
        t.count = cnt;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        if (s.empty() || cnt == 0) return t;

        bool numeric = !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
        if (numeric)
        {
            t.kind   = RewardKind::Item;
            t.itemId = (uint32)std::strtoul(s.c_str(), nullptr, 10);
            return t;
        }

        if (s == "material1") t.kind = RewardKind::Mat1;
        else if (s == "material2") t.kind = RewardKind::Mat2;
        else if (s == "material3") t.kind = RewardKind::Mat3;
        else if (s == "material4") t.kind = RewardKind::Mat4;
        else if (s == "random")       t.kind = RewardKind::Random;
        return t;
    }

    // === WoW item link helpery ===
    static std::string MakeItemLink(uint32 itemId)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            return Acore::StringFormat("[Item {}]", itemId);

        // Barva dle kvality (Trinity/AzerothCore má ItemQualityColors[])
        uint32 rgba = ItemQualityColors[proto->Quality];
        uint8 r = (rgba >> 16) & 0xFF;
        uint8 g = (rgba >> 8)  & 0xFF;
        uint8 b = (rgba)       & 0xFF;

        return Acore::StringFormat(
            "|cff{:02X}{:02X}{:02X}|Hitem:{}:0:0:0:0:0:0:0|h[{}]|h|r",
            (uint32)r, (uint32)g, (uint32)b,
            (uint32)itemId,
            proto->Name1
        );
    }

    // Plain link (bez barvy) pro gossip
    static std::string MakeItemLinkPlain(uint32 itemId)
    {
        if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId))
            return Acore::StringFormat("[{}]", proto->Name1);
        return Acore::StringFormat("[Item {}]", itemId);
    }

    // Formátování pro gossip řádek „Odměna:“
    // - Materiály: "+15 Náhodný materiál" / "+12 Materiál 1"
    // - Itemy: "5x [Elixir of Accuracy]" (bez barvy v linku)
    static std::string RewardTokenToText(RewardToken const& t)
    {
        using namespace GuildVillage::Names;
        if (t.kind == RewardKind::None || t.count == 0)
            return "";

        if (t.kind == RewardKind::Item)
        {
            std::string link = MakeItemLinkPlain(t.itemId); // bez barvy
            return Acore::StringFormat("{}x {}", t.count, link);
        }

        auto matName = [&](Names::Mat m)->std::string
        {
            return Names::CountName(m, t.count);
        };

        switch (t.kind)
        {
            case RewardKind::Mat1: return Acore::StringFormat("+{} {}", t.count, matName(Names::Mat::Material1));
            case RewardKind::Mat2: return Acore::StringFormat("+{} {}", t.count, matName(Names::Mat::Material2));
            case RewardKind::Mat3: return Acore::StringFormat("+{} {}", t.count, matName(Names::Mat::Material3));
            case RewardKind::Mat4: return Acore::StringFormat("+{} {}", t.count, matName(Names::Mat::Material4));
            case RewardKind::Random:
            {
                std::string any = (LangOpt()==Lang::EN) ? "Random material" : "Náhodný materiál";
                return Acore::StringFormat("+{} {}", t.count, any);
            }
            default: break;
        }
        return "";
    }

    // Vystavovací řádek do gossipu
    static std::string BuildRewardLine(uint32 questId)
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT reward1, reward1_count, reward2, reward2_count, reward3, reward3_count, reward4, reward4_count, reward5, reward5_count "
                "FROM customs.gv_quest_catalog WHERE id={}", questId))
        {
            Field* f = r->Fetch();
            std::vector<std::string> parts;
            parts.reserve(5);
            for (int i=0;i<5;i++)
            {
                if (f[2*i].IsNull() || f[2*i+1].IsNull()) continue;
                std::string key = f[2*i].Get<std::string>();
                uint32      cnt = f[2*i+1].Get<uint32>();
                RewardToken tok = ParseRewardToken(key, cnt);
                std::string text = RewardTokenToText(tok);
                if (!text.empty()) parts.push_back(text);
            }

            std::string joined;
            for (size_t i=0;i<parts.size();++i)
            {
                if (i) joined += ", ";
                joined += parts[i];
            }

            std::string label = (LangOpt()==Lang::EN) ? "Reward: " : "Odměna: ";
            if (joined.empty())
                joined = "—";
            return label + joined;
        }
        return (LangOpt()==Lang::EN) ? "Reward: —" : "Odměna: —";
    }

    // Postaví text „Odměna …“ z reálně připsaných kusů (jen materiály)
    static std::string BuildGrantedRewardText(std::array<uint32,4> adds)
    {
        using namespace GuildVillage::Names;
        std::vector<std::string> parts;
        auto push = [&](Names::Mat m, uint32 cnt)
        {
            if (!cnt) return;
            parts.push_back(Acore::StringFormat("{} {}", cnt, Names::CountName(m, cnt)));
        };

        push(Names::Mat::Material1, adds[0]);
        push(Names::Mat::Material2, adds[1]);
        push(Names::Mat::Material3, adds[2]);
        push(Names::Mat::Material4, adds[3]);

        if (parts.empty()) return LangOpt()==Lang::EN ? "—" : "—";

        std::string joined;
        for (size_t i=0;i<parts.size(); ++i)
        {
            if (i) joined += ", ";
            joined += parts[i];
        }
        return joined;
    }

    // Systémové oznámení všem ONLINE členům dané guildy
    static void GuildBroadcastSystem(uint32 guildId, std::string const& msg)
    {
        std::string pref = "[Guild Village] ";

        auto const& players = HashMapHolder<Player>::GetContainer();
        for (auto const& pair : players)
        {
            Player* plr = pair.second;
            if (!plr) continue;
            if (!plr->IsInWorld()) continue;
            if (plr->GetGuildId() != guildId) continue;

            if (WorldSession* sess = plr->GetSession())
                ChatHandler(sess).SendSysMessage(pref + msg);
        }
    }

    // === MAILING: poslat item odměny GM (ONLY GM), stacking + až 12 příloh / mail ===
    static void SendItemRewardsToGuildMaster(uint32 guildId, std::vector<std::pair<uint32/*itemId*/, uint32/*count*/>> const& items)
    {
        if (items.empty())
            return;

        Guild* g = sGuildMgr->GetGuildById(guildId);
        if (!g)
            return;

        ObjectGuid gmGuid = g->GetLeaderGUID();
        if (!gmGuid)
            return;

        ObjectGuid::LowType gmLow = gmGuid.GetCounter();
        Player* gmOnline = ObjectAccessor::FindPlayer(gmGuid);

        // Připravit rozpad na stacky dle maxStackSize
        struct Stack { uint32 itemId; uint32 count; };
        std::vector<Stack> stacks;

        for (auto const& p : items)
        {
            uint32 itemId = p.first;
            uint32 total  = p.second;
            if (!itemId || !total) continue;

            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
            if (!proto) continue;

            uint32 maxStack = 1;
#if AC_COMPILER == AC_COMPILER_GNU || 1

            if (proto->GetMaxStackSize() > 0)
                maxStack = proto->GetMaxStackSize();
            else if (proto->Stackable > 0)
                maxStack = proto->Stackable;
#else
            maxStack = proto->GetMaxStackSize();
#endif
            if (maxStack == 0) maxStack = 1;

            while (total > 0)
            {
                uint32 take = std::min(maxStack, total);
                stacks.push_back({ itemId, take });
                total -= take;
            }
        }

        if (stacks.empty())
            return;

        // Odeslat poštou: 12 příloh / mail
        static constexpr uint8  MAX_ATTACH = 12;
        static constexpr uint32 QUEST_BOARD_GO_ENTRY = 990204;
        MailSender sender(MAIL_GAMEOBJECT, QUEST_BOARD_GO_ENTRY, MAIL_STATIONERY_DEFAULT);

        std::string subject = (LangOpt() == Lang::EN) ? "Guild Quest Reward" : "Odměna za guildovní úkol";
        std::string body    = (LangOpt() == Lang::EN)
            ? "Your guild has completed a quest. The reward is attached."
            : "Vaše guilda dokončila úkol. V příloze naleznete odměnu.";

        size_t i = 0;
        while (i < stacks.size())
        {
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            MailDraft draft(subject, body);

            uint8 attached = 0;
            for (; i < stacks.size() && attached < MAX_ATTACH; ++i, ++attached)
            {
                uint32 itemId = stacks[i].itemId;
                uint32 count  = stacks[i].count;

                Item* it = Item::CreateItem(itemId, count, gmOnline);
                if (!it)
                    continue;

                it->SetOwnerGUID(gmGuid);
                it->SaveToDB(trans);
                draft.AddItem(it);
            }

            draft.SendMailTo(
                trans,
                MailReceiver(gmOnline, gmLow),
                sender,
                MAIL_CHECK_MASK_NONE,
                60,   // doručení za 60 s
                30,   // expirace 30 dní
                false,
                true
            );

            CharacterDatabase.CommitTransaction(trans);
        }

        // Informační broadcast pro každý typ itemu (poštou GM)
        for (auto const& p : items)
        {
            uint32 itemId = p.first;
            uint32 count  = p.second;
            if (!itemId || !count) continue;

            std::string link = MakeItemLink(itemId);
            std::string line = (LangOpt() == Lang::EN)
                ? Acore::StringFormat("Reward {}x {} has been sent by mail to the Guild Master.", count, link)
                : Acore::StringFormat("Odměna {}x {} byla odeslána poštou Guild Masterovi.", count, link);

            GuildBroadcastSystem(guildId, line);
        }
    }

    // --- odměny (reálné připsání + report) ---
    static RewardApplyResult GrantRewardsToGuild(uint32 guildId, uint32 questId)
    {
        uint32 m1=0,m2=0,m3=0,m4=0;

        std::vector<std::pair<uint32,uint32>> itemRewards;

        if (QueryResult r = WorldDatabase.Query(
                "SELECT reward1, reward1_count, reward2, reward2_count, reward3, reward3_count, reward4, reward4_count, reward5, reward5_count "
                "FROM customs.gv_quest_catalog WHERE id={}", questId))
        {
            Field* f = r->Fetch();

            auto addAllSplit = [&](uint32 count)
			{
				if (!count)
					return;
			

				if (count == 1)
				{
					switch (urand(0,3))
					{
						case 0: ++m1; break;
						case 1: ++m2; break;
						case 2: ++m3; break;
						default: ++m4; break;
					}
					return;
				}
			
				// 2 typy ~40 %, 3 typy ~35 %, 4 typy ~25 %.
				uint32 k;
				uint32 roll = urand(1,100);
				if (roll <= 40)       k = 2; // 40 %
				else if (roll <= 75)  k = 3; // 35 %
				else                  k = 4; // 25 %
			
				if (count < k)
					k = count;
			
				// Indexy 0..3 = m1..m4
				uint8 idx[4] = {0,1,2,3};
			
				// Jednoduchý Fisher–Yates shuffle přes urand
				for (uint8 i = 0; i < 4; ++i)
				{
					uint8 j = urand(i, 3);
					std::swap(idx[i], idx[j]);
				}
			
				uint32 tmp[4] = {0,0,0,0};
			
				// Každý z vybraných typů dostane nejdřív 1 kus
				for (uint32 i = 0; i < k; ++i)
					++tmp[idx[i]];
			
				uint32 remaining = count - k;
			
				// Zbytek náhodně rozházet jen mezi vybrané typy
				while (remaining--)
				{
					uint32 r = urand(0, k - 1);     // index do prvních k v poli idx
					++tmp[idx[r]];
				}
			
				// Připsat do globálních počitadl
				m1 += tmp[0];
				m2 += tmp[1];
				m3 += tmp[2];
				m4 += tmp[3];
			};

            for (int i=0;i<5;i++)
            {
                if (f[2*i].IsNull() || f[2*i+1].IsNull()) continue;
                std::string rkey = f[2*i].Get<std::string>();
                uint32      cnt  = f[2*i+1].Get<uint32>();
                if (rkey.empty() || cnt==0) continue;

                RewardToken tok = ParseRewardToken(rkey, cnt);
                switch (tok.kind)
                {
                    case RewardKind::Mat1: m1 += tok.count; break;
                    case RewardKind::Mat2: m2 += tok.count; break;
                    case RewardKind::Mat3: m3 += tok.count; break;
                    case RewardKind::Mat4: m4 += tok.count; break;
                    case RewardKind::Random:  addAllSplit(tok.count); break;
                    case RewardKind::Item:
                    {
                        itemRewards.emplace_back(tok.itemId, tok.count);
                        break;
                    }
                    default: break;
                }
            }
        }

        // Připsat materiály s capy
        auto adds = ApplyGuildCurrencyWithCapsReport(guildId, m1,m2,m3,m4);

        return RewardApplyResult{adds, itemRewards};
    }

	// Vrátí true, pokud killEntry odpovídá primárnímu c.creature_entry
	// NEBO existuje jako alternativa v customs.gv_quest_creature_multi
	static bool MatchCreatureForQuest(uint32 questId, uint32 cfgCreatureEntry, uint32 killEntry)
	{
		if (!questId || !cfgCreatureEntry || !killEntry)
			return false;
	
		if (killEntry == cfgCreatureEntry)
			return true;
	
		// alternativa?
		return WorldDatabase.Query(
			"SELECT 1 FROM customs.gv_quest_creature_multi "
			"WHERE quest_id={} AND creature_entry={} LIMIT 1",
			questId, killEntry
		) != nullptr;
	}

	// --- generická větev pro stringové quest_type + volitelný creature_entry / item_entry / creature_type ---
	static void IncrementProgressIfQuestType(
		uint32 guildId, ResetType rt,
		std::string const& expectedType,
		std::optional<uint32> expectedCreatureEntry = std::nullopt,
		std::optional<uint32> expectedItemEntry     = std::nullopt,
		std::optional<uint8>  expectedCreatureType  = std::nullopt,
		Player* actor = nullptr
		)
		
	{
		if (expectedType.empty())
			return;

		if (QueryResult r = WorldDatabase.Query(
			"SELECT g.quest_id, g.progress, g.goal, g.completed, "
			"       c.quest_type, IFNULL(c.creature_entry,0), IFNULL(c.item_entry,0), c.creature_type, "
			"       c.quest_name_cs, c.quest_name_en "            // NOVÉ
			"FROM customs.gv_guild_quests g "
			"LEFT JOIN customs.gv_quest_catalog c ON c.id=g.quest_id "
			"WHERE g.guildId={} AND g.reset_type={}",
			guildId, (uint32)rt))

		{
			do
			{
				Field* f = r->Fetch();
				uint32 qid              = f[0].Get<uint32>();
				uint32 prog             = f[1].Get<uint32>();
				uint32 goal             = f[2].Get<uint32>();
				bool   done             = f[3].Get<bool>();
				std::string qtypeName   = f[4].IsNull() ? "" : f[4].Get<std::string>();
				uint32 cfgCreatureEntry = f[5].Get<uint32>();
				uint32 cfgItemEntry     = f[6].Get<uint32>();
				std::string qname_cs = f[8].IsNull()? "" : f[8].Get<std::string>();
				std::string qname_en = f[9].IsNull()? "" : f[9].Get<std::string>();


				uint8  cfgCreatureType  = 0;
				if (!f[7].IsNull())
				{
					std::string ctName = f[7].Get<std::string>();
					cfgCreatureType = ParseCreatureTypeName(ctName);
				}

				if (done)
					continue;

				// type match (case-insensitive)
				std::string a = qtypeName, b = expectedType;
				std::transform(a.begin(), a.end(), a.begin(), ::tolower);
				std::transform(b.begin(), b.end(), b.begin(), ::tolower);
				if (a != b)
					continue;

				// 1) je-li v katalogu vyplněn creature_entry (>0), musí sedět
				//    (povolené i alternativy z customs.gv_quest_creature_multi)
				if (cfgCreatureEntry > 0)
				{
					if (!expectedCreatureEntry.has_value())
						continue;
				
					uint32 killEntry = expectedCreatureEntry.value();
					if (!MatchCreatureForQuest(qid, cfgCreatureEntry, killEntry))
						continue;
				}

				// 2) je-li v katalogu vyplněn item_entry (>0), musí sedět
				if (cfgItemEntry > 0)
				{
					if (!expectedItemEntry.has_value() || cfgItemEntry != expectedItemEntry.value())
						continue;
				}

				// 3) je-li v katalogu vyplněn creature_type (>0), musí sedět
				if (cfgCreatureType > 0)
				{
					if (!expectedCreatureType.has_value() || cfgCreatureType != expectedCreatureType.value())
						continue;
				}

				// --- progres / dokončení ---
				uint32 newProg = std::min(prog + 1, goal);
				bool   nowDone = (newProg >= goal);

				WorldDatabase.DirectExecute(Acore::StringFormat(
					"UPDATE customs.gv_guild_quests "
					"SET progress={}, completed={} "
					"WHERE guildId={} AND reset_type={} AND quest_id={}",
					newProg, (uint32)nowDone, guildId, (uint32)rt, qid).c_str());
				
				if (!nowDone && actor && actor->GetGuildId() == guildId)
				{
					std::string qname = (LangOpt()==Lang::EN ? qname_en : qname_cs);
					if (qname.empty()) qname = (LangOpt()==Lang::EN ? "Quest" : "Úkol");
				
					// Postup řádek (cs/en)
					std::string line;
					if (LangOpt()==Lang::EN)
						line = Acore::StringFormat("{} - Progress: {}/{}", qname, newProg, goal);
					else
						line = Acore::StringFormat("{} - Postup: {}/{}", qname, newProg, goal);
				
					if (WorldSession* s = actor->GetSession())
						ChatHandler(s).SendSysMessage(line);
				}
				
				if (nowDone)
				{
					// odměna + broadcast + mail per–quest
					RewardApplyResult res = GrantRewardsToGuild(guildId, qid);

					WorldDatabase.DirectExecute(Acore::StringFormat(
						"UPDATE customs.gv_guild_quests SET reward_claimed=1 "
						"WHERE guildId={} AND reset_type={} AND quest_id={}",
						guildId, (uint32)rt, qid).c_str());

					const bool en = (LangOpt()==Lang::EN);
					std::string head = (rt == ResetType::Daily)
						? (en ? "Daily quest completed. Reward "
							  : "Denní úkol byl dokončen. Odměna ")
						: (en ? "Weekly quest completed. Reward "
							  : "Týdenní úkol byl dokončen. Odměna ");
					std::string tail = BuildGrantedRewardText(res.adds);
					GuildBroadcastSystem(guildId, head + tail);

					if (!res.items.empty())
						SendItemRewardsToGuildMaster(guildId, res.items);
				}
			}
			while (r->NextRow());
		}
	}	

    // === PUBLIC helpery, aby je mohly volat hooky v druhém .cpp ===

    void GV_QuestProgress_OnCraft(Player* player, std::string const& questType)
    {
        if (!player) return;
        Guild* g = player->GetGuild();
        if (!g) return;
        uint32 gid = g->GetId();
        if (!GuildHasQuestsUpgrade(gid)) return;

        IncrementProgressIfQuestType(gid, ResetType::Daily,  questType, std::nullopt, std::nullopt, std::nullopt, player);
		IncrementProgressIfQuestType(gid, ResetType::Weekly, questType, std::nullopt, std::nullopt, std::nullopt, player);
    }

	void GV_QuestProgress_OnCraftItem(Player* player, uint32 itemEntry)
	{
		if (!player || !itemEntry) return;
		Guild* g = player->GetGuild();
		if (!g) return;
		uint32 gid = g->GetId();
		if (!GuildHasQuestsUpgrade(gid)) return;

		IncrementProgressIfQuestType(gid, ResetType::Daily,  "craft_item", std::nullopt, itemEntry, std::nullopt, player);
		IncrementProgressIfQuestType(gid, ResetType::Weekly, "craft_item", std::nullopt, itemEntry, std::nullopt, player);
	}

    void GV_QuestProgress_OnLoot(Player* player, std::string const& questType)
    {
        if (!player) return;
        Guild* g = player->GetGuild();
        if (!g) return;
        uint32 gid = g->GetId();
        if (!GuildHasQuestsUpgrade(gid)) return;

        IncrementProgressIfQuestType(gid, ResetType::Daily,  questType, std::nullopt, std::nullopt, std::nullopt, player);
		IncrementProgressIfQuestType(gid, ResetType::Weekly, questType, std::nullopt, std::nullopt, std::nullopt, player);
    }

	// TAP kredit: 1 bod za GUILDU (oba reset typy) – KONKRÉTNÍ ENTRY
	void GV_QuestProgress_TapCredit_KillCreature(uint32 guildId, uint32 creatureEntry, Player* notifier)
	{
		if (!guildId || !creatureEntry) return;
		if (!GuildHasQuestsUpgrade(guildId)) return;
	
		IncrementProgressIfQuestType(guildId, ResetType::Daily,  "kill_creature", creatureEntry, std::nullopt, std::nullopt, notifier);
		IncrementProgressIfQuestType(guildId, ResetType::Weekly, "kill_creature", creatureEntry, std::nullopt, std::nullopt, notifier);
	}
	
	// TAP kredit: 1 bod za GUILDU (oba reset typy) – KONKRÉTNÍ TYP
	void GV_QuestProgress_TapCredit_KillCreatureType(uint32 guildId, uint8 creatureType, Player* notifier)
	{
		if (!guildId || !creatureType) return;
		if (!GuildHasQuestsUpgrade(guildId)) return;
	
		IncrementProgressIfQuestType(guildId, ResetType::Daily,  "kill_creature_type",
                             std::nullopt, std::nullopt, creatureType, notifier);
		IncrementProgressIfQuestType(guildId, ResetType::Weekly, "kill_creature_type",
                             std::nullopt, std::nullopt, creatureType, notifier);
	}

    void GV_QuestProgress_OnPvPKill(Player* player)
    {
        if (!player) return;
        Guild* g = player->GetGuild();
        if (!g) return;
        uint32 gid = g->GetId();
        if (!GuildHasQuestsUpgrade(gid)) return;

        IncrementProgressIfQuestType(gid, ResetType::Daily,  "pvp_kill", std::nullopt, std::nullopt, std::nullopt, player);
		IncrementProgressIfQuestType(gid, ResetType::Weekly, "pvp_kill", std::nullopt, std::nullopt, std::nullopt, player);
    }

    // Přesunutá logika dungeon boss kill (z původního Quests_Player)
	void GV_QuestProgress_OnDungeonBossKill(Player* killer, Creature* killed)
	{
		if (!killer || !killed) return;
	
		Guild* g = killer->GetGuild();
		if (!g) return;
		uint32 gid = g->GetId();
		if (!GuildHasQuestsUpgrade(gid)) return;
	
		Map* map = killer->GetMap();
		if (!map || !map->IsNonRaidDungeon()) return;
		if (!killed->IsDungeonBoss()) return;
	
		if (CFG_ONLY_WOTLK_DUNGEONS && !IsWotlkDungeon(map->GetId()))
			return;
	
		// NEW: heroic boss => přičíst heroic i normal; normal boss => jen normal
		if (map->IsHeroic())
		{
			IncrementProgressIfQuestType(gid, ResetType::Daily,  "kill_boss_heroic", std::nullopt, std::nullopt, std::nullopt, killer);
			IncrementProgressIfQuestType(gid, ResetType::Weekly, "kill_boss_heroic", std::nullopt, std::nullopt, std::nullopt, killer);
			IncrementProgressIfQuestType(gid, ResetType::Daily,  "kill_boss_normal", std::nullopt, std::nullopt, std::nullopt, killer);
			IncrementProgressIfQuestType(gid, ResetType::Weekly, "kill_boss_normal", std::nullopt, std::nullopt, std::nullopt, killer);
		}
		else
		{
			IncrementProgressIfQuestType(gid, ResetType::Daily,  "kill_boss_normal", std::nullopt, std::nullopt, std::nullopt, killer);
			IncrementProgressIfQuestType(gid, ResetType::Weekly, "kill_boss_normal", std::nullopt, std::nullopt, std::nullopt, killer);
		}

	}

	// --- GAMEOBJECT gossip (bulletin board) ---
	class go_gv_quests : public GameObjectScript
	{
	public:
		go_gv_quests() : GameObjectScript("go_gv_quests") {}
	
		// Akce pro router
		enum Actions : uint32
		{
			ACT_ROOT                 = 100,
			ACT_OPEN_DAILY           = 110,
			ACT_OPEN_WEEKLY          = 120,
			ACT_BACK_TO_ROOT         = 190,
	
			// Slot detaily
			ACT_OPEN_DAILY_SLOT_BASE  = 2000, // +slot (1..10)
			ACT_OPEN_WEEKLY_SLOT_BASE = 3000, // +slot (1..10)
	
			// „Neakční“ kliky (info/separátor) – jen překreslí aktuální obrazovku
			ACT_SEP_DAILY_LIST      = 141,   // seznam denních
			ACT_SEP_WEEKLY_LIST     = 142,   // seznam týdenních
		};
	
		// "Neakční" řádek: pořád sender=GOSSIP_SENDER_MAIN, ale akce jen překreslí aktuální obrazovku
		static void AddNoop(Player* player, std::string const& text, uint32 actionForThisPage)
		{
			AddGossipItemFor(player, /*icon*/0, text, GOSSIP_SENDER_MAIN, actionForThisPage);
		}
		static void AddSeparator(Player* player, uint32 actionForThisPage)
		{
			AddGossipItemFor(player, /*icon*/0, SeparatorLine(), GOSSIP_SENDER_MAIN, actionForThisPage);
		}
	
		static std::string GreenDone()
		{
			return Acore::StringFormat("|cff00cc00({})|r", T("Dokončeno", "Completed"));
		}
	
		// ------------------------------
		// DETAIL ÚKOLU
		// ------------------------------
		static void ShowQuestDetail(Player* player, GameObject* go, ResetType rt, uint8 slot, bool backToList)
		{
			ClearGossipMenuFor(player);
			Guild* g = player->GetGuild();
			if (!g)
			{
				ChatHandler(player->GetSession()).SendSysMessage(T("Nejsi v guildě.", "You are not in a guild."));
				SendGossipMenuFor(player, 1, go->GetGUID());
				return;
			}
	
			auto rows = LoadGuildQuests(g->GetId(), rt);
			auto it = std::find_if(rows.begin(), rows.end(), [&](GuildQuestRow const& r){ return r.slot == slot; });
			if (it == rows.end())
			{
				// fallback – žádný slot
				uint32 backAction = (backToList && GetQuestAmount(rt) > 1)
					? ((rt==ResetType::Daily) ? ACT_OPEN_DAILY : ACT_OPEN_WEEKLY)
					: ACT_BACK_TO_ROOT;
	
				AddNoop(player, LangOpt()==Lang::EN ? "No quest." : "Žádný úkol.", backAction);
				AddSeparator(player, backAction);
				AddGossipItemFor(player, GOSSIP_ICON_TAXI, T("Zpátky", "Back"), GOSSIP_SENDER_MAIN, backAction);
				SendGossipMenuFor(player, 1, go->GetGUID());
				return;
			}
	
			auto const& row = *it;
	
			// „Neakční“ kliky v detailu přesměrují zpět na TEN SAMÝ detail (slot)
			uint32 noopAction = (rt==ResetType::Daily)
				? (uint32)(ACT_OPEN_DAILY_SLOT_BASE  + row.slot)
				: (uint32)(ACT_OPEN_WEEKLY_SLOT_BASE + row.slot);
	
			// INFO + (volitelně) ÚKOL – podle počtu konfigurovaných úkolů
			{
				const bool en = (LangOpt()==Lang::EN);
				uint32 amount = GetQuestAmount(rt);
				bool singleConfigured = (amount == 1);
				bool manyConfigured   = (amount > 2);
			
				std::string name = en ? row.questName_en : row.questName_cs;
				if (name.empty()) name = en ? "Quest" : "Úkol";
			
				std::string info = en ? row.info_en : row.info_cs;
				if (info.empty()) info = "—";
			
				std::string L_Quest = en ? "Quest: " : "Úkol: ";
				std::string L_Info  = "Info: ";
			
				// 1) Pokud je konfigurovaný jen 1 úkol → zobrazí „Úkol:“ i „Info:“
				if (singleConfigured)
				{
					AddNoop(player, L_Quest + name, noopAction);
					AddNoop(player, L_Info  + info, noopAction);
				}
				else
				{
					// 2) Při víc než 2 úkolech (i obecně pro více úkolů) chci jen „Info:“
					// (Požadavek říká „víc jak 2“; pro 2 zachovávám bez „Úkol:“.)
					AddNoop(player, L_Info + info, noopAction);
				}
			}

			// Progress
			{
				std::string prog = (LangOpt()==Lang::EN ? "Progress: " : "Postup: ");
				prog += std::to_string(row.progress) + "/" + std::to_string(row.goal);
				AddNoop(player, prog, noopAction);
			}
	
			// Reward
			AddNoop(player, BuildRewardLine(row.questId), noopAction);
	
			// Status
			{
				std::string st = (LangOpt()==Lang::EN ? "Status: " : "Stav: ");
				if (row.completed)
					st += (LangOpt()==Lang::EN ? "|cff00cc00Quest completed|r" : "|cff00cc00Úkol splněný|r");
				else
					st += (LangOpt()==Lang::EN ? "|cffff0000Quest not completed yet|r" : "|cffff0000Úkol ještě není dokončen|r");
				AddNoop(player, st, noopAction);
			}
	
			// oddělovač (neakční) + Back (jediné akční tlačítko)
			AddSeparator(player, noopAction);
			uint32 backAction = (backToList && GetQuestAmount(rt) > 1)
				? ((rt==ResetType::Daily) ? ACT_OPEN_DAILY : ACT_OPEN_WEEKLY)
				: ACT_BACK_TO_ROOT;
	
			AddGossipItemFor(player, GOSSIP_ICON_TAXI, T("Zpátky", "Back"), GOSSIP_SENDER_MAIN, backAction);
			SendGossipMenuFor(player, 1, go->GetGUID());
		}
	
		// ------------------------------
		// SEZNAM ÚKOLŮ (kategorie Denní / Týdenní)
		// ------------------------------
		static void ShowQuestList(Player* player, GameObject* go, ResetType rt)
		{
			ClearGossipMenuFor(player);
			Guild* g = player->GetGuild();
			if (!g)
			{
				ChatHandler(player->GetSession()).SendSysMessage(T("Nejsi v guildě.", "You are not in a guild."));
				SendGossipMenuFor(player, 1, go->GetGUID());
				return;
			}
		
			auto rows = LoadGuildQuests(g->GetId(), rt);
			uint32 sepAct = (rt==ResetType::Daily) ? ACT_SEP_DAILY_LIST : ACT_SEP_WEEKLY_LIST;
		
			std::string header = (rt==ResetType::Daily)
				? (LangOpt()==Lang::EN ? "Daily quests" : "Denní úkoly")
				: (LangOpt()==Lang::EN ? "Weekly quests" : "Týdenní úkoly");
		
			if (rows.empty())
			{
				AddNoop(player, header + ": " + (LangOpt()==Lang::EN ? "No quest." : "Žádný úkol."), sepAct);
				AddNoop(player, LangOpt()==Lang::EN ? "Progress: —" : "Postup: —", sepAct);
				AddNoop(player, LangOpt()==Lang::EN ? "Reward: —"  : "Odměna: —", sepAct);
				AddNoop(player, LangOpt()==Lang::EN ? "Status: waiting for assignment" : "Stav: čeká na přiřazení", sepAct);
				AddSeparator(player, sepAct);
				AddGossipItemFor(player, GOSSIP_ICON_TAXI, T("Zpátky", "Back"), GOSSIP_SENDER_MAIN, ACT_BACK_TO_ROOT);
				SendGossipMenuFor(player, 1, go->GetGUID());
				return;
			}
		
			// Pokud je v configu jen 1 úkol, rovnou detail
			if (GetQuestAmount(rt) == 1 && rows.size() == 1)
			{
				ShowQuestDetail(player, go, rt, rows[0].slot, /*backToList*/false);
				return;
			}
		
			for (auto const& row : rows)
			{
				std::string name = (LangOpt()==Lang::EN ? row.questName_en : row.questName_cs);
				if (name.empty()) name = (LangOpt()==Lang::EN ? row.info_en : row.info_cs);
				if (name.empty()) name = (LangOpt()==Lang::EN ? "Quest" : "Úkol");
		
				if (row.completed) name += " " + GreenDone();
		
				uint32 act = (rt==ResetType::Daily)
					? (uint32)(ACT_OPEN_DAILY_SLOT_BASE  + row.slot)
					: (uint32)(ACT_OPEN_WEEKLY_SLOT_BASE + row.slot);
		
				AddGossipItemFor(player, GOSSIP_ICON_TRAINER, name, GOSSIP_SENDER_MAIN, act);
			}
		
			// oddělovač (neakční) + Back
			AddSeparator(player, sepAct);
			AddGossipItemFor(player, GOSSIP_ICON_TAXI, T("Zpátky", "Back"), GOSSIP_SENDER_MAIN, ACT_BACK_TO_ROOT);
			SendGossipMenuFor(player, 1, go->GetGUID());
		}
	
		// ------------------------------
		// ROOT MENU (Denní úkoly / Týdenní úkoly)
		// ------------------------------
		static void ShowRootGO(Player* player, GameObject* go)
		{
			ClearGossipMenuFor(player);
	
			Guild* g = player->GetGuild();
			if (!g)
			{
				ChatHandler(player->GetSession()).SendSysMessage(T("Nejsi v guildě.", "You are not in a guild."));
				SendGossipMenuFor(player, 1, go->GetGUID());
				return;
			}
	
			if (!GuildHasQuestsUpgrade(g->GetId()))
			{
				ChatHandler(player->GetSession()).SendSysMessage(
					T("Tvoje guilda nemá zakoupené rozšíření „Quests“.","Your guild hasn't purchased the \"Quests\" expansion."));
				SendGossipMenuFor(player, 1, go->GetGUID());
				return;
			}
	
			// Rotace/assign při otevření GO
			EnsureGuildQuestAssigned(g->GetId(), ResetType::Daily);
			EnsureGuildQuestAssigned(g->GetId(), ResetType::Weekly);
	
			auto rowsDaily  = LoadGuildQuests(g->GetId(), ResetType::Daily);
			auto rowsWeekly = LoadGuildQuests(g->GetId(), ResetType::Weekly);
	
			// Denní položka + (Dokončeno) pokud je jen 1 a je complete
			{
				std::string label = (LangOpt()==Lang::EN) ? "Daily quests" : "Denní úkoly";
				if (CFG_DAILY_AMOUNT == 1 && rowsDaily.size() == 1 && rowsDaily[0].completed)
					label += " " + GreenDone();
				AddGossipItemFor(player, GOSSIP_ICON_TRAINER, label, GOSSIP_SENDER_MAIN, ACT_OPEN_DAILY);
			}
	
			// Týdenní položka + (Dokončeno) pokud je jen 1 a je complete
			{
				std::string label = (LangOpt()==Lang::EN) ? "Weekly quests" : "Týdenní úkoly";
				if (CFG_WEEKLY_AMOUNT == 1 && rowsWeekly.size() == 1 && rowsWeekly[0].completed)
					label += " " + GreenDone();
				AddGossipItemFor(player, GOSSIP_ICON_TRAINER, label, GOSSIP_SENDER_MAIN, ACT_OPEN_WEEKLY);
			}
	
			SendGossipMenuFor(player, 1, go->GetGUID());
		}
	
		// ------------------------------
		// HOOKY
		// ------------------------------
		bool OnGossipHello(Player* player, GameObject* go) override
		{
			Guild* g = player->GetGuild();
			if (!g)
			{
				ChatHandler(player->GetSession()).SendSysMessage(T("Nejsi v guildě.", "You are not in a guild."));
				return true;
			}
			ShowRootGO(player, go);
			return true;
		}
	
		bool OnGossipSelect(Player* player, GameObject* go, uint32 sender, uint32 action) override
		{
			// jakákoliv kliknutí na neakční řádky/separátory (sender!=GOSSIP_SENDER_MAIN) se ignorují
			if (sender != GOSSIP_SENDER_MAIN)
				return false;
	
			switch (action)
			{
				case ACT_ROOT:
				case ACT_BACK_TO_ROOT:
					ShowRootGO(player, go);
					return true;
	
				case ACT_OPEN_DAILY:
					ShowQuestList(player, go, ResetType::Daily);
					return true;
	
				case ACT_OPEN_WEEKLY:
					ShowQuestList(player, go, ResetType::Weekly);
					return true;
	
				// „Neakční“ kliky v seznamech – jen překreslit seznam
				case ACT_SEP_DAILY_LIST:
					ShowQuestList(player, go, ResetType::Daily);
					return true;
				case ACT_SEP_WEEKLY_LIST:
					ShowQuestList(player, go, ResetType::Weekly);
					return true;
	
				default:
					break;
			}
	
			// Slot detail – denní
			if (action >= ACT_OPEN_DAILY_SLOT_BASE && action < ACT_OPEN_DAILY_SLOT_BASE + 100)
			{
				uint8 slot = uint8(action - ACT_OPEN_DAILY_SLOT_BASE);
				ShowQuestDetail(player, go, ResetType::Daily, slot, /*backToList=*/true);
				return true;
			}
	
			// Slot detail – týdenní
			if (action >= ACT_OPEN_WEEKLY_SLOT_BASE && action < ACT_OPEN_WEEKLY_SLOT_BASE + 100)
			{
				uint8 slot = uint8(action - ACT_OPEN_WEEKLY_SLOT_BASE);
				ShowQuestDetail(player, go, ResetType::Weekly, slot, /*backToList=*/true);
				return true;
			}
	
			// Fallback → root
			ShowRootGO(player, go);
			return true;
		}
	};

    class Quests_World : public WorldScript
    {
    public:
        Quests_World() : WorldScript("guild_village_quests_World") {}
        void OnAfterConfigLoad(bool /*reload*/) override { LoadCfg(); }
        void OnStartup() override { LoadCfg(); }
    };
} // namespace GuildVillage

void RegisterGuildVillageQuests()
{
    new GuildVillage::go_gv_quests();
    new GuildVillage::Quests_World();
}
