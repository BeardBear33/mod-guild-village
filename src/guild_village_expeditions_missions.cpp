// modules/mod-guild-village/src/guild_village_expeditions_missions.cpp

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
#include "StringFormat.h"
#include "GuildMgr.h"
#include "Define.h"
#include "World.h"
#include "GameTime.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "gv_names.h"
using namespace GuildVillage::Names;

#include <string>
#include <algorithm>
#include <optional>
#include <cstdint>
#include <vector>
#include <sstream>
#include <unordered_map>
#include <random>
#include <unordered_set>
#include <ctime>
#include <cstdio>

namespace GuildVillageMissions
{
	// kontrola+zápis pro guild watch
	static std::vector<uint32> LoadAllGuildIds()
	{
		std::vector<uint32> out;
		if (QueryResult r = CharacterDatabase.Query(
			"SELECT guildid FROM guild"))
		{
			do
			{
				Field* f = r->Fetch();
				out.push_back(f[0].Get<uint32>());
			}
			while (r->NextRow());
		}
		return out;
	}

	// stránkování velikosti
    static constexpr uint32 LOOTBANK_PAGE_SIZE     = 10;
    static constexpr uint32 LOOT_MEMBER_PAGE_SIZE  = 10;
	
	// balení dvou 16bit hodnot do jednoho uint32
	static inline uint32 Pack16_16(uint16 high, uint16 low)
	{
		return (uint32(high) << 16) | uint32(low);
	}
	
	static inline uint16 UnpackHigh16(uint32 v)
	{
		return uint16((v >> 16) & 0xFFFF);
	}
	
	static inline uint16 UnpackLow16(uint32 v)
	{
		return uint16(v & 0xFFFF);
	}
	
	static inline uint32 EncodeItemAndPage(uint32 lootIndex, uint32 pageMembers)
	{
		return lootIndex * 1000u + pageMembers;
	}
	static inline void DecodeItemAndPage(uint32 code, uint32& lootIndexOut, uint32& pageOut)
	{
		lootIndexOut = code / 1000u;
		pageOut      = code % 1000u;
	}
	
	static inline uint32 EncodeGive(uint32 lootIndex, uint32 memberIndex)
	{
		return Pack16_16(uint16(lootIndex & 0xFFFF), uint16(memberIndex & 0xFFFF));
	}
	
	static inline void DecodeGive(uint32 code, uint32& lootIndexOut, uint32& memberIndexOut)
	{
		lootIndexOut    = UnpackHigh16(code);
		memberIndexOut  = UnpackLow16(code);
	}

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
    // Práva (GM / Officer)
    // --------------------------
    static inline bool IsGuildLeaderOrOfficer(Player* player)
    {
        if (!player) return false;
        Guild* g = player->GetGuild();
        if (!g) return false;

        // Guild Master
        if (g->GetLeaderGUID() == player->GetGUID())
            return true;

        // Officer rank 1 pokud povoleno configem
        if (sConfigMgr->GetOption<bool>("GuildVillage.Expeditions.OfficersCanManage", true))
            if (auto m = g->GetMember(player->GetGUID()))
                return m->GetRankId() == 1;

        return false;
    }

    // --------------------------
    // Stav expedic guildy
    // --------------------------
    struct ExpGuildRow
    {
        uint32 guildId     = 0;
        uint8  owned       = 0;   // zakoupení hrdinové CELKEM
        uint8  onMission   = 0;   // kolik je momentálně pryč
        uint8  maxHeroes   = 25;
        uint16 gearLevel   = 182;   // uložený iLvl družiny (baseline 182)
    };

    static std::optional<ExpGuildRow> LoadGuildExpData(uint32 guildId)
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT heroes_owned, heroes_on_mission, heroes_max, gear_level "
                "FROM customs.gv_expedition_guild WHERE guildId={}", guildId))
        {
            Field* f = r->Fetch();
            ExpGuildRow row;
            row.guildId   = guildId;
            row.owned     = f[0].Get<uint8>();
            row.onMission = f[1].Get<uint8>();
            row.maxHeroes = f[2].Get<uint8>();
            row.gearLevel = f[3].Get<uint16>();
            return row;
        }

        return std::nullopt;
    }

    static inline uint8 GetAvailableHeroes(ExpGuildRow const& row)
    {
        if (row.owned <= row.onMission)
            return 0;
        return row.owned - row.onMission;
    }

	static inline uint16 DisplayIlvl(uint16 stored)
	{
		return stored;
	}

    // --------------------------
    // Prahy pro dostupnost kategorií
    // --------------------------
	
	// Dungeon normal: min 1 owned (unlock kategorie), gear >=182
    static inline uint8  Req_Normal_Heroes() { return 1; }
    static inline uint16 Req_Normal_Gear() { return 182; }

	// Dungeon hc: min 1 owned (unlock kategorie), gear >=200
    static inline uint8  Req_Heroic_Heroes() { return 1; }
    static inline uint16 Req_Heroic_Gear() { return 200; }

    // Raid 10 man: min 10 owned (unlock kategorie), gear >=213
    static inline uint8  Req_Raid10_Heroes() { return 6; }
    static inline uint16 Req_Raid10_Gear() { return 200; }

    // Raid 25 man: min 25 owned, gear zatím 999 (později)
    static inline uint8  Req_Raid25_Heroes() { return 11; }
    static inline uint16 Req_Raid25_Gear() { return 213; }

    // Splňuje guilda požadavky na kategorii?
    static bool MeetsReq(uint8 haveHeroes, uint16 haveGear,
                         uint8 needHeroes, uint16 needGear)
    {
        if (haveHeroes < needHeroes)
            return false;
        if (haveGear   < needGear)
            return false;
        return true;
    }

    // --------------------------
    // Informace o misi (instance)
    // --------------------------
    enum class MissionType
    {
        NORMAL_5,
        HEROIC_5,
        RAID_10,
        RAID_25
    };

    struct MissionInfo
    {
        MissionType type;
        std::string name;
        uint8 maxIdealHeroes;
    };
	
	// --------------------------
    // Požadavky pro extraChance
    // --------------------------
	// kolik ilvl se vyžaduje, pro nárok na extraChance v daném typu mise
	static uint16 ExtraReqIlvl(MissionType t)
	{
		switch (t)
		{
			case MissionType::NORMAL_5:
				return (uint16)sConfigMgr->GetOption<uint32>(
					"GuildVillage.Expeditions.normal.extrachance.req.ilvl", 0);
	
			case MissionType::HEROIC_5:
				return (uint16)sConfigMgr->GetOption<uint32>(
					"GuildVillage.Expeditions.hc.extrachance.req.ilvl", 0);
	
			case MissionType::RAID_10:
				return (uint16)sConfigMgr->GetOption<uint32>(
					"GuildVillage.Expeditions.10m.extrachance.req.ilvl", 0);
	
			case MissionType::RAID_25:
				return (uint16)sConfigMgr->GetOption<uint32>(
					"GuildVillage.Expeditions.25m.extrachance.req.ilvl", 0);
		}
		return 0;
	}
	
	// kolik celkově "owned heroes" se vyžaduje, pro nárok na extraChance
	static uint8 ExtraReqHeroes(MissionType t)
	{
		switch (t)
		{
			case MissionType::NORMAL_5:
				return (uint8)sConfigMgr->GetOption<uint32>(
					"GuildVillage.Expeditions.normal.extrachance.req.hero", 0);
	
			case MissionType::HEROIC_5:
				return (uint8)sConfigMgr->GetOption<uint32>(
					"GuildVillage.Expeditions.hc.extrachance.req.hero", 0);
	
			case MissionType::RAID_10:
				return (uint8)sConfigMgr->GetOption<uint32>(
					"GuildVillage.Expeditions.10m.extrachance.req.hero", 0);
	
			case MissionType::RAID_25:
				return (uint8)sConfigMgr->GetOption<uint32>(
					"GuildVillage.Expeditions.25m.extrachance.req.hero", 0);
		}
		return 0;
	}
	
	// kolik hrdinů je "full team" pro daný typ mise
	static uint8 FullTeamSize(MissionType t)
	{
		switch (t)
		{
			case MissionType::NORMAL_5:
			case MissionType::HEROIC_5:
				return 5;
			case MissionType::RAID_10:
				return 10;
			case MissionType::RAID_25:
				return 25;
		}
		return 0;
	}

    // --------------------------
    // Cena mise (tabulka customs.gv_expedition_mission_cost)
    // --------------------------
    struct MissionCost
    {
        uint32 mat1 = 0;
        uint32 mat2 = 0;
        uint32 mat3 = 0;
        uint32 mat4 = 0;
        uint32 gold = 0;
    };

    static std::optional<MissionCost> LoadMissionCost(std::string const& missionName)
    {
        if (QueryResult r = WorldDatabase.Query(
				"SELECT cost_mat1, cost_mat2, cost_mat3, cost_mat4, cost_gold "
				"FROM customs.gv_expedition_mission_cost WHERE mission_name='{}'",
				missionName))
        {
            Field* f = r->Fetch();
            MissionCost mc;
            mc.mat1 = f[0].Get<uint32>();
            mc.mat2 = f[1].Get<uint32>();
            mc.mat3 = f[2].Get<uint32>();
            mc.mat4 = f[3].Get<uint32>();
            mc.gold = f[4].Get<uint32>();
            return mc;
        }
        return std::nullopt;
    }

    // --------------------------
    // Konfig mise (.conf)
    // --------------------------
    struct MissionConf
    {
        uint32 amount = 0;          // kolik itemů rollnout
        std::string itemids;        // "123,456,789"
        uint32 timeSeconds = 0;     // délka expedice v sekundách
        uint8  extraChance = 0;     // bonus v "%" pokud splňujem podmínky
    };

	static std::string ConfKeyFromMissionName(std::string missionName)
	{
		std::string key;
		key.reserve(missionName.size());
	
		for (char c : missionName)
		{
			if (c == '(' || c == ')')
				continue;
	
			if (c == ' ' || c == '-' || c == '\'')
			{
				if (key.empty() || key.back() != '_')
					key.push_back('_');
				continue;
			}
	
			key.push_back(c);
		}
	
		while (!key.empty() && key.back() == '_')
			key.pop_back();
	
		return key;
	}

    static MissionConf LoadMissionConf(std::string const& missionName)
    {
        MissionConf out;
        std::string baseKey = "GuildVillage.Expeditions." + ConfKeyFromMissionName(missionName);

        out.amount      = sConfigMgr->GetOption<uint32>(baseKey + ".amount", 0);
        out.itemids     = sConfigMgr->GetOption<std::string>(baseKey + ".itemids", "");
        out.timeSeconds = sConfigMgr->GetOption<uint32>(baseKey + ".time", 3600);
        out.extraChance = (uint8)sConfigMgr->GetOption<uint32>(baseKey + ".extrachance", 0);

        return out;
    }
	
	// --------------------------
	// Submenu požadavky z DB: customs.gv_expedition_requirements
	// Sloupce: mission_name VARCHAR, ilvl INT, heroes INT
	// --------------------------
	struct MissionReq
	{
		uint16 ilvl  = 0;
		uint8  heroes = 0;
	};
	
	static MissionReq LoadMissionReq(std::string const& missionName)
	{
		MissionReq r{};
		if (QueryResult q = WorldDatabase.Query(
				"SELECT ilvl, heroes FROM customs.gv_expedition_requirements WHERE mission_name='{}' LIMIT 1",
				missionName))
		{
			Field* f = q->Fetch();
			r.ilvl   = f[0].Get<uint16>();
			r.heroes = f[1].Get<uint8>();
		}
		return r;
	}
	
	static bool ShouldShowMissionInSubmenu(std::string const& missionName, ExpGuildRow const& info)
	{
		MissionReq req = LoadMissionReq(missionName);
		uint16 haveIlvl = DisplayIlvl(info.gearLevel);
		uint8  haveOwned = info.owned;
	
		if (req.ilvl   != 0 && haveIlvl   < req.ilvl)   return false;
		if (req.heroes != 0 && haveOwned  < req.heroes) return false;
	
		return true;
	}
	
	// Malý helper: přidá položku do submenu jen pokud projde ShouldShowMissionInSubmenu
	static void AddMissionIfAllowed(Player* player, Creature* creature, ExpGuildRow const& info,
									char const* missionName, uint32 action)
	{
		if (ShouldShowMissionInSubmenu(missionName, info))
		{
			AddGossipItemFor(player, GOSSIP_ICON_TRAINER, missionName, GOSSIP_SENDER_MAIN, action);
		}
	}

    // --------------------------
    // Odečet currency + goldy hráče
    // --------------------------
    static bool TryDeductGuildCurrencyAndGold(
        uint32 guildId, Player* buyer,
        uint32 needMat1, uint32 needMat2,
        uint32 needMat3, uint32 needMat4,
        uint32 needGold)
    {
        if (!buyer)
            return false;

        // načtení měny guildy
        uint64 m1=0, m2=0, m3=0, m4=0;
        if (QueryResult q = WorldDatabase.Query(
                "SELECT material1, material2, material3, material4 "
                "FROM customs.gv_currency WHERE guildId={}", guildId))
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

        // check currency
        if (m1 < needMat1 || m2 < needMat2 || m3 < needMat3 || m4 < needMat4)
            return false;

        // check gold hráče
        uint64 needCopper = (uint64)needGold * 10000ULL;
        if (buyer->GetMoney() < needCopper)
            return false;

        // odečíst currency guildy
        WorldDatabase.DirectExecute(
            "UPDATE customs.gv_currency SET "
            "material1 = material1 - {}, "
            "material2 = material2 - {}, "
            "material3 = material3 - {}, "
            "material4 = material4 - {}, "
            "last_update = NOW() "
            "WHERE guildId = {}",
            needMat1, needMat2, needMat3, needMat4, guildId);

        // odečíst goldy hráče
        if (needCopper > 0)
            buyer->ModifyMoney(-(int64)needCopper);

        return true;
    }

    // --------------------------
    // Gossip action IDs
    // --------------------------
    enum GossipAction : uint32
    {
		ACT_NOOP_ROOT        = 1,     // separator / inertní věci v root menu
        ACT_NOOP_NORMAL      = 2,     // separator v normal dungu
        ACT_NOOP_HEROIC      = 3,     // separator v heroic dungu
        ACT_NOOP_RAID10      = 4,     // separator v raid10 menu
        ACT_NOOP_RAID25      = 5,     // separator v raid25 menu
        ACT_NOOP_LOOT_ROOT   = 6,     // separator v lootbank root
		ACT_NOOP_CONFIRM_BASE     = 800000000,     // separator v potvzení
		ACT_NOOP_LOOT_MEMBER_BASE = 500000000,     // separator ve výběru hráče
		
        // root
        ACT_MISSIONS_ROOT        = 50000,

        // vstup do podkategorií
        ACT_SELECT_NORMAL        = 50010,
        ACT_SELECT_HEROIC        = 50020,
        ACT_SELECT_RAID10        = 50030,
        ACT_SELECT_RAID25        = 50040,

        // submenu jednotlivých kategorií
        ACT_MENU_NORMAL          = 50100,
        ACT_MENU_HEROIC          = 50200,
        ACT_MENU_RAID10          = 50300,
        ACT_MENU_RAID25          = 50400,

        // výběr expedice -> potvrzení
        ACT_CONFIRM_MISSION_BASE = 600000000,

        // potvrzený start expedice
        ACT_LAUNCH_MISSION_BASE  = 700000000,

        // LOOT BANK root tlačítko
        ACT_LOOTBANK_ROOT        = 80000,

        // stránkování v rootu loot banky
        // action = ACT_LOOTBANK_PAGE_BASE + page
        ACT_LOOTBANK_PAGE_BASE   = 1000000,

        // klik na item = otevřít seznam hráčů
        // action = ACT_LOOTBANK_ITEM_BASE + encoded(lootRowId16,pageMembers16)
        ACT_LOOTBANK_ITEM_BASE   = 2000000,

        // klik na hráče = poslat loot
        // action = ACT_LOOTBANK_GIVE_BASE + encoded(shortLoot16,shortTarget16)
        ACT_LOOTBANK_GIVE_BASE   = 20000000,

        // zpátky
        ACT_BACK_TO_ROOT         = 89990,
    };
	
    // -------------------------------------------------
    // Mission index mapping
    // -------------------------------------------------
    static std::optional<MissionInfo> MissionFromIndex(uint32 idx)
    {
        // Normal 5-man
        switch (idx)
        {
            case 0:   return MissionInfo{ MissionType::NORMAL_5, "Utgarde Keep", 5 };
            case 1:   return MissionInfo{ MissionType::NORMAL_5, "Utgarde Pinnacle", 5 };
            case 2:   return MissionInfo{ MissionType::NORMAL_5, "The Nexus", 5 };
            case 3:   return MissionInfo{ MissionType::NORMAL_5, "The Oculus", 5 };
            case 4:   return MissionInfo{ MissionType::NORMAL_5, "Culling of Stratholme", 5 };
            case 5:   return MissionInfo{ MissionType::NORMAL_5, "DrakTharon Keep", 5 };
            case 6:   return MissionInfo{ MissionType::NORMAL_5, "Halls of Stone", 5 };
            case 7:   return MissionInfo{ MissionType::NORMAL_5, "Azjol Nerub", 5 };
            case 8:   return MissionInfo{ MissionType::NORMAL_5, "Halls of Lightning", 5 };
            case 9:   return MissionInfo{ MissionType::NORMAL_5, "Gundrak", 5 };
            case 10:  return MissionInfo{ MissionType::NORMAL_5, "Violet Hold", 5 };
            case 11:  return MissionInfo{ MissionType::NORMAL_5, "AhnKahet", 5 };

            case 100: return MissionInfo{ MissionType::NORMAL_5, "Trial of the Champion", 5 };

            case 200: return MissionInfo{ MissionType::NORMAL_5, "Forge of Souls", 5 };
            case 201: return MissionInfo{ MissionType::NORMAL_5, "Halls of Reflection", 5 };
            case 202: return MissionInfo{ MissionType::NORMAL_5, "Pit of Saron", 5 };
        }

        // Heroic 5-man
        switch (idx)
        {
            case 1000: return MissionInfo{ MissionType::HEROIC_5, "Utgarde Keep (HC)", 5 };
            case 1001: return MissionInfo{ MissionType::HEROIC_5, "Utgarde Pinnacle (HC)", 5 };
            case 1002: return MissionInfo{ MissionType::HEROIC_5, "The Nexus (HC)", 5 };
            case 1003: return MissionInfo{ MissionType::HEROIC_5, "The Oculus (HC)", 5 };
            case 1004: return MissionInfo{ MissionType::HEROIC_5, "Culling of Stratholme (HC)", 5 };
            case 1005: return MissionInfo{ MissionType::HEROIC_5, "DrakTharon Keep (HC)", 5 };
            case 1006: return MissionInfo{ MissionType::HEROIC_5, "Halls of Stone (HC)", 5 };
            case 1007: return MissionInfo{ MissionType::HEROIC_5, "Azjol Nerub (HC)", 5 };
            case 1008: return MissionInfo{ MissionType::HEROIC_5, "Halls of Lightning (HC)", 5 };
            case 1009: return MissionInfo{ MissionType::HEROIC_5, "Gundrak (HC)", 5 };
            case 1010: return MissionInfo{ MissionType::HEROIC_5, "Violet Hold (HC)", 5 };
            case 1011: return MissionInfo{ MissionType::HEROIC_5, "AhnKahet (HC)", 5 };

            case 1100: return MissionInfo{ MissionType::HEROIC_5, "Trial of the Champion (HC)", 5 };

            case 1200: return MissionInfo{ MissionType::HEROIC_5, "Halls of Reflection (HC)", 5 };
            case 1201: return MissionInfo{ MissionType::HEROIC_5, "Forge of Souls (HC)", 5 };
            case 1202: return MissionInfo{ MissionType::HEROIC_5, "Pit of Saron (HC)", 5 };
        }

        // Raid 10
        switch (idx)
        {
            case 2000: return MissionInfo{ MissionType::RAID_10, "The Eye of Eternity (10)", 10 };
            case 2001: return MissionInfo{ MissionType::RAID_10, "The Obsidian Sanctum (10)", 10 };
            case 2002: return MissionInfo{ MissionType::RAID_10, "Naxxramas (10)", 10 };
            case 2100: return MissionInfo{ MissionType::RAID_10, "Ulduar (10)", 10 };
            case 2200: return MissionInfo{ MissionType::RAID_10, "Trial of the Crusader (10)", 10 };
            case 2300: return MissionInfo{ MissionType::RAID_10, "IceCrown Citadel (10)", 10 };
            case 2400: return MissionInfo{ MissionType::RAID_10, "The Ruby Sanctum (10)", 10 };
        }

        // Raid 25
        switch (idx)
        {
            case 3000: return MissionInfo{ MissionType::RAID_25, "The Eye of Eternity (25)", 25 };
            case 3001: return MissionInfo{ MissionType::RAID_25, "The Obsidian Sanctum (25)", 25 };
            case 3002: return MissionInfo{ MissionType::RAID_25, "Naxxramas (25)", 25 };
            case 3100: return MissionInfo{ MissionType::RAID_25, "Ulduar (25)", 25 };
            case 3200: return MissionInfo{ MissionType::RAID_25, "Trial of the Crusader (25)", 25 };
            case 3300: return MissionInfo{ MissionType::RAID_25, "IceCrown Citadel (25)", 25 };
            case 3400: return MissionInfo{ MissionType::RAID_25, "The Ruby Sanctum (25)", 25 };
        }

        return std::nullopt;
    }

    // --------------------------
    // Výpočet kolik hrdinů poslat
    // --------------------------
    static uint8 CalculateHeroesToSend(MissionType t, uint8 available)
    {
        switch (t)
        {
            case MissionType::NORMAL_5:
            case MissionType::HEROIC_5:
            {
                if (available == 0)
                    return 0;
                if (available < 5)
                    return available;
                return 5;
            }
            case MissionType::RAID_10:
            {
                if (available < 6)
                    return 0;
                if (available < 10)
                    return available;
                return 10;
            }
            case MissionType::RAID_25:
            {
                if (available < 11)
                    return 0;
                if (available < 25)
                    return available;
                return 25;
            }
        }

        return 0;
    }

    // --------------------------
    // SuccessChance base z gv_expedition_catalog
    // --------------------------
    static uint8 LoadSuccessChanceForCount(uint8 countHeroes)
    {
        if (countHeroes == 0)
            return 0;
        if (countHeroes > 25)
            countHeroes = 25;

        if (QueryResult r = WorldDatabase.Query(
                "SELECT successChance FROM customs.gv_expedition_catalog WHERE slot={}",
                (uint32)countHeroes))
        {
            Field* f = r->Fetch();
            uint8 chance = f[0].Get<uint8>();
            if (chance > 100)
                chance = 100;
            return chance;
        }

        return 100;
    }

    // --------------------------
    // ExtraChance logika
    // --------------------------
    static uint8 LoadMissionExtraChanceFromConf(std::string const& missionName)
    {
        MissionConf conf = LoadMissionConf(missionName);
        return conf.extraChance;
    }

    static uint8 LoadExtraChanceBonus(MissionInfo const& mi, ExpGuildRow const& info, uint8 toSend)
	{
		// aktuální stav guildy
		uint16 guildIlvl     = DisplayIlvl(info.gearLevel);
		uint8  guildOwned    = info.owned;
	
		// požadavky z configu pro ten typ mise
		uint16 needIlvl      = ExtraReqIlvl(mi.type);
		uint8  needHeroes    = ExtraReqHeroes(mi.type);
		uint8  needFullTeam  = FullTeamSize(mi.type);
	
		if (toSend != needFullTeam)
			return 0;
	
		if (needIlvl > 0 && guildIlvl < needIlvl)
			return 0;
	
		if (needHeroes > 0 && guildOwned < needHeroes)
			return 0;
	
		MissionConf conf = LoadMissionConf(mi.name);
	
		uint32 bonus = conf.extraChance;
		if (bonus > 100)
			bonus = 100;
	
		return (uint8)bonus;
	}

	// --------------------------
	// Forward deklarace menu / akcí
	// --------------------------
	static void ShowMissionsRoot(Player* player, Creature* creature);
	
	// podmenu
	static void ShowNormalMenu(Player* player, Creature* creature, ExpGuildRow const& info);
	static void ShowHeroicMenu(Player* player, Creature* creature, ExpGuildRow const& info);
	static void ShowRaid10Menu(Player* player, Creature* creature, ExpGuildRow const& info);
	static void ShowRaid25Menu(Player* player, Creature* creature, ExpGuildRow const& info);
	
	// confirm + start mise
	static void ShowConfirmMission(Player* player, Creature* creature, ExpGuildRow const& info, uint32 missionIndex);
	static void StartMission(Player* player, Creature* creature, ExpGuildRow const& info, uint32 missionIndex);
	
	// --- forward deklarace transakčních helperů pro loot ---
	static void AddLootForGuildTx(WorldDatabaseTransaction& tx, uint32 guildId, uint32 itemId, uint32 count);
	static void AddLootListTx(WorldDatabaseTransaction& tx, uint32 guildId, std::string const& csv, uint32 amount);
	
	static void ShowLootBankRoot(Player* player, Creature* creature, ExpGuildRow const& info, uint32 page = 0);

	static void ShowLootBankChooseMember(
		Player* player,
		Creature* creature,
		ExpGuildRow const& info,
		uint32 lootIndex,
		uint32 page = 0
	);

    // --------------------------
    // LOOT BANK: helpery pro menu / mailing
    // --------------------------
    struct LootRow
    {
        uint64 id;
        uint32 guildId;
        uint32 itemId;
        uint32 amount;
    };

    // načtení loot banky pro guildu
    static std::vector<LootRow> LoadGuildLoot(uint32 guildId)
    {
        std::vector<LootRow> out;
        if (QueryResult r = WorldDatabase.Query(
                "SELECT id, guildId, itemId, amount "
                "FROM customs.gv_expedition_loot "
                "WHERE guildId={} ORDER BY itemId ASC",
                guildId))
        {
            do
            {
                Field* f = r->Fetch();
                LootRow row;
                row.id      = f[0].Get<uint64>();
                row.guildId = f[1].Get<uint32>();
                row.itemId  = f[2].Get<uint32>();
                row.amount  = f[3].Get<uint32>();
                out.push_back(row);
            }
            while (r->NextRow());
        }
        return out;
    }

    // zmenšení / smazání položky lootu po úspěšném odeslání
    static void DecrementLootEntry(uint64 lootEntryId)
    {
        // nejdřív snížit amount -1, a pak smazat pokud padne na 0
        WorldDatabase.DirectExecute(
            "UPDATE customs.gv_expedition_loot "
            "SET amount = amount - 1 "
            "WHERE id={} AND amount > 0",
            lootEntryId);

        WorldDatabase.DirectExecute(
            "DELETE FROM customs.gv_expedition_loot "
            "WHERE id={} AND amount <= 0",
            lootEntryId);
    }

    // ODESLÁNÍ MAILU HRÁČI
	static bool SendLootItemByMail(uint32 /*guildId*/, ObjectGuid receiverGuid, uint32 itemId)
	{
		// validace šablony
		ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
		if (!proto)
			return false;
	
		// low GUID cílového hráče (characters.guid)
		ObjectGuid::LowType receiverLow = receiverGuid.GetCounter();
	
		// jestli je online
		Player* receiver = ObjectAccessor::FindPlayer(receiverGuid);
	
		// vytvoř jeden kus předmětu
		Item* it = Item::CreateItem(itemId, 1, receiver);
		if (!it)
			return false;
	
		// nastavit vlastníka (důležité pro korektní uložení do DB)
		it->SetOwnerGUID(receiverGuid);
	
		// připravit mail
		static constexpr uint32 EXPEDITION_NPC_ENTRY = 987446;
		MailSender sender(MAIL_CREATURE, EXPEDITION_NPC_ENTRY, MAIL_STATIONERY_DEFAULT);
	
		std::string subject = (LangOpt() == Lang::EN)
			? "Guild Expedition Reward"
			: "Loot z cechovní expedice";
	
		std::string body = (LangOpt() == Lang::EN)
			? "Your guild expedition has returned and delivered loot."
			: "Byl jsi vybrán k obdržení tohoto lootu z expedice. Gratuluji.";
	
		MailDraft draft(subject, body);
	
		// spustit transakci
		CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
	
		// ULOŽIT ITEM DO DB JEŠTĚ PŘED ODESLÁNÍM MAILU
		it->SaveToDB(trans);
	
		draft.AddItem(it);
	
		// poslat mail s delay a expirací (60s doručení, 30 dní expirace)
		draft.SendMailTo(
			trans,
			MailReceiver(receiver, receiverLow),
			sender,
			MAIL_CHECK_MASK_NONE,
			60,        // deliver_delay (sekundy)
			30,        // expiration (dny)
			false,
			true
		);
	
		CharacterDatabase.CommitTransaction(trans);
	
		return true;
	}

    // ========================================================================
    //  GUILD MEMBER WATCH  (anti abuse pro rozdělování lootu)
    //
    //  Tabulka: customs.gv_expedition_member_watch
    //    guildId    INT UNSIGNED
    //    playerGuid INT UNSIGNED
    //    join_time  INT UNSIGNED (unix timestamp)
    //
    //  Chování:
    //   - SyncGuildMemberWatch(guildId) se volá při otevření seznamu členů a každých 5 minut.
    //     -> přidá chybějící členy s join_time=now
    //     -> odebere ty, co už v guildě nejsou (pro tu danou guildu)
    //
    //   - CheckLootEligibility(...) se volá těsně před rozesláním lootu.
    //     -> vrací false a důvod (datum, od kdy má nárok), pokud není členem dostatečně dlouho
    //
    //  Respektuje config:
    //    GuildVillage.Expeditions.LootMinMemberAgeDays
    //    0 = vypnuto (žádný zápis do tabulky, žádná kontrola)
    // ========================================================================

    // helper: aktuální čas v sekundách (unix)
    static inline uint32 NowSec()
    {
        return uint32(GameTime::GetGameTime().count());
    }

    // helper: načtení hodnoty z configu
    static inline uint32 GetLootMinAgeDays()
    {
        return sConfigMgr->GetOption<uint32>("GuildVillage.Expeditions.LootMinMemberAgeDays", 0);
    }

    // helper: format "YYYY-MM-DD" z unix timestampu
    static std::string DateYMD(uint32 unixTs)
    {
        time_t tt = (time_t)unixTs;
        tm tml{};

        localtime_r(&tt, &tml);

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
            tml.tm_year + 1900,
            tml.tm_mon + 1,
            tml.tm_mday
        );
        return std::string(buf);
    }

    // CharacterDatabase vytáhne aktuální roster guidů pro danou guildu
    static std::unordered_set<uint32> LoadRosterGUIDs(uint32 guildId)
    {
        std::unordered_set<uint32> roster;
        if (QueryResult r = CharacterDatabase.Query(
            "SELECT guid FROM guild_member WHERE guildid={}", guildId))
        {
            do
            {
                Field* f = r->Fetch();
                uint32 lowGuid = f[0].Get<uint32>();
                roster.insert(lowGuid);
            }
            while (r->NextRow());
        }
        return roster;
    }

    // WorldDatabase vytáhne aktuální watch záznamy pro danou guildu
    static std::unordered_set<uint32> LoadWatchGUIDs(uint32 guildId)
    {
        std::unordered_set<uint32> watch;
        if (QueryResult r = WorldDatabase.Query(
            "SELECT playerGuid FROM customs.gv_expedition_member_watch WHERE guildId={}", guildId))
        {
            do
            {
                Field* f = r->Fetch();
                uint32 lowGuid = f[0].Get<uint32>();
                watch.insert(lowGuid);
            }
            while (r->NextRow());
        }
        return watch;
    }

    // SYNC:
    //  - přidá chybějící členy s join_time=NowSec()
    //  - smaže ty, co už nejsou v rosteru (ale jen pro tuhle guildId)
    //  - pokud je config 0 dní, nedělá nic
    static void SyncGuildMemberWatch(uint32 guildId)
    {
        uint32 minDays = GetLootMinAgeDays();
        if (minDays == 0)
            return;

        // 1) načte seznam aktuálních členů z characters.guild_member
        std::unordered_set<uint32> roster = LoadRosterGUIDs(guildId);

        // 2) načte seznam už zapsaných z customs.gv_expedition_member_watch
        std::unordered_set<uint32> watch  = LoadWatchGUIDs(guildId);

        uint32 nowTs = NowSec();

        // 3) kdo je v rosteru ale není v watch -> beru jako nový join do téhle guildy
		for (uint32 guidLow : roster)
		{
			if (watch.count(guidLow))
			{
				continue;
			}
	
			// hráč není ve watchu pro tuhle guildu:
			// - buď je to úplně nový člen
			// - nebo se do téhle guildy znovu vrátil
			// - nebo sem přišel z jiné guildy
			// aby neměl "starý veterán timestamp" z jiné guildy,
			// vyčistím všechny jeho staré záznamy napříč všemi guildami
			WorldDatabase.DirectExecute(
				"DELETE FROM customs.gv_expedition_member_watch WHERE playerGuid={}",
				guidLow
			);
	
			// zapíšu nový čistý záznam pro aktuální guildId s aktuálním časem
			WorldDatabase.DirectExecute(
				"INSERT INTO customs.gv_expedition_member_watch (guildId, playerGuid, join_time) "
				"VALUES ({}, {}, {})",
				guildId,
				guidLow,
				nowTs
			);
		}

        // 4) kdo je ve watchu ale není v rosteru -> DELETE z téhle guildy
        for (uint32 guidLow : watch)
        {
            if (!roster.count(guidLow))
            {
                WorldDatabase.DirectExecute(
                    "DELETE FROM customs.gv_expedition_member_watch "
                    "WHERE guildId={} AND playerGuid={}",
                    guildId,
                    guidLow
                );
            }
        }
    }

    // KONTROLA:
    //  - vrátí true = hráč může dostat loot
    //  - vrátí false = hráč je moc nový; outDenyDate dostane datum YYYY-MM-DD,
    //    od kdy už bude moct loot dostat
    //
    // Logika:
    //  - pokud config 0 => vždy povolit
    //  - pokud v tabulce není řádek, povolit protože při nastavení na 0 neděláme zápis
    //  - jinak spočítat, jestli (now - join_time) >= minDays
    static bool CheckLootEligibility(uint32 guildId, uint32 playerGuidLow, std::string& outDenyDate)
    {
        uint32 minDays = GetLootMinAgeDays();
        if (minDays == 0)
            return true; // ochrana vypnutá

        if (QueryResult r = WorldDatabase.Query(
            "SELECT join_time FROM customs.gv_expedition_member_watch "
            "WHERE guildId={} AND playerGuid={} LIMIT 1",
            guildId,
            playerGuidLow))
        {
            Field* f = r->Fetch();
            uint32 joinTs = f[0].Get<uint32>();
            uint32 nowTs  = NowSec();

            uint32 needSec = minDays * 86400u;

            if (nowTs >= joinTs + needSec)
                return true;

            uint32 allowedTs = joinTs + needSec;
            outDenyDate = DateYMD(allowedTs);
            return false;
        }

        return true;
    }

    // -------------------------------------------------
    // Menu pro lootbanku (výběr itemu)
    // -------------------------------------------------

    // Seznam členů guildy
    struct GuildMemberInfo
	{
		uint32 lowGuid;
		std::string name;
		uint32 rank;
	};
	
	static std::vector<GuildMemberInfo> LoadGuildMembersSorted(Guild* g)
	{
		std::vector<GuildMemberInfo> out;
		if (!g)
			return out;
	
		uint32 guildId = g->GetId();
	
		if (QueryResult r = CharacterDatabase.Query(
			"SELECT gm.guid, gm.rank, c.name "
			"FROM guild_member gm "
			"JOIN characters c ON c.guid = gm.guid "
			"WHERE gm.guildid = {}",
			guildId))
		{
			do
			{
				Field* f = r->Fetch();
	
				uint32 lowGuid = f[0].Get<uint32>();
				uint32 rank    = f[1].Get<uint32>();
				std::string name = f[2].Get<std::string>();
	
				GuildMemberInfo inf;
				inf.lowGuid = lowGuid;
				inf.rank    = rank;
				inf.name    = name;
				out.push_back(inf);
			}
			while (r->NextRow());
		}
	
		std::sort(out.begin(), out.end(),
			[](GuildMemberInfo const& a, GuildMemberInfo const& b)
			{
				return a.name < b.name;
			});
	
		return out;
	}

    // Zobrazení root loot bank menu (list všech itemů co guilda má)
	static void ShowLootBankRoot(Player* player, Creature* creature, ExpGuildRow const& info, uint32 page /*=0*/)
	{
		ClearGossipMenuFor(player);
		
		SyncGuildMemberWatch(info.guildId);
	
		auto lootList = LoadGuildLoot(info.guildId);
	
		if (lootList.empty())
		{
			AddGossipItemFor(
				player,
				GOSSIP_ICON_CHAT,
				T("Žádný loot k rozdání.", "No rewards to distribute."),
				GOSSIP_SENDER_MAIN,
				ACT_LOOTBANK_ROOT);
		}
		else
		{
			uint32 total = lootList.size();
			uint32 per   = LOOTBANK_PAGE_SIZE;                 // 10
			uint32 pages = (total + per - 1) / per;            // ceil
			if (page >= pages)
				page = (pages > 0 ? pages - 1 : 0);
	
			uint32 start = page * per;
			uint32 end   = std::min(start + per, total);
	
			// ===== HEADER: "Stránka X/Y" (jen pokud je víc než 1 stránka) =====
			if (pages > 1)
			{
				std::string pageLine = Acore::StringFormat(
					T("Stránka {}/{}", "Page {}/{}"),
					(uint32)(page + 1),
					(uint32)pages
				);
	
				AddGossipItemFor(
					player,
					GOSSIP_ICON_CHAT,
					pageLine,
					GOSSIP_SENDER_MAIN,
					ACT_LOOTBANK_ROOT  // inertní
				);
			}
	
			// ===== SEPARATOR pod "Stránka X/Y" =====
			AddGossipItemFor(
				player,
				0,
				SeparatorLine(),
				GOSSIP_SENDER_MAIN,
				ACT_NOOP_LOOT_ROOT // inertní
			);
	
			// ===== SEZNAM ITEMŮ (aktuální stránka) =====
			for (uint32 i = start; i < end; ++i)
			{
				auto const& row = lootList[i];
			
				std::string itemName;
				if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(row.itemId))
					itemName = proto->Name1;
				else
					itemName = Acore::StringFormat("Item {}", row.itemId);
			
				std::string line = Acore::StringFormat(
					"{} x{}",
					itemName,
					row.amount
				);
			
				// lootIndex = pořadí v lootListu (0..N-1), vejde se nám do uint16
				uint32 lootIndex32   = i;
				uint32 memberPage32  = 0; // první stránka hráčů
				
				uint32 encoded = EncodeItemAndPage(lootIndex32, memberPage32);
				
				uint32 actionId = ACT_LOOTBANK_ITEM_BASE + encoded;

				AddGossipItemFor(
					player,
					GOSSIP_ICON_MONEY_BAG,
					line,
					GOSSIP_SENDER_MAIN,
					actionId
				);
			}

			// ===== SEPARATOR pod posledním itemem =====
			AddGossipItemFor(
				player,
				0,
				SeparatorLine(),
				GOSSIP_SENDER_MAIN,
				ACT_NOOP_LOOT_ROOT // inertní
			);
	
			// ===== DALŠÍ STRÁNKA (hned pod tím separator) =====
			if (page + 1 < pages)
			{
				uint32 nextPage = page + 1;
	
				AddGossipItemFor(
					player,
					GOSSIP_ICON_TAXI,
					T("Další stránka >>", "Next page >>"),
					GOSSIP_SENDER_MAIN,
					ACT_LOOTBANK_PAGE_BASE + nextPage
				);
			}
	
			// ===== PŘEDCHOZÍ STRÁNKA (hned nad Zpátky) =====
			if (page > 0)
			{
				uint32 prevPage = page - 1;
	
				AddGossipItemFor(
					player,
					GOSSIP_ICON_TAXI,
					T("<< Predchozí stránka", "<< Previous page"),
					GOSSIP_SENDER_MAIN,
					ACT_LOOTBANK_PAGE_BASE + prevPage
				);
			}
		}
	
		// ===== Zpátky =====
		AddGossipItemFor(
			player,
			GOSSIP_ICON_TAXI,
			T("Zpátky", "Back"),
			GOSSIP_SENDER_MAIN,
			ACT_BACK_TO_ROOT
		);
	
		SendGossipMenuFor(player, 1, creature->GetGUID());
	}

	// submenu: výběr člena guildy pro konkrétní loot item (s pagination)
	static void ShowLootBankChooseMember(
		Player* player,
		Creature* creature,
		ExpGuildRow const& info,
		uint32 lootIndex,
		uint32 page /*=0*/
	)
	{
		ClearGossipMenuFor(player);
		
		SyncGuildMemberWatch(info.guildId);
		
		uint32 lootPage = lootIndex / LOOTBANK_PAGE_SIZE;
	
		Guild* g = player->GetGuild();
		auto members = LoadGuildMembersSorted(g);
	
		if (members.empty())
		{
			AddGossipItemFor(
				player,
				GOSSIP_ICON_CHAT,
				T("Žádní hráči v guildě.", "No players in guild."),
				GOSSIP_SENDER_MAIN,
				ACT_LOOTBANK_ROOT);
	
			// zpět do seznamu itemů (root loot bank)
			AddGossipItemFor(
				player,
				GOSSIP_ICON_TAXI,
				T("Zpátky", "Back"),
				GOSSIP_SENDER_MAIN,
				ACT_LOOTBANK_ROOT);
	
			SendGossipMenuFor(player, 1, creature->GetGUID());
			return;
		}
	
		uint32 total = members.size();
		uint32 per   = LOOT_MEMBER_PAGE_SIZE;
		uint32 pages = (total + per - 1) / per; // ceil
	
		if (page >= pages)
			page = (pages > 0 ? pages - 1 : 0);
	
		uint32 start = page * per;
		uint32 end   = std::min(start + per, total);
	
		// ===== header řádek + separator =====
		{
			uint32 encodedHeader = Pack16_16(uint16(lootIndex & 0xFFFF), uint16(page & 0xFFFF));
	
			// pokud mám víc než 1 stránku, přidat řádek "Stránka X/Y"
			if (pages > 1)
			{
				std::string pageLine = Acore::StringFormat(
					T("Stránka {}/{}", "Page {}/{}"),
					(uint32)(page + 1),
					(uint32)pages
				);
	
				AddGossipItemFor(
					player,
					GOSSIP_ICON_CHAT,
					pageLine,
					GOSSIP_SENDER_MAIN,
					ACT_LOOTBANK_ITEM_BASE + encodedHeader
				);
			}
	
			// separator (inertní)
			AddGossipItemFor(
				player,
				0,
				SeparatorLine(),
				GOSSIP_SENDER_MAIN,
				ACT_NOOP_LOOT_MEMBER_BASE + EncodeItemAndPage(lootIndex, page)
			);
		}
	
		// ===== seznam hráčů =====
		for (uint32 i = start; i < end; ++i)
		{
			auto const& m = members[i];
		
			std::string line = m.name;
		
			uint32 encodedGive = EncodeGive(lootIndex, i);
		
			uint32 actionId = ACT_LOOTBANK_GIVE_BASE + encodedGive;
		
			AddGossipItemFor(
				player,
				GOSSIP_ICON_MONEY_BAG,
				line,
				GOSSIP_SENDER_MAIN,
				actionId
			);
		}

		// ===== druhý separator (inertní) =====
		{
			AddGossipItemFor(
				player,
				0,
				SeparatorLine(),
				GOSSIP_SENDER_MAIN,
				ACT_NOOP_LOOT_MEMBER_BASE + EncodeItemAndPage(lootIndex, page)
			);
		}
	
		// ===== navigace stránkování a zpátky =====
		{
			bool hasPrev = (page > 0);
			bool hasNext = (page + 1 < pages);
	
			// 1) "Další stránka >>" (pokud existuje)
			if (hasNext)
			{
				uint32 nextPage = page + 1;
				uint32 encodedNext = EncodeItemAndPage(lootIndex, nextPage);
				AddGossipItemFor(
					player,
					GOSSIP_ICON_TAXI,
					T("Další stránka >>", "Next page >>"),
					GOSSIP_SENDER_MAIN,
					ACT_LOOTBANK_ITEM_BASE + encodedNext
				);
			}
	
			// 2) "<< Předchozí stránka" (pokud existuje)
			if (hasPrev)
			{
				uint32 prevPage = page - 1;
				uint32 encodedPrev = EncodeItemAndPage(lootIndex, prevPage);
			
				AddGossipItemFor(
					player,
					GOSSIP_ICON_TAXI,
					T("<< Predchozí stránka", "<< Previous page"),
					GOSSIP_SENDER_MAIN,
					ACT_LOOTBANK_ITEM_BASE + encodedPrev
				);
			}
	
			// 3) "Zpátky" zobrazovat jen na první stránce (page == 0)
			//    a vrátit se na tu lootbank stránku, kde jsem klikl na item
			if (page == 0)
			{
				AddGossipItemFor(
					player,
					GOSSIP_ICON_TAXI,
					T("Zpátky", "Back"),
					GOSSIP_SENDER_MAIN,
					ACT_LOOTBANK_PAGE_BASE + lootPage
				);
			}
		}
	
		SendGossipMenuFor(player, 1, creature->GetGUID());
	}

    // Když officer klikne na konkrétního hráče -> poslat mail a odečíst 1 kus
    // encoded data rozbalit zpět na (lootRowId32, guildMemberIndex)
	static void DoDistributeLoot(
		Player* player,
		Creature* creature,
		ExpGuildRow const& info,
		uint32 encoded)
	{
		Guild* g = player->GetGuild();
		if (!g)
			return;
	
		// 1) rozbalit z action ID
		uint32 lootIndex    = 0;
		uint32 memberIndex  = 0;
		DecodeGive(encoded, lootIndex, memberIndex);
	
		// 2) načíst aktuální loot list
		auto lootList = LoadGuildLoot(info.guildId);
	
		if (lootIndex >= lootList.size())
		{
			ChatHandler(player->GetSession()).SendSysMessage(
				T("Tento loot už není k dispozici.", "This loot is no longer available."));
			ShowLootBankRoot(player, creature, info, 0);
			return;
		}
	
		LootRow const& chosen = lootList[lootIndex];
	
		// 3) načíst aktuální členy gildy, stejné řazení jako v ShowLootBankChooseMember
		auto members = LoadGuildMembersSorted(g);
	
		if (memberIndex >= members.size())
		{
			ChatHandler(player->GetSession()).SendSysMessage(
				T("Tento hráč už není v guildě.", "This player is no longer in the guild."));
			ShowLootBankRoot(player, creature, info, 0);
			return;
		}
	
		GuildMemberInfo const& targetMember = members[memberIndex];
	
		uint32 fullLowGuid = targetMember.lowGuid;
		ObjectGuid targetGuid = ObjectGuid(HighGuid::Player, fullLowGuid);
	
		// === zkontrolovat, jestli má právo na to dostat loot podle stáří v guildě ===
		{
			std::string denyDate;
			if (!CheckLootEligibility(info.guildId, fullLowGuid, denyDate))
			{
				// hráč je moc nový v guildě -> bloknout rozdělení
				std::string msg;
	
				if (LangOpt() == Lang::EN)
				{
					msg = Acore::StringFormat(
						"|cffff0000This player hasn't been in the guild long enough. "
						"They are eligible for loot after {}.|r",
						denyDate
					);
				}
				else
				{
					msg = Acore::StringFormat(
						"|cffff0000Tento hráč je v guildě kratší dobu, než je povolený limit. "
						"Nemá nárok na loot do {}.|r",
						denyDate
					);
				}
	
				ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
	
				// vrátit do root loot banky (stejně jako při jiných chybách)
				ShowLootBankRoot(player, creature, info, 0);
				return;
			}
		}
	
		// 4) poslat mail
		if (!SendLootItemByMail(info.guildId, targetGuid, chosen.itemId))
		{
			ChatHandler(player->GetSession()).SendSysMessage(
				T("Nepodařilo se odeslat mail.", "Failed to send mail."));
			ShowLootBankRoot(player, creature, info, 0);
			return;
		}
	
		// 5) odečíst 1 kus z banky
		DecrementLootEntry(chosen.id);
	
		// 6) složit link na item pro zprávu
		std::string itemLink;
		if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(chosen.itemId))
		{
			uint32 rgba = ItemQualityColors[proto->Quality];
			uint8 r = (rgba >> 16) & 0xFF;
			uint8 g = (rgba >> 8)  & 0xFF;
			uint8 b = (rgba)       & 0xFF;

			itemLink = Acore::StringFormat(
				"|cff{:02X}{:02X}{:02X}|Hitem:{}:0:0:0:0:0:0:0|h[{}]|h|r",
				(uint32)r, (uint32)g, (uint32)b,
				(uint32)chosen.itemId,
				proto->Name1
			);
		}
		else
		{
			itemLink = Acore::StringFormat("[Item {}]", chosen.itemId);
		}
		
		// nový link pro jméno příjemce ve formátu [Pepa] s jemnou barvou
		std::string targetNameLink = Acore::StringFormat(
			"|cff00ffff[{}]|r",
			targetMember.name
		);
		
		// nová hláška s dvěma placeholdery
		std::string msg = Acore::StringFormat(
			T("Předmět {} byl odeslán hráči {}, doručení za 1 minutu.",
			"Item {} has been sent to {}, it will arrive in 1 minute."),
			itemLink,
			targetNameLink
		);
		
		// poslání hlášky důstojníkovi, co to dal
		ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
	
		// zpět do lootbank root (první stránka)
		ShowLootBankRoot(player, creature, info, 0);
		return;
	}

    // --------------------------
    // CONFIRM okno před spuštěním expedice
    // --------------------------
    static void ShowConfirmMission(Player* player, Creature* creature, ExpGuildRow const& info, uint32 missionIndex)
    {
        ClearGossipMenuFor(player);

        auto miOpt = MissionFromIndex(missionIndex);
		if (!miOpt)
		{
			ChatHandler(player->GetSession()).SendSysMessage(
				T("Tato mise není k dispozici.", "This mission is not available."));
			ShowMissionsRoot(player, creature);
			return;
		}

        MissionInfo mi = *miOpt;

        uint8 available = GetAvailableHeroes(info);
		uint8 toSend    = CalculateHeroesToSend(mi.type, available);
		
		if (toSend == 0)
		{
			ChatHandler(player->GetSession()).SendSysMessage(
				T("Není dostatek dostupných hrdinů pro tuto expedici.",
				"Not enough available heroes for this mission."));
		
			// poslat hráče zpátky do správného podmenu podle typu mise
			switch (mi.type)
			{
				case MissionType::NORMAL_5:
					ShowNormalMenu(player, creature, info);
					break;
				case MissionType::HEROIC_5:
					ShowHeroicMenu(player, creature, info);
					break;
				case MissionType::RAID_10:
					ShowRaid10Menu(player, creature, info);
					break;
				case MissionType::RAID_25:
					ShowRaid25Menu(player, creature, info);
					break;
			}
			return;
		}

        MissionConf conf = LoadMissionConf(mi.name);
        auto costOpt     = LoadMissionCost(mi.name);

        // spočítat success šanci stejně jako při StartMission
        uint8 baseChance  = LoadSuccessChanceForCount(toSend);
        uint8 bonusChance = LoadExtraChanceBonus(mi, info, toSend);

        uint32 finalChance = baseChance + bonusChance;
        if (finalChance > 100)
            finalChance = 100;

        // horní info řádek
        {
            std::string header = Acore::StringFormat(
                T("Expedice: {}",
                  "Expedition: {}"),
                mi.name
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                header,
                GOSSIP_SENDER_MAIN,
                ACT_CONFIRM_MISSION_BASE + missionIndex);
        }

        // čas trvání
        {
            uint32 secs = conf.timeSeconds ? conf.timeSeconds : 3600;
            uint32 mins = secs / 60;
            std::string timeInfo;
            if (mins < 120)
                timeInfo = Acore::StringFormat(
                    T("Délka mise: {} min", "Mission duration: {} min"),
                    (uint32)mins);
            else
            {
                uint32 hrs = mins / 60;
                timeInfo = Acore::StringFormat(
                    T("Délka mise: {} h", "Mission duration: {} h"),
                    (uint32)hrs);
            }

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                timeInfo,
                GOSSIP_SENDER_MAIN,
                ACT_CONFIRM_MISSION_BASE + missionIndex);
        }

        // šance na úspěch
        {
            std::string chanceLine;

            if (bonusChance > 0)
            {
                // ukázat i bonus, ať je jasné odkud se bere vyšší číslo
                chanceLine = Acore::StringFormat(
                    T("Šance na úspech: {}% (+{}%)",
                      "Success chance: {}% (+{}%)"),
                    (uint32)finalChance,
                    (uint32)bonusChance
                );
            }
            else
            {
                chanceLine = Acore::StringFormat(
                    T("Šance na úspech: {}%",
                      "Success chance: {}%"),
                    (uint32)finalChance
                );
            }

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                chanceLine,
                GOSSIP_SENDER_MAIN,
                ACT_CONFIRM_MISSION_BASE + missionIndex);
        }

		// cena mise (s názvy materiálů + správné tvary + zlato)
		if (costOpt)
		{
			MissionCost const& mc = *costOpt;
		
			std::string pretty = CostLine(mc.mat1, mc.mat2, mc.mat3, mc.mat4, mc.gold);
		
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
					ACT_CONFIRM_MISSION_BASE + missionIndex);
			}
		}

        // separator
        AddGossipItemFor(
            player,
            0,
            SeparatorLine(),
            GOSSIP_SENDER_MAIN,
            ACT_NOOP_CONFIRM_BASE + missionIndex);

        // potvrdit start mise
        AddGossipItemFor(
            player,
            GOSSIP_ICON_TRAINER,
            T("Potvrdit zahájení expedice", "Confirm expedition start"),
            GOSSIP_SENDER_MAIN,
            ACT_LAUNCH_MISSION_BASE + missionIndex);

        // zpět - podle typu mise zpět do správného podmenu
        uint32 backAction = ACT_MISSIONS_ROOT;
        switch (mi.type)
        {
            case MissionType::NORMAL_5: backAction = ACT_MENU_NORMAL; break;
            case MissionType::HEROIC_5: backAction = ACT_MENU_HEROIC; break;
            case MissionType::RAID_10:  backAction = ACT_MENU_RAID10; break;
            case MissionType::RAID_25:  backAction = ACT_MENU_RAID25; break;
        }

        AddGossipItemFor(
            player,
            GOSSIP_ICON_TAXI,
            T("Zpátky", "Back"),
            GOSSIP_SENDER_MAIN,
            backAction);

        SendGossipMenuFor(player, 1, creature->GetGUID());
    }

    // --------------------------
    // Start mise
    // --------------------------
	static void StartMission(Player* player, Creature* creature, ExpGuildRow const& info, uint32 missionIndex)
	{
		auto miOpt = MissionFromIndex(missionIndex);
		if (!miOpt)
		{
			ChatHandler(player->GetSession()).SendSysMessage(
				T("Tato mise není k dispozici.", "This mission is not available."));
			ShowMissionsRoot(player, creature);
			return;
		}

        MissionInfo mi = *miOpt;

        Guild* g = player->GetGuild();
        if (!g)
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Nejsi v guildě.", "You are not in a guild."));
            return;
        }

        if (!IsGuildLeaderOrOfficer(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Přístup má jen Guild Master nebo Zástupce.",
                  "Only Guild Master or Officers can manage expeditions."));
            return;
        }

        uint8 available = GetAvailableHeroes(info);
        uint8 toSend = CalculateHeroesToSend(mi.type, available);
		if (toSend == 0)
		{
			ChatHandler(player->GetSession()).SendSysMessage(
				T("Není dostatek dostupných hrdinů pro tuto expedici.",
				"Not enough available heroes for this mission."));
		
			// zpátky do confirm okna té konkrétní mise,
			// aby hráč pořád viděl co vlastně zkoušel spustit
			ShowConfirmMission(player, creature, info, missionIndex);
			return;
		}

        // cena mise
        MissionCost mc{};
        if (auto costOpt = LoadMissionCost(mi.name))
            mc = *costOpt;

        // pokus o odečet měny + goldů
        if (!TryDeductGuildCurrencyAndGold(
                g->GetId(), player,
                mc.mat1, mc.mat2, mc.mat3, mc.mat4,
                mc.gold))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Nedostatek materiálu nebo zlata.",
                  "Not enough materials or gold."));
            ShowConfirmMission(player, creature, info, missionIndex);
            return;
        }

        // spočítat successChance
        uint8 baseChance  = LoadSuccessChanceForCount(toSend);
        uint8 bonusChance = LoadExtraChanceBonus(mi, info, toSend);

        uint32 finalChance = baseChance + bonusChance;
        if (finalChance > 100)
            finalChance = 100;

        // čas konce mise
        MissionConf conf = LoadMissionConf(mi.name);
        uint32 durationSec = conf.timeSeconds ? conf.timeSeconds : 3600;

        uint32 nowSec = uint32(GameTime::GetGameTime().count());
        uint32 endSec = nowSec + durationSec;

        // zapsat běžící expedici
        // customs.gv_expedition_active:
        //   id BIGINT/INT AI
        //   guildId INT
        //   mission_name VARCHAR
        //   heroes_sent TINYINT
        //   end_time DATETIME
        //   success_chance TINYINT
        //   resolved TINYINT DEFAULT 0
        WorldDatabase.DirectExecute(
			"INSERT INTO customs.gv_expedition_active "
			"(guildId, mission_name, heroes_sent, end_time, success_chance, resolved) "
			"VALUES ({}, '{}', {}, FROM_UNIXTIME({}), {}, 0)",
			g->GetId(),
			mi.name,
			(uint32)toSend,
			(uint32)endSec,
			(uint32)finalChance
		);

        // zvětšit heroes_on_mission
        WorldDatabase.DirectExecute(
            "UPDATE customs.gv_expedition_guild "
            "SET heroes_on_mission = heroes_on_mission + {}, last_update = NOW() "
            "WHERE guildId={}",
            (uint32)toSend,
            g->GetId()
        );

        ChatHandler(player->GetSession()).SendSysMessage(
            T("Expedice zahájena!",
              "Expedition started!"));

        // zpět do root menu
        auto refreshed = LoadGuildExpData(g->GetId());
        if (refreshed)
            ShowMissionsRoot(player, creature);
        else
            ;
    }

    // --------------------------
    // Vyhodnocení expedic po doběhu
    // (WorldScript tick každých ~5s)
    // --------------------------

    static uint32 PickRandomItemIdFromList(std::string const& csv)
    {
        std::vector<uint32> ids;
        {
            std::stringstream ss(csv);
            std::string token;
            while (std::getline(ss, token, ','))
            {
                if (token.empty())
                    continue;
                ids.push_back(uint32(std::stoul(token)));
            }
        }

        if (ids.empty())
            return 0;

        uint32 idx = urand(0, ids.size() - 1);
        return ids[idx];
    }

	// Vytvoří náhodný loot z missionConf.itemids
	// amount = kolik kusů celkem
	static void AddLootListTx(WorldDatabaseTransaction& tx, uint32 guildId, std::string const& csv, uint32 amount)
	{
		if (!guildId || !amount || csv.empty())
			return;
	
		std::vector<uint32> ids;
		{
			std::stringstream ss(csv);
			std::string token;
			while (std::getline(ss, token, ','))
			{
				if (token.empty())
					continue;
				ids.push_back(uint32(std::stoul(token)));
			}
		}
	
		if (ids.empty())
			return;
	
		for (uint32 i = 0; i < amount; ++i)
		{
			uint32 idx    = urand(0, ids.size() - 1);
			uint32 itemId = ids[idx];
			AddLootForGuildTx(tx, guildId, itemId, 1);
		}
	}

    // Uložit loot do guild banky (gv_expedition_loot)
	// Vyžaduje UNIQUE KEY (guildId, itemId)
	static void AddLootForGuildTx(WorldDatabaseTransaction& tx, uint32 guildId, uint32 itemId, uint32 count)
	{
		if (!guildId || !itemId || !count)
			return;
	
		tx->Append(
			"INSERT INTO customs.gv_expedition_loot (guildId, itemId, amount) "
			"VALUES ({}, {}, {}) "
			"ON DUPLICATE KEY UPDATE amount = amount + VALUES(amount)",
			guildId, itemId, count
		);
	}

    // Vyhodnotit jednu expedici která doběhla
	static void ResolveSingleExpedition(
		uint64 activeId,
		uint32 guildId,
		std::string const& missionName,
		uint8 heroesSent,
		uint8 successChance)
	{
		MissionConf conf = LoadMissionConf(missionName);
	
		bool success = (urand(1, 100) <= successChance);
	
		// spustit transakci
		WorldDatabaseTransaction tx = WorldDatabase.BeginTransaction();
	
		// 1) Loot pro guildu (jen pokud mise uspěla)
		if (success && conf.amount > 0 && !conf.itemids.empty())
		{
			AddLootListTx(tx, guildId, conf.itemids, conf.amount);
		}
	
		// 2) Vrátit hrdiny domů
		tx->Append(
			"UPDATE customs.gv_expedition_guild "
			"SET heroes_on_mission = GREATEST(heroes_on_mission - {}, 0), "
			"    last_update = NOW() "
			"WHERE guildId={}",
			(uint32)heroesSent,
			guildId
		);
	
		// 3) Označit expedici jako vyřešenou
		tx->Append(
			"UPDATE customs.gv_expedition_active "
			"SET resolved=1, resolved_at=NOW() "
			"WHERE id={}",
			(uint32)activeId
		);
	
		// 4) Commit všech změn v jedné transakci
		WorldDatabase.CommitTransaction(tx);
	}

	static void CleanupResolvedExpeditions()
	{
		WorldDatabase.DirectExecute(
			"DELETE FROM customs.gv_expedition_active "
			"WHERE resolved=1 AND ("
			"      (resolved_at IS NOT NULL AND resolved_at <= NOW() - INTERVAL 5 SECOND)"
			"   OR (resolved_at IS NULL AND end_time   <= NOW() - INTERVAL 30 SECOND)"
			")"
		);
	}

    // projít všechny expedice, co doběhly
    static void ProcessFinishedExpeditions()
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT id, guildId, mission_name, heroes_sent, success_chance "
                "FROM customs.gv_expedition_active "
                "WHERE resolved=0 AND end_time <= NOW()"))
        {
            do
            {
                Field* f = r->Fetch();
                uint64 activeId       = f[0].Get<uint64>();
                uint32 guildId        = f[1].Get<uint32>();
                std::string missionNm = f[2].Get<std::string>();
                uint8 heroesSent      = f[3].Get<uint8>();
                uint8 succChance      = f[4].Get<uint8>();

                ResolveSingleExpedition(activeId, guildId, missionNm, heroesSent, succChance);
            }
            while (r->NextRow());
        }
    }

	// aktivní expedice pro guildu
	struct ActiveMissionRow
	{
		std::string missionName;
		uint32      endTime;
	};
	
	// Tohle je řádek pro chat výpis (.village expedition)
    struct ExpeditionLine
    {
        std::string mission;
        std::string remain;
    };

	
	// načte všechny expedice, které ještě běží (resolved=0 a end_time > NOW())
	static std::vector<ActiveMissionRow> LoadActiveMissions(uint32 guildId)
	{
		std::vector<ActiveMissionRow> out;
	
		if (QueryResult r = WorldDatabase.Query(
			"SELECT mission_name, UNIX_TIMESTAMP(end_time) "
			"FROM customs.gv_expedition_active "
			"WHERE guildId={} AND resolved=0 AND end_time > NOW() "
			"ORDER BY end_time ASC",
			guildId))
		{
			do
			{
				Field* f = r->Fetch();
	
				ActiveMissionRow row;
				row.missionName = f[0].Get<std::string>();
				row.endTime     = f[1].Get<uint32>();
				out.push_back(row);
			}
			while (r->NextRow());
		}
	
		return out;
	}
	
	static std::string FormatRemaining(uint32 nowSec, uint32 endSec)
	{
		if (endSec <= nowSec)
			return "0h 00m 00s";
	
		uint32 diff = endSec - nowSec;
	
		uint32 hours   = diff / 3600;
		uint32 remain  = diff % 3600;
		uint32 minutes = remain / 60;
		uint32 seconds = remain % 60;
	
		// např. "1h 23m 15s"
		return Acore::StringFormat(
			"{}h {:02}m {:02}s",
			hours,
			minutes,
			seconds
		);
	}
	
    std::vector<ExpeditionLine> BuildExpeditionLinesForGuild(uint32 guildId)
    {
        std::vector<ExpeditionLine> out;

        std::vector<ActiveMissionRow> activeList = LoadActiveMissions(guildId);
        if (activeList.empty())
            return out;

        uint32 nowSec = uint32(GameTime::GetGameTime().count());

        for (auto const& act : activeList)
        {
            ExpeditionLine line;
            line.mission = act.missionName;
            line.remain  = FormatRemaining(nowSec, act.endTime);
            out.push_back(line);
        }

        return out;
    }

    // --------------------------
    // Root menu (výběr kategorie + Loot bank)
    // --------------------------
    static void ShowMissionsRoot(Player* player, Creature* creature)
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
		
		SyncGuildMemberWatch(g->GetId());

        if (!IsGuildLeaderOrOfficer(player))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Přístup má jen Guild Master nebo Zástupce.",
                  "Only Guild Master or Officers can manage expeditions."));
            SendGossipMenuFor(player, 1, creature->GetGUID());
            return;
        }

        auto infoOpt = LoadGuildExpData(g->GetId());
        if (!infoOpt.has_value())
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                T("Chybí data expedic pro tvoji guildu.",
                  "Your guild has no expedition data initialized."));
            SendGossipMenuFor(player, 1, creature->GetGUID());
            return;
        }

        ExpGuildRow info = *infoOpt;

        uint16 shownIlvl = DisplayIlvl(info.gearLevel);
        uint8  available = GetAvailableHeroes(info);

        // 1) Hrdinové: X/Y
        {
            std::string lineHeroesTotal = Acore::StringFormat(
                T("Hrdinové: {}/{}",
                  "Heroes: {}/{}"),
                (uint32)info.owned,
                (uint32)info.maxHeroes
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                lineHeroesTotal,
                GOSSIP_SENDER_MAIN,
                ACT_MISSIONS_ROOT);
        }

        // 2) Výbava iLvl
        if (info.owned > 0)
        {
            std::string lineGear = Acore::StringFormat(
                T("Výbava iLvl {}",
                  "Gear iLvl {}"),
                (uint32)shownIlvl
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                lineGear,
                GOSSIP_SENDER_MAIN,
                ACT_MISSIONS_ROOT);
        }
		
		// 3) Dostupní: 
		if (info.owned > 0)
        {
            std::string lineAvail = Acore::StringFormat(
                T("Dostupní: {}",
                  "Available: {}"),
                (uint32)available
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                lineAvail,
                GOSSIP_SENDER_MAIN,
                ACT_MISSIONS_ROOT);
        }
		
		// 4) Na expedici: 
		if (info.onMission > 0)
        {
            std::string lineOnMission = Acore::StringFormat(
                T("Na expedici: {}",
                  "On mission: {}"),
                (uint32)info.onMission
            );

            AddGossipItemFor(
                player,
                GOSSIP_ICON_CHAT,
                lineOnMission,
                GOSSIP_SENDER_MAIN,
                ACT_MISSIONS_ROOT);
        }
		
		// Probíhající expedice (pokud nějaké jsou)
		{
			std::vector<ActiveMissionRow> activeList = LoadActiveMissions(info.guildId);
		
			if (!activeList.empty())
			{
				uint32 nowSec = uint32(GameTime::GetGameTime().count());
		
				// nadpis sekce
				AddGossipItemFor(
					player,
					GOSSIP_ICON_CHAT,
					T("Probíhající expedice:", "Active expeditions:"),
					GOSSIP_SENDER_MAIN,
					ACT_MISSIONS_ROOT);
		
				// každou expedici jako samostatný řádek:
				for (auto const& act : activeList)
				{
					std::string remainStr = FormatRemaining(nowSec, act.endTime);
		
					std::string line = Acore::StringFormat(
						"{} - {}",
						act.missionName,
						remainStr
					);
		
					AddGossipItemFor(
						player,
						GOSSIP_ICON_CHAT,
						line,
						GOSSIP_SENDER_MAIN,
						ACT_MISSIONS_ROOT);
				}
			}
		}

        // Separator
        AddGossipItemFor(
            player,
            0,
            SeparatorLine(),
            GOSSIP_SENDER_MAIN,
            ACT_NOOP_ROOT);

        // Kategorie: 5M Normal
        if (MeetsReq(info.owned, info.gearLevel,
                     Req_Normal_Heroes(), Req_Normal_Gear()))
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_BATTLE,
                T("5M - Dungeon (Normal)", "5M - Dungeon (Normal)"),
                GOSSIP_SENDER_MAIN,
                ACT_SELECT_NORMAL);
        }

        // Kategorie: 5M Heroic
        if (MeetsReq(info.owned, info.gearLevel,
                     Req_Heroic_Heroes(), Req_Heroic_Gear()))
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_BATTLE,
                T("5M - Dungeon (Heroic)", "5M - Dungeon (Heroic)"),
                GOSSIP_SENDER_MAIN,
                ACT_SELECT_HEROIC);
        }

        // Kategorie: 10M Raid
        if (MeetsReq(info.owned, info.gearLevel,
                     Req_Raid10_Heroes(), Req_Raid10_Gear()))
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_BATTLE,
                "10M - Raid",
                GOSSIP_SENDER_MAIN,
                ACT_SELECT_RAID10);
        }

        // Kategorie: 25M Raid
        if (MeetsReq(info.owned, info.gearLevel,
                     Req_Raid25_Heroes(), Req_Raid25_Gear()))
        {
            AddGossipItemFor(
                player,
                GOSSIP_ICON_BATTLE,
                "25M - Raid",
                GOSSIP_SENDER_MAIN,
                ACT_SELECT_RAID25);
        }

        // Separator
        AddGossipItemFor(
            player,
            0,
            SeparatorLine(),
            GOSSIP_SENDER_MAIN,
            ACT_NOOP_ROOT);

        // Guild Loot Bank
        AddGossipItemFor(
            player,
            GOSSIP_ICON_MONEY_BAG,
            T("Guild Loot Bank / Rozdat loot", "Guild Loot Bank / Distribute loot"),
            GOSSIP_SENDER_MAIN,
            ACT_LOOTBANK_ROOT);

        SendGossipMenuFor(player, 1, creature->GetGUID());
    }

    // --------------------------
    // Submenu: 5M Normal Dungeon
    // --------------------------
	static void ShowNormalMenu(Player* player, Creature* creature, ExpGuildRow const& info)
	{
		ClearGossipMenuFor(player);
	
		// 182 pool
		const char* dungs182[] = {
			"Utgarde Keep", "Utgarde Pinnacle", "The Nexus", "The Oculus",
			"Culling of Stratholme", "DrakTharon Keep", "Halls of Stone",
			"Azjol Nerub", "Halls of Lightning", "Gundrak",
			"Violet Hold", "AhnKahet"
		};
		for (uint32 i = 0; i < 12; ++i)
			AddMissionIfAllowed(player, creature, info, dungs182[i], ACT_CONFIRM_MISSION_BASE + i); // 0..11
	
		// ToC normal
		AddMissionIfAllowed(player, creature, info, "Trial of the Champion", ACT_CONFIRM_MISSION_BASE + 100);
	
		// ICC5 normal
		AddMissionIfAllowed(player, creature, info, "Forge of Souls",        ACT_CONFIRM_MISSION_BASE + 200);
		AddMissionIfAllowed(player, creature, info, "Halls of Reflection",   ACT_CONFIRM_MISSION_BASE + 201);
		AddMissionIfAllowed(player, creature, info, "Pit of Saron",          ACT_CONFIRM_MISSION_BASE + 202);
	
		AddGossipItemFor(player, 0, SeparatorLine(), GOSSIP_SENDER_MAIN, ACT_NOOP_NORMAL);
		AddGossipItemFor(player, GOSSIP_ICON_TAXI, T("Zpátky", "Back"), GOSSIP_SENDER_MAIN, ACT_BACK_TO_ROOT);
		SendGossipMenuFor(player, 1, creature->GetGUID());
	}

    // --------------------------
    // Submenu: 5M Heroic Dungeon
    // --------------------------
	static void ShowHeroicMenu(Player* player, Creature* creature, ExpGuildRow const& info)
	{
		ClearGossipMenuFor(player);
	
		const char* dungs200[] = {
			"Utgarde Keep (HC)", "Utgarde Pinnacle (HC)", "The Nexus (HC)", "The Oculus (HC)",
			"Culling of Stratholme (HC)", "DrakTharon Keep (HC)", "Halls of Stone (HC)",
			"Azjol Nerub (HC)", "Halls of Lightning (HC)", "Gundrak (HC)",
			"Violet Hold (HC)", "AhnKahet (HC)"
		};
		for (uint32 i = 0; i < 12; ++i)
			AddMissionIfAllowed(player, creature, info, dungs200[i], ACT_CONFIRM_MISSION_BASE + 1000 + i); // 1000..1011
	
		AddMissionIfAllowed(player, creature, info, "Trial of the Champion (HC)", ACT_CONFIRM_MISSION_BASE + 1100);
	
		AddMissionIfAllowed(player, creature, info, "Halls of Reflection (HC)",   ACT_CONFIRM_MISSION_BASE + 1200);
		AddMissionIfAllowed(player, creature, info, "Forge of Souls (HC)",        ACT_CONFIRM_MISSION_BASE + 1201);
		AddMissionIfAllowed(player, creature, info, "Pit of Saron (HC)",          ACT_CONFIRM_MISSION_BASE + 1202);
	
		AddGossipItemFor(player, 0, SeparatorLine(), GOSSIP_SENDER_MAIN, ACT_NOOP_HEROIC);
		AddGossipItemFor(player, GOSSIP_ICON_TAXI, T("Zpátky", "Back"), GOSSIP_SENDER_MAIN, ACT_BACK_TO_ROOT);
		SendGossipMenuFor(player, 1, creature->GetGUID());
	}

    // --------------------------
    // Submenu: 10M Raid
    // --------------------------
	static void ShowRaid10Menu(Player* player, Creature* creature, ExpGuildRow const& info)
	{
		ClearGossipMenuFor(player);
	
		uint8 available = GetAvailableHeroes(info);
		if (available < 6)
		{
			AddGossipItemFor(
				player, GOSSIP_ICON_CHAT,
				T("Nedostatek dostupných hrdinů (minimálně 6 potřeba).",
				"Not enough available heroes (need at least 6)."),
				GOSSIP_SENDER_MAIN, ACT_MENU_RAID10);
		}
	
		AddMissionIfAllowed(player, creature, info, "The Eye of Eternity (10)",   ACT_CONFIRM_MISSION_BASE + 2000);
		AddMissionIfAllowed(player, creature, info, "The Obsidian Sanctum (10)",  ACT_CONFIRM_MISSION_BASE + 2001);
		AddMissionIfAllowed(player, creature, info, "Naxxramas (10)",             ACT_CONFIRM_MISSION_BASE + 2002);
		AddMissionIfAllowed(player, creature, info, "Ulduar (10)",                ACT_CONFIRM_MISSION_BASE + 2100);
		AddMissionIfAllowed(player, creature, info, "Trial of the Crusader (10)", ACT_CONFIRM_MISSION_BASE + 2200);
		AddMissionIfAllowed(player, creature, info, "IceCrown Citadel (10)",      ACT_CONFIRM_MISSION_BASE + 2300);
		AddMissionIfAllowed(player, creature, info, "The Ruby Sanctum (10)",      ACT_CONFIRM_MISSION_BASE + 2400);
	
		AddGossipItemFor(player, 0, SeparatorLine(), GOSSIP_SENDER_MAIN, ACT_NOOP_RAID10);
		AddGossipItemFor(player, GOSSIP_ICON_TAXI, T("Zpátky", "Back"), GOSSIP_SENDER_MAIN, ACT_BACK_TO_ROOT);
		SendGossipMenuFor(player, 1, creature->GetGUID());
	}

    // --------------------------
    // Submenu: 25M Raid
    // --------------------------
	static void ShowRaid25Menu(Player* player, Creature* creature, ExpGuildRow const& info)
	{
		ClearGossipMenuFor(player);
	
		uint8 available = GetAvailableHeroes(info);
		if (available < 11)
		{
			AddGossipItemFor(
				player, GOSSIP_ICON_CHAT,
				T("Nedostatek dostupných hrdinů (minimálně 11 potřeba).",
				"Not enough available heroes (need at least 11)."),
				GOSSIP_SENDER_MAIN, ACT_MENU_RAID25);
		}
	
		AddMissionIfAllowed(player, creature, info, "The Eye of Eternity (25)",   ACT_CONFIRM_MISSION_BASE + 3000);
		AddMissionIfAllowed(player, creature, info, "The Obsidian Sanctum (25)",  ACT_CONFIRM_MISSION_BASE + 3001);
		AddMissionIfAllowed(player, creature, info, "Naxxramas (25)",             ACT_CONFIRM_MISSION_BASE + 3002);
		AddMissionIfAllowed(player, creature, info, "Ulduar (25)",                ACT_CONFIRM_MISSION_BASE + 3100);
		AddMissionIfAllowed(player, creature, info, "Trial of the Crusader (25)", ACT_CONFIRM_MISSION_BASE + 3200);
		AddMissionIfAllowed(player, creature, info, "IceCrown Citadel (25)",      ACT_CONFIRM_MISSION_BASE + 3300);
		AddMissionIfAllowed(player, creature, info, "The Ruby Sanctum (25)",      ACT_CONFIRM_MISSION_BASE + 3400);
	
		AddGossipItemFor(player, 0, SeparatorLine(), GOSSIP_SENDER_MAIN, ACT_NOOP_RAID25);
		AddGossipItemFor(player, GOSSIP_ICON_TAXI, T("Zpátky", "Back"), GOSSIP_SENDER_MAIN, ACT_BACK_TO_ROOT);
		SendGossipMenuFor(player, 1, creature->GetGUID());
	}

    // --------------------------
    // Gossip NPC script
    // --------------------------
    class npc_gv_expeditions_missions : public CreatureScript
    {
    public:
        npc_gv_expeditions_missions() : CreatureScript("npc_gv_expeditions_missions") { }

        bool OnGossipHello(Player* player, Creature* creature) override
        {
            if (!sConfigMgr->GetOption<bool>("GuildVillage.Expeditions.Enable", true))
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    T("Expedice jsou dočasně vypnuté.",
                      "Expeditions are temporarily disabled."));
                return true;
            }

            ShowMissionsRoot(player, creature);
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
					T("Nejsi v guildě.", "You are not in a guild."));
				return true;
			}
		
			auto infoOpt = LoadGuildExpData(g->GetId());
			if (!infoOpt.has_value())
			{
				ChatHandler(player->GetSession()).SendSysMessage(
					T("Chybí data expedic pro tvoji guildu.",
					"Your guild has no expedition data initialized."));
				return true;
			}
			ExpGuildRow info = *infoOpt;
		
			// 0) nejdřív přímé pevné přepínače přes switch
			switch (action)
			{
				case ACT_NOOP_ROOT:
				{
					ShowMissionsRoot(player, creature);
					return true;
				}
		
				case ACT_NOOP_NORMAL:
				{
					ShowNormalMenu(player, creature, info);
					return true;
				}
		
				case ACT_NOOP_HEROIC:
				{
					ShowHeroicMenu(player, creature, info);
					return true;
				}
		
				case ACT_NOOP_RAID10:
				{
					ShowRaid10Menu(player, creature, info);
					return true;
				}
		
				case ACT_NOOP_RAID25:
				{
					ShowRaid25Menu(player, creature, info);
					return true;
				}
		
				case ACT_NOOP_LOOT_ROOT:
				{
					ShowLootBankRoot(player, creature, info, 0);
					return true;
				}
		
				case ACT_MISSIONS_ROOT:
				case ACT_BACK_TO_ROOT:
				{
					ShowMissionsRoot(player, creature);
					return true;
				}
		
				case ACT_SELECT_NORMAL:
				case ACT_MENU_NORMAL:
				{
					ShowNormalMenu(player, creature, info);
					return true;
				}
		
				case ACT_SELECT_HEROIC:
				case ACT_MENU_HEROIC:
				{
					ShowHeroicMenu(player, creature, info);
					return true;
				}
		
				case ACT_SELECT_RAID10:
				case ACT_MENU_RAID10:
				{
					ShowRaid10Menu(player, creature, info);
					return true;
				}
		
				case ACT_SELECT_RAID25:
				case ACT_MENU_RAID25:
				{
					ShowRaid25Menu(player, creature, info);
					return true;
				}
		
				case ACT_LOOTBANK_ROOT:
				{
					ShowLootBankRoot(player, creature, info, 0);
					return true;
				}
		
				default:
					break;
			}
		
		// --- RANGE HANDLERY POD SWITCHEM ---
		
		// 1) klik na separator nebo infolinku uvnitř confirm okna
		// action = ACT_NOOP_CONFIRM_BASE + missionIndex
		if (action >= ACT_NOOP_CONFIRM_BASE && action < ACT_LOOTBANK_PAGE_BASE)
		{
			uint32 missionIndex = action - ACT_NOOP_CONFIRM_BASE;
			ShowConfirmMission(player, creature, info, missionIndex);
			return true;
		}
		
		// 2) stránkování root loot banky
		// action = ACT_LOOTBANK_PAGE_BASE + page
		if (action >= ACT_LOOTBANK_PAGE_BASE && action < ACT_LOOTBANK_ITEM_BASE)
		{
			uint32 page = action - ACT_LOOTBANK_PAGE_BASE;
			ShowLootBankRoot(player, creature, info, page);
			return true;
		}
		
		// 3) klik na item v loot bance NEBO stránkování seznamu členů
		// action = ACT_LOOTBANK_ITEM_BASE + EncodeItemAndPage(lootIndex, pageMembers)
		if (action >= ACT_LOOTBANK_ITEM_BASE && action < ACT_LOOTBANK_GIVE_BASE)
		{
			uint32 encoded = action - ACT_LOOTBANK_ITEM_BASE;
		
			uint32 lootIndex   = 0;
			uint32 pageMembers = 0;
			DecodeItemAndPage(encoded, lootIndex, pageMembers);
		
			// info do chatu jen u první stránky členů
			if (pageMembers == 0)
			{
				auto lootList = LoadGuildLoot(info.guildId);
		
				if (lootIndex < lootList.size())
				{
					uint32 itemId = lootList[lootIndex].itemId;
		
					if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId))
					{
						uint32 rgba = ItemQualityColors[proto->Quality];
						uint8 r = (rgba >> 16) & 0xFF;
						uint8 g = (rgba >> 8)  & 0xFF;
						uint8 b = (rgba)       & 0xFF;
		
						std::string itemLink = Acore::StringFormat(
							"|cff{:02X}{:02X}{:02X}|Hitem:{}:0:0:0:0:0:0:0|h[{}]|h|r",
							(uint32)r, (uint32)g, (uint32)b,
							(uint32)itemId,
							proto->Name1
						);
		
						std::string msg = Acore::StringFormat(
							T("Vyber komu chceš poslat tento předmět {}",
							"Choose who should receive this item {}"),
							itemLink
						);
		
						ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
					}
				}
			}
		
			ShowLootBankChooseMember(player, creature, info, lootIndex, pageMembers);
			return true;
		}
		
		// 4) klik na konkrétního hráče = rozdat loot
		// action = ACT_LOOTBANK_GIVE_BASE + EncodeGive(lootIndex, targetIndex)
		// DŮLEŽITÉ: horní hranice musí být < ACT_NOOP_LOOT_MEMBER_BASE (5 000 000),
		//           ne < ACT_NOOP_CONFIRM_BASE (4 000 000)
		if (action >= ACT_LOOTBANK_GIVE_BASE && action < ACT_NOOP_LOOT_MEMBER_BASE)
		{
			uint32 encodedGive = action - ACT_LOOTBANK_GIVE_BASE;
			DoDistributeLoot(player, creature, info, encodedGive);
			return true;
		}
		
		// 5) klik na separator/šedou čáru v seznamu členů
		// action = ACT_NOOP_LOOT_MEMBER_BASE + EncodeItemAndPage(lootIndex, pageMembers)
		// horní hranice < ACT_CONFIRM_MISSION_BASE (6 0000), protože další blok je confirm mission
		if (action >= ACT_NOOP_LOOT_MEMBER_BASE && action < ACT_CONFIRM_MISSION_BASE)
		{
			uint32 encoded = action - ACT_NOOP_LOOT_MEMBER_BASE;
		
			uint32 lootIndex   = 0;
			uint32 pageMembers = 0;
			DecodeItemAndPage(encoded, lootIndex, pageMembers);
		
			ShowLootBankChooseMember(player, creature, info, lootIndex, pageMembers);
			return true;
		}
		
		// 6) CONFIRM mission range
		// action = ACT_CONFIRM_MISSION_BASE + missionIndex
		if (action >= ACT_CONFIRM_MISSION_BASE && action < ACT_LAUNCH_MISSION_BASE)
		{
			uint32 missionIndex = action - ACT_CONFIRM_MISSION_BASE;
			ShowConfirmMission(player, creature, info, missionIndex);
			return true;
		}
		
		// 7) LAUNCH mission range
		// action = ACT_LAUNCH_MISSION_BASE + missionIndex
		if (action >= ACT_LAUNCH_MISSION_BASE && action < ACT_NOOP_CONFIRM_BASE)
		{
			uint32 missionIndex = action - ACT_LAUNCH_MISSION_BASE;
			StartMission(player, creature, info, missionIndex);
			return true;
		}
		
		// fallback
		ShowMissionsRoot(player, creature);
		return true;
		}
    };

    // --------------------------
    // WorldScript updater (kontroluje doběhlé expedice)
    // --------------------------
	class gv_expeditions_world_updater : public WorldScript
	{
	public:
		gv_expeditions_world_updater() : WorldScript("gv_expeditions_world_updater") { }
	
		uint32 _accumMsFast = 0;   // ~5s tick pro expedice
		uint32 _accumMsSlow = 0;   // ~300s tick pro sync členů
	
		void OnUpdate(uint32 diff) override
		{
			_accumMsFast += diff;
			_accumMsSlow += diff;
	
			// --- FAST TASK (každých ~5 sekund) ---
			if (_accumMsFast >= 5000)
			{
				_accumMsFast = 0;
	
				if (sConfigMgr->GetOption<bool>("GuildVillage.Expeditions.Enable", true))
				{
					// dokončit doběhlé expedice, rozdat loot do banky, vrátit hrdiny
					ProcessFinishedExpeditions();
	
					// odebrat dokončené expedice
					CleanupResolvedExpeditions();
				}
			}
	
			// --- SLOW TASK (každých ~300 sekund = 5 minut) ---
			if (_accumMsSlow >= 300000)
			{
				_accumMsSlow = 0;
	
				uint32 minDays = GetLootMinAgeDays();
				if (minDays > 0)
				{
					// 1) natáhnout všechny aktuální guildy
					auto guildIds = LoadAllGuildIds();
	
					// 2) pro každou guildu udělat SyncGuildMemberWatch(guildId)
					for (uint32 gid : guildIds)
					{
						SyncGuildMemberWatch(gid);
					}
				}
			}
		}
	};
}

// registrace
void RegisterGuildVillageExpeditionsMissions()
{
    new GuildVillageMissions::npc_gv_expeditions_missions();
    new GuildVillageMissions::gv_expeditions_world_updater();
}
