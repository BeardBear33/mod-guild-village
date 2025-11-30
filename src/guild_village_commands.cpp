// modules/mod-guild-village/src/guild_village_commands.cpp

#include "ScriptMgr.h"
#include "Config.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Player.h"
#include "Guild.h"
#include "DatabaseEnv.h"
#include "DataMap.h"
#include "gv_common.h"
#include "GameTime.h"
#include "gv_names.h"
#include "gv_production.h"
#include "ObjectMgr.h"
#include "ItemTemplate.h"
#include "Map.h"

#include <cstdio>
#include <string>
#include <algorithm>
#include <optional>
#include <vector>
#include <cctype>
#include <ctime>
#include <cmath>

namespace GuildVillageMissions
{
    struct ExpeditionLine
    {
        std::string mission;
        std::string remain;
    };

    std::vector<ExpeditionLine> BuildExpeditionLinesForGuild(uint32 guildId);
}

static inline bool BackEnabled()
{
    return sConfigMgr->GetOption<bool>("GuildVillage.Teleport.Back", true);
}

namespace GuildVillage
{
    void GV_EnsureGuildQuestsAssignedForGuild(uint32 guildId);
}

namespace
{
    // === Locale handling (cs|en) ===
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

    static inline uint32 DefMap()
    {
        return sConfigMgr->GetOption<uint32>("GuildVillage.Default.Map", 37);
    }

    // --- definice bossů (id1 + lokalizované jméno)
    struct BossDef { uint32 id1; char const* name_cs; char const* name_en; };
    static BossDef kBosses[] = {
        { 987400, "Thranok the Unyielding",   "Thranok the Unyielding"   },
        { 987401, "Thalor the Lifebinder",    "Thalor the Lifebinder"    },
        { 987411, "Thalgron the Earthshaker", "Thalgron the Earthshaker" },
        { 987408, "Voltrix the Unbound",      "Voltrix the Unbound"      },
    };

    static inline std::string BossName(BossDef const& b)
    {
        return (LangOpt() == Lang::EN) ? b.name_en : b.name_cs;
    }

    // --- zjištění přesné phase (jedno číslo) pro hráčovu guildu
    static std::optional<uint32> GetGuildPhase(uint32 guildId)
    {
        if (QueryResult r = WorldDatabase.Query(
            "SELECT phase FROM customs.gv_guild WHERE guild={}", guildId))
            return (*r)[0].Get<uint32>();
        return std::nullopt;
    }

    static std::optional<uint32> FindBossGuid(uint32 id1, uint32 phaseMask)
    {
        if (QueryResult r = WorldDatabase.Query(
            "SELECT guid FROM creature WHERE id1={} AND map={} AND phaseMask={} LIMIT 1",
            id1, DefMap(), phaseMask))
            return (*r)[0].Get<uint32>();
        return std::nullopt;
    }

    // --- helper: formátování datumu/času (bez odpočtu)
    static std::string FormatRespawnLine(time_t when)
    {
        if (!when)
            return T("Naživu", "Alive");

        std::tm* tm = std::localtime(&when);
        char buf[64];
        if (tm)
        {
            std::strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", tm); // lokální čas
            return Acore::StringFormat("{} {}", T("Respawn:", "Respawns at:"), buf);
        }
        // fallback
        return Acore::StringFormat("{} {}", T("Respawn (unix):", "Respawns at (unix):"), (long long)when);
    }

    // --- hlavní logika pro řádek stavu bosse (Alive / respawn timestamp)
    static std::string BossStatusLine(uint32 guildId, BossDef const& b)
    {
        // 1) phase dané guildy
        auto phOpt = GetGuildPhase(guildId);
        if (!phOpt)
            return Acore::StringFormat("|cff00ffff{}:|r {}", BossName(b), T("neznámý stav", "unknown status"));

        uint32 phaseMask = *phOpt;

        // 2) GUID spawnu bossa
        auto guidOpt = FindBossGuid(b.id1, phaseMask);
        if (!guidOpt)
            return Acore::StringFormat("|cff00ffff{}:|r {}", BossName(b), T("neinstalováno", "not installed"));

        uint32 guid = *guidOpt;

        // 3) respawnTime z characters.creature_respawn
        time_t respawn = 0;
        if (QueryResult rr = CharacterDatabase.Query(
                "SELECT respawnTime FROM creature_respawn WHERE guid = {} LIMIT 1", guid))
        {
            respawn = (*rr)[0].Get<time_t>();
        }
        else
        {
            // žádný záznam => boss žije
            return Acore::StringFormat("|cff00ffff{}:|r {}", BossName(b), T("Naživu", "Alive"));
        }

        // 4) je už naživu?
        time_t now = (time_t)GameTime::GetGameTime().count();
        if (respawn == 0 || respawn <= now)
            return Acore::StringFormat("|cff00ffff{}:|r {}", BossName(b), T("Naživu", "Alive"));

        // 5) vypsat datum/čas respawnu
        return Acore::StringFormat("|cff00ffff{}:|r {}", BossName(b), FormatRespawnLine(respawn));
    }

    // === Permissions ===
    static inline bool AllowAllMembers()
    {
        return sConfigMgr->GetOption<bool>("GuildVillage.Status.AllowAllMembers", false);
    }

    static bool IsGuildMaster(Player* player)
    {
        if (!player) return false;
        if (Guild* g = player->GetGuild())
            return g->GetLeaderGUID() == player->GetGUID();
        return false;
    }

    static bool CanUseStatus(Player* player)
    {
        if (!player) return false;
        if (!player->GetGuild()) return false;
        if (AllowAllMembers()) return true;
        return IsGuildMaster(player);
    }

    // === Currency caps from config ===
    static inline bool CapsEnabled()
    {
        return sConfigMgr->GetOption<bool>("GuildVillage.CurrencyCap.Enabled", true);
    }
    static inline uint32 CapMaterial1()  { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material1",   1000); }
    static inline uint32 CapMaterial2()  { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material2",    1000); }
    static inline uint32 CapMaterial3()  { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material3",     1000); }
    static inline uint32 CapMaterial4()  { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material4",  1000); }

    // utils
    static inline std::string Trim(std::string s)
    {
        auto ns = [](int ch){ return !std::isspace(ch); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns));
        s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end());
        return s;
    }
    static inline std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
        return s;
    }

    // --- Teleport alias helper ---
    static inline bool IsTeleportArg(std::string sLower)
    {
        return sLower == "teleport" || sLower == "tp";
    }
	
	// --- back alias helper ---
	static inline bool IsBackArg(std::string const& sLower)
	{
		return sLower == "back";
	}
	
	// --- uloží "back" bod před TP do vesnice (pouze mimo vesnici / BG / instanci / combat) ---
	static void SaveBackPointIfEligible(Player* player)
	{
		if (!BackEnabled()) return;
		if (!player) return;
	
		// nesmí být ve vesnici, v BG/areně, v instanci nebo v boji
		if (player->GetMapId() == DefMap()) return;
		if (player->InBattleground()) return;
		if (player->IsInCombat()) return;
		if (Map* m = player->GetMap()) { if (m->Instanceable()) return; }
	
		uint32 pguid = player->GetGUID().GetCounter();
		uint32 map   = player->GetMapId();
		double x     = player->GetPositionX();
		double y     = player->GetPositionY();
		double z     = player->GetPositionZ();
		float  o     = player->GetOrientation();
	
		std::string q =
			"REPLACE INTO customs.gv_teleport_back "
			"(player, map, positionx, positiony, positionz, orientation, set_time) VALUES (" +
			std::to_string(pguid) + ", " +
			std::to_string(map)   + ", " +
			std::to_string(x)     + ", " +
			std::to_string(y)     + ", " +
			std::to_string(z)     + ", " +
			std::to_string(o)     + ", NOW())";
	
		WorldDatabase.Execute(q);
	}
	
	// --- načte back bod ---
	static bool LoadBackPoint(uint32 pguid,
							/*out*/ uint32& map, /*out*/ double& x, /*out*/ double& y,
							/*out*/ double& z, /*out*/ float& o)
	{
		if (QueryResult res = WorldDatabase.Query(
				"SELECT map, positionx, positiony, positionz, orientation "
				"FROM customs.gv_teleport_back WHERE player={} LIMIT 1", pguid))
		{
			Field* f = res->Fetch();
			map = f[0].Get<uint32>(); x = f[1].Get<double>(); y = f[2].Get<double>();
			z   = f[3].Get<double>(); o = f[4].Get<float>();
			return true;
		}
		return false;
	}
	
	// --- po úspěšném návratu smaž bod ---
	static void ClearBackPoint(uint32 pguid)
	{
		WorldDatabase.Execute("DELETE FROM customs.gv_teleport_back WHERE player=" + std::to_string(pguid));
	}
	
    // --- rozdělení "arg1 arg2 ..." na 1. token a zbytek
    static inline void SplitFirstToken(std::string const& in, std::string& tok, std::string& rest)
    {
        std::string s = Trim(in);
        auto p = s.find(' ');
        if (p == std::string::npos) { tok = s; rest.clear(); }
        else { tok = Trim(s.substr(0, p)); rest = Trim(s.substr(p + 1)); }
    }

    static inline bool IsSetArg(std::string const& sLower)
    {
        return sLower == "set";
    }
	
	// --- color helpers ---
	static inline std::string CWhite(std::string const& s) { return "|cffffffff" + s + "|r"; }
	
	// WoW quality → hex barva (WotLK)
	static inline char const* QualityHex(uint32 q)
	{
		switch (q)
		{
			case 0: return "ff9d9d9d"; // Poor
			case 1: return "ffffffff"; // Common
			case 2: return "ff1eff00"; // Uncommon
			case 3: return "ff0070dd"; // Rare
			case 4: return "ffa335ee"; // Epic
			case 5: return "ffff8000"; // Legendary
			case 6: return "ffe6cc80"; // Artifact
			case 7: return "ffe6cc80"; // Heirloom (WotLK-like)
			default:return "ffffffff";
		}
	}
	
	// Sestaví barevný klikací odkaz na item (s lokalizovaným názvem z DBC/DB)
	static inline std::string BuildItemLink(uint32 itemId)
	{
		if (ItemTemplate const* it = sObjectMgr->GetItemTemplate(itemId))
		{
			char const* hex = QualityHex(it->Quality);
			// minimální link: |cffHEX|Hitem:itemId:0:0:0:0:0:0:0|h[Name]|h|r
			return Acore::StringFormat("|c{}|Hitem:{}:0:0:0:0:0:0:0|h[{}]|h|r", hex, itemId, it->Name1);
		}
		// fallback
		return Acore::StringFormat("|cffffffff|Hitem:{}:0:0:0:0:0:0:0|h[Item {}]|h|r", itemId, itemId);
	}

    // uloží osobní TP bod do customs.gv_teleport_player
    static bool SavePersonalVillageTp(Player* player, ChatHandler* handler)
    {
        if (!player || !player->GetGuild())
        {
            handler->SendSysMessage(T("Nejsi v žádné guildě.", "You are not in a guild."));
            return false;
        }

        if (player->GetMapId() != DefMap())
        {
            handler->SendSysMessage(T("Osobní bod lze nastavit pouze uvnitř guildovní vesnice.",
                                      "Personal point can only be set while inside the guild village."));
            return false;
        }

        // guildu a phase vezmu z customs.gv_guild (stejně jako při default TP)
        uint32 guildId = player->GetGuildId();
        uint32 phaseMask = 0;
        uint32 mapId = player->GetMapId();

        if (QueryResult res = WorldDatabase.Query(
                "SELECT phase FROM customs.gv_guild WHERE guild={}", guildId))
        {
            phaseMask = res->Fetch()[0].Get<uint32>();
        }
        else
        {
            handler->SendSysMessage(T("Tvoje guilda nevlastní guildovní vesnici.",
                                      "Your guild does not own a guild village."));
            return false;
        }

        uint32 pguid = player->GetGUID().GetCounter();
        float x = player->GetPositionX();
        float y = player->GetPositionY();
        float z = player->GetPositionZ();
        float o = player->GetOrientation();

        std::string q =
            "INSERT INTO customs.gv_teleport_player "
            "(player, guild, map, positionx, positiony, positionz, orientation, phase) VALUES (" +
            std::to_string(pguid) + ", " +
            std::to_string(guildId) + ", " +
            std::to_string(mapId) + ", " +
            std::to_string(x) + ", " +
            std::to_string(y) + ", " +
            std::to_string(z) + ", " +
            std::to_string(o) + ", " +
            std::to_string(phaseMask) + ") "
            "ON DUPLICATE KEY UPDATE "
            "guild=VALUES(guild), map=VALUES(map), positionx=VALUES(positionx), positiony=VALUES(positiony), "
            "positionz=VALUES(positionz), orientation=VALUES(orientation), phase=VALUES(phase), set_time=NOW()";

        WorldDatabase.Execute(q);

        handler->SendSysMessage(T("Osobní bod teleportu nastaven.",
                                  "Personal teleport point set."));
        return true;
    }

    // načte osobní TP; vrátí true pokud nalezeno
    static bool LoadPersonalVillageTp(Player* player,
                                      /*out*/ uint32& map, /*out*/ double& x, /*out*/ double& y,
                                      /*out*/ double& z, /*out*/ float& o, /*out*/ uint32& phaseMask)
    {
        if (!player || !player->GetGuild())
            return false;

        uint32 pguid = player->GetGUID().GetCounter();
        uint32 guildId = player->GetGuildId();

        if (QueryResult res = WorldDatabase.Query(
                "SELECT map, positionx, positiony, positionz, orientation, phase "
                "FROM customs.gv_teleport_player WHERE player={} AND guild={} LIMIT 1",
                pguid, guildId))
        {
            Field* f = res->Fetch();
            map       = f[0].Get<uint32>();
            x         = f[1].Get<double>();
            y         = f[2].Get<double>();
            z         = f[3].Get<double>();
            o         = f[4].Get<float>();
            phaseMask = f[5].Get<uint32>();
            return true;
        }
        return false;
    }
	
    // --- info aliases helper ---
    static inline bool IsInfoArg(std::string const& sLower)
    {
        return sLower == "i"
            || sLower == "in"
            || sLower == "inf"
            || sLower == "info";
    }

    // --- expedition aliases helper ---
    static inline bool IsExpeditionArg(std::string const& sLower)
    {
        return sLower == "e"
            || sLower == "exp"
            || sLower == "expe"
            || sLower == "exped"
            || sLower == "expedition";
    }

    // --- quests aliases helper (nové: daily / weekly) ---
    static inline bool IsQuestDailyArg(std::string const& sLower)
    {
        return sLower == "questdaily" || sLower == "qd";
    }

    static inline bool IsQuestWeeklyArg(std::string const& sLower)
    {
        return sLower == "questweekly" || sLower == "qw";
    }


    // === Per-player stash (sdílené jméno s ostatními částmi modu) ===
    struct GVPhaseData : public DataMap::Base
    {
        uint32 phaseMask = 0; // cílová phase po teleportu
    };

    // === PlayerScript: sjednocené chování pro příkaz i NPC ===
    class guild_village_PlayerPhase : public PlayerScript
    {
    public:
        guild_village_PlayerPhase() : PlayerScript("guild_village_PlayerPhase") { }

        static uint32 GetNormalPhase(Player* plr)
        {
            if (plr->IsGameMaster()) return PHASEMASK_ANYWHERE;
            uint32 p = plr->GetPhaseByAuras();
            return p ? p : PHASEMASK_NORMAL;
        }

        // vesnice: pevně mapa 37
        static bool InVillage(Player* p) { return p && p->GetMapId() == 37; }

        static void ApplyGVPhaseIfNeeded(Player* player)
        {
            if (!player) return;

            if (InVillage(player))
            {
                // 1) stash z příkazu/NPC (zapsán přes GetDefault v teleportu)
                if (auto* stash = player->CustomData.Get<GVPhaseData>("gv_phase"))
                {
                    if (stash->phaseMask)
                    {
                        player->SetPhaseMask(stash->phaseMask, true);
                        return;
                    }
                }
                // 2) fallback z DB (pro případ, že stash není)
                if (QueryResult res = WorldDatabase.Query(
                        "SELECT phase FROM customs.gv_guild WHERE guild={}", player->GetGuildId()))
                {
                    uint32 ph = res->Fetch()[0].Get<uint32>();
                    if (ph)
                    {
                        player->SetPhaseMask(ph, true);
                        return;
                    }
                }
            }

            // mimo vesnici (nebo nic v DB)
            player->SetPhaseMask(GetNormalPhase(player), true);
        }

        void OnPlayerLogin(Player* p) override                      { ApplyGVPhaseIfNeeded(p); }
        void OnPlayerMapChanged(Player* p) override                 { ApplyGVPhaseIfNeeded(p); }
        void OnPlayerUpdateZone(Player* p, uint32, uint32) override { ApplyGVPhaseIfNeeded(p); }
    };

    static bool PrepareVillageStatus(Player* player,
                                     ChatHandler* handler,
                                     GuildVillageProduction::GuildCurrency& outCur,
                                     GuildVillage::Names::All const*& outNames)
    {
        if (!player->GetGuild())
        {
            handler->SendSysMessage(T("Nejsi v žádné guildě.", "You are not in a guild."));
            return false;
        }

        if (!CanUseStatus(player))
        {
            handler->SendSysMessage(T("Na tento příkaz nemáš oprávnění.",
                                      "You are not allowed to use this command."));
            return false;
        }

        // Syncne produkci = dopočítá tick a případně stopne při capu
        auto curOpt = GuildVillageProduction::SyncGuildProduction(player->GetGuildId());
        if (!curOpt.has_value())
        {
            handler->SendSysMessage(T("Tvá guilda nevlastní guildovní vesnici.",
                                      "Your guild does not own a guild village."));
            return false;
        }

        outCur = *curOpt;
        outNames = &GuildVillage::Names::Get();
        return true;
    }

    // ---- blok [Expedice] (probíhající expedice)
    static void SendExpeditionBlock(Player* player, ChatHandler* handler)
    {
        handler->SendSysMessage(
            T("|cff00ff00[Expedice]|r",
              "|cff00ff00[Expeditions]|r")
        );

        if (!player->GetGuild())
        {
            handler->SendSysMessage(
                T("Nejsi v guildě.", "You are not in a guild.")
            );
            return;
        }

        std::vector<GuildVillageMissions::ExpeditionLine> lines =
            GuildVillageMissions::BuildExpeditionLinesForGuild(player->GetGuildId());

        if (lines.empty())
        {
            handler->SendSysMessage(
                T("Žádná aktivní expedice.",
                  "No active expeditions.")
            );
            return;
        }

        for (auto const& L : lines)
        {
            // "Utgarde Keep - 1h 23m 15s"
            std::string row = Acore::StringFormat(
                "{} - {}",
                L.mission,
                L.remain
            );
            handler->SendSysMessage(row.c_str());
        }
    }

    // ---- blok [Bossové]
    static void SendBossBlock(Player* player, ChatHandler* handler)
    {
        handler->SendSysMessage(
            T("|cff00ff00[Bossové]|r",
              "|cff00ff00[Bosses]|r")
        );

        for (BossDef const& b : kBosses)
            handler->SendSysMessage(BossStatusLine(player->GetGuildId(), b).c_str());
    }

    // ---- blok [Produkce]
    static void SendProductionBlock(Player* player, ChatHandler* handler,
                                    GuildVillageProduction::GuildCurrency const& cur,
                                    GuildVillage::Names::All const& N)
    {
        handler->SendSysMessage(
            T("|cff00ff00[Produkce]|r",
              "|cff00ff00[Production]|r")
        );

        uint8 activeMatId = GuildVillageProduction::GetCurrentlyActiveMaterial(player->GetGuildId());
        if (activeMatId == 0)
        {
            handler->SendSysMessage(
                T("Není aktivní žádná výroba.",
                  "No production is currently running.")
            );
            return;
        }

        // jméno materiálu
        std::string matName;
        switch (activeMatId)
        {
            case 1: matName = N.status.material1; break;
            case 2: matName = N.status.material2; break;
            case 3: matName = N.status.material3; break;
            case 4: matName = N.status.material4; break;
            default: matName = (LangOpt()==Lang::EN ? "Unknown" : "Neznámý"); break;
        }

        // detail pro ten materiál
        GuildVillageProduction::ProdStatusForMat st =
            GuildVillageProduction::GetProductionStatus(player->GetGuildId(), activeMatId);

        handler->SendSysMessage(
            Acore::StringFormat(
                T("Právě je aktivní produkce: {}",
                  "Currently producing: {}"),
                matName
            ).c_str()
        );

        // zápis intervalu
        char buf[32];
        float h = st.hoursPerTick;
        uint32 hInt = (uint32)std::floor(h + 0.0001f);
        if (std::fabs(h - (float)hInt) < 0.001f)
            std::snprintf(buf, sizeof(buf), "%uh", hInt);
        else
            std::snprintf(buf, sizeof(buf), "%.2fh", h);

        handler->SendSysMessage(
            Acore::StringFormat(
                T("Produkuje: +{} každých {}",
                  "Producing: +{} every {}"),
                st.amountPerTick,
                buf
            ).c_str()
        );
    }

    // ---- blok [Materiály] (currency)
    static void SendCurrencyBlock(ChatHandler* handler,
                                  GuildVillageProduction::GuildCurrency const& cur,
                                  GuildVillage::Names::All const& N)
    {

        handler->SendSysMessage(
            T("|cff00ff00[Materiál]|r",
              "|cff00ff00[Materials]|r")
        );

        auto sendMatLine = [&](std::string const& dispName, uint64 curVal, uint32 cap)
        {
            std::string line = "|cff00ffff" + dispName + ":|r " + std::to_string(curVal);
            if (CapsEnabled())
            {
                if (cap == 0)
                    line += " / ∞";
                else
                    line += " / " + std::to_string(cap);
            }
            handler->SendSysMessage(line.c_str());
        };

        sendMatLine(N.status.material1, cur.material1, CapMaterial1());
        sendMatLine(N.status.material2, cur.material2, CapMaterial2());
        sendMatLine(N.status.material3, cur.material3, CapMaterial3());
        sendMatLine(N.status.material4, cur.material4, CapMaterial4());

    }

    // ====== [Quests] podpora (lazy-assign + výpis jako GO gossip) ======

    enum class ResetType : uint8 { Daily = 1, Weekly = 2 };

    static inline const char* ResetName(ResetType rt) { return (rt == ResetType::Daily) ? "daily" : "weekly"; }

    // --- NOVÉ: konfig klíče pro quests (oddělené)
    static inline std::string DailyResetTimeStr()
    {
        return sConfigMgr->GetOption<std::string>("GuildVillage.Quests.DailyReset.Time", "04:00");
    }
    static inline std::string WeeklyResetTimeStr()
    {
        return sConfigMgr->GetOption<std::string>("GuildVillage.Quests.WeeklyReset.Time", "04:00");
    }
    static inline std::string WeeklyResetDayStr()
    {
        return sConfigMgr->GetOption<std::string>("GuildVillage.Quests.WeeklyReset.Day", "Mon");
    }

    static bool GuildHasQuestsUpgrade(uint32 guildId)
	{
		return WorldDatabase.Query(
			"SELECT 1 FROM customs.gv_upgrades WHERE guildId={} AND expansion_key='quests' LIMIT 1",
			guildId) != nullptr;
	}

    // pomocné: parsování HH:MM a výpočet next TS (stejně jako v quests.cpp)
    static bool ParseHHMM(std::string const& s, int& outH, int& outM)
    {
        outH=0; outM=0;
        if (s.size() < 4) return false;
        char colon=':'; int h=0,m=0;
        if (std::sscanf(s.c_str(), "%d%c%d", &h, &colon, &m) == 3 && colon==':' && h>=0 && h<=23 && m>=0 && m<=59)
        { outH=h; outM=m; return true; }
        return false;
    }
    static uint32 ToUnix(std::tm tmv)
    {
        time_t t = std::mktime(&tmv);
        if (t < 0) t = 0;
        return static_cast<uint32>(t);
    }
    static uint32 CalcNextDailyResetTS()
    {
        int H=4, M=0; ParseHHMM(DailyResetTimeStr(), H, M);
        time_t nowT = time(nullptr);
        std::tm now = *std::localtime(&nowT);

        std::tm target = now;
        target.tm_hour = H; target.tm_min = M; target.tm_sec = 0;

        uint32 todayAt = ToUnix(target);
        if (static_cast<uint32>(nowT) <= todayAt)
            return todayAt;

        target = *std::localtime(&nowT);
        target.tm_mday += 1;
        target.tm_hour = H; target.tm_min = M; target.tm_sec = 0;
        return ToUnix(target);
    }

    // >>> ZMĚNĚNO: rozšířený parser dne (EN, CZ, čísla 0–6, 7→0) <<<
    static int DOWFromToken(std::string day)
    {
        // trim
        auto ltrim = [](std::string& s){ s.erase(0, s.find_first_not_of(" \t\r\n")); };
        auto rtrim = [](std::string& s){ s.erase(s.find_last_not_of(" \t\r\n") + 1); };
        ltrim(day); rtrim(day);

        // to lower
        std::transform(day.begin(), day.end(), day.begin(), ::tolower);

        // numeric support: 0..6 (0=neděle/sunday), 7 -> 0
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

        // CZ (s i bez diakritiky + zkratky)
        if (day == "nedele"  || day == "neděle"  || day == "ne") return 0;
        if (day == "pondeli" || day == "pondělí" || day == "po") return 1;
        if (day == "utery"   || day == "úterý"   || day == "ut" || day == "út") return 2;
        if (day == "streda"  || day == "středa"  || day == "st") return 3;
        if (day == "ctvrtek" || day == "čtvrtek" || day == "ct" || day == "čt") return 4;
        if (day == "patek"   || day == "pátek"   || day == "pa" || day == "pá") return 5;
        if (day == "sobota"  || day == "so")                     return 6;

        // fallback – pondělí (1)
        return 1;
    }

    static uint32 CalcNextWeeklyResetTS()
    {
        int H=4, M=0; ParseHHMM(WeeklyResetTimeStr(), H, M);
        int tgt = DOWFromToken(WeeklyResetDayStr());

        time_t nowT = time(nullptr);
        std::tm now = *std::localtime(&nowT);

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

    // Lazy-assign jako v GO gossip: pokud vypršelo, přidělí nový quest
    static void EnsureGuildQuestAssigned(uint32 guildId, ResetType rt)
    {
        if (!GuildHasQuestsUpgrade(guildId))
            return;

        if (QueryResult r = WorldDatabase.Query(
                "SELECT next_rotation_at FROM customs.gv_guild_quests "
                "WHERE guildId={} AND reset_type={} LIMIT 1", guildId, (uint32)rt))
        {
            uint32 until = r->Fetch()[0].Get<uint32>();
            if (until > uint32(time(nullptr)))
                return;
        }

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
				"ORDER BY RAND() LIMIT 1",
				ResetName(rt), guildId))
		{
			Field* f = q->Fetch();
			uint32 qid  = f[0].Get<uint32>();
			uint32 goal = f[1].Get<uint32>();
	
			uint32 now  = uint32(time(nullptr));
			uint32 next = (rt == ResetType::Daily) ? CalcNextDailyResetTS() : CalcNextWeeklyResetTS();
			if (!next || next <= now) next = now + (rt == ResetType::Daily ? 86400u : 7u*86400u);
	
			WorldDatabase.DirectExecute(Acore::StringFormat(
				"REPLACE INTO customs.gv_guild_quests "
				"(guildId, reset_type, quest_id, progress, goal, completed, reward_claimed, assigned_at, next_rotation_at) "
				"VALUES ({}, {}, {}, 0, {}, 0, 0, {}, {})",
				guildId, (uint32)rt, qid, goal, now, next).c_str());
		}
    }

    // --- Reward line (stejně jako v GO: načte tokeny a složí text) ---
    enum class RewardKind { None, Mat1, Mat2, Mat3, Mat4, Random, Item };
    struct RewardToken { RewardKind kind=RewardKind::None; uint32 itemId=0; uint32 count=0; };

    static RewardToken ParseRewardToken(std::string s, uint32 cnt)
    {
        RewardToken t; t.count = cnt;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        if (s.empty() || cnt == 0) return t;
        bool numeric = !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
        if (numeric) { t.kind=RewardKind::Item; t.itemId=(uint32)std::strtoul(s.c_str(), nullptr, 10); return t; }
        if (s=="material1") t.kind=RewardKind::Mat1;
        else if (s=="material2") t.kind=RewardKind::Mat2;
        else if (s=="material3") t.kind=RewardKind::Mat3;
        else if (s=="material4") t.kind=RewardKind::Mat4;
        else if (s=="random")       t.kind=RewardKind::Random;
        return t;
    }

	// --- Reward line (stejně jako v GO: načte tokeny a složí text) ---
	static std::string RewardTokenToText(RewardToken const& t)
	{
		if (t.kind == RewardKind::None || t.count == 0)
			return "";
	
		// Item: "5x " (bíle) + ItemLink (barevně podle quality)
		if (t.kind == RewardKind::Item)
		{
			std::string prefix = CWhite(Acore::StringFormat("{}x ", t.count));
			return prefix + BuildItemLink(t.itemId);
		}
	
		using GuildVillage::Names::Mat;
		auto matName = [&](Mat m) -> std::string { return GuildVillage::Names::CountName(m, t.count); };
	
		// Materiály / Random → celé bíle (nemají vlastní quality barvu ani link)
		switch (t.kind)
		{
			case RewardKind::Mat1:  return CWhite(Acore::StringFormat("+{} {}", t.count, matName(Mat::Material1)));
			case RewardKind::Mat2:  return CWhite(Acore::StringFormat("+{} {}", t.count, matName(Mat::Material2)));
			case RewardKind::Mat3:  return CWhite(Acore::StringFormat("+{} {}", t.count, matName(Mat::Material3)));
			case RewardKind::Mat4:  return CWhite(Acore::StringFormat("+{} {}", t.count, matName(Mat::Material4)));
			case RewardKind::Random:
			{
				std::string any = (LangOpt()==Lang::EN) ? "Random material" : "Náhodný materiál";
				return CWhite(Acore::StringFormat("+{} {}", t.count, any));
			}
			default: break;
		}
		return "";
	}

    static std::string BuildRewardLine(uint32 questId)
	{
		std::string label = (LangOpt()==Lang::EN) ? "Reward: " : "Odměna: ";
	
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
				if (i) joined += CWhite(", "); // čárka taky bíle
				joined += parts[i];
			}
	
			if (joined.empty())
				joined = CWhite("—");
	
			return label + joined;
		}
		return label + CWhite("—");
	}

	// --- Struktura pro výpis questů v příkazu ---
	struct CmdGuildQuestRow
	{
		uint8  slot = 0;
		uint32 questId = 0;
		uint32 progress = 0;
		uint32 goal = 0;
		bool   completed = false;
	
		// NOVĚ: název questu
		std::string name_cs;
		std::string name_en;
	
		// Popis (dříve používaný jako titulek ve výpisu)
		std::string info_cs;
		std::string info_en;
	};

	// Načtení všech questů pro daný reset_type (daily/weekly)
	static std::vector<CmdGuildQuestRow> LoadGuildQuestsForCmd(uint32 guildId, ResetType rt)
	{
		std::vector<CmdGuildQuestRow> out;
	
		if (QueryResult r = WorldDatabase.Query(
				"SELECT g.slot, g.quest_id, g.progress, g.goal, g.completed, "
				"       c.info_cs, c.info_en, c.quest_name_cs, c.quest_name_en "
				"FROM customs.gv_guild_quests g "
				"LEFT JOIN customs.gv_quest_catalog c ON c.id=g.quest_id "
				"WHERE g.guildId={} AND g.reset_type={} "
				"ORDER BY g.slot ASC",
				guildId, (uint32)rt))
		{
			do
			{
				Field* f = r->Fetch();
				CmdGuildQuestRow row;
				row.slot      = f[0].Get<uint8>();
				row.questId   = f[1].Get<uint32>();
				row.progress  = f[2].Get<uint32>();
				row.goal      = f[3].Get<uint32>();
				row.completed = f[4].Get<bool>();
	
				row.info_cs   = f[5].IsNull() ? "" : f[5].Get<std::string>();
				row.info_en   = f[6].IsNull() ? "" : f[6].Get<std::string>();
				row.name_cs   = f[7].IsNull() ? "" : f[7].Get<std::string>();
				row.name_en   = f[8].IsNull() ? "" : f[8].Get<std::string>();
	
				out.push_back(row);
			}
			while (r->NextRow());
		}
	
		return out;
	}

// Výpis questů pro příkaz .v qd / .v qw se stránkováním
static void SendQuestListPaged(Player* player, ChatHandler* handler, ResetType rt, uint32 page)
{
    if (!player->GetGuild())
    {
        handler->SendSysMessage(T("Nejsi v žádné guildě.", "You are not in a guild."));
        return;
    }

    uint32 gid = player->GetGuildId();

    if (!GuildHasQuestsUpgrade(gid))
    {
        handler->SendSysMessage(
            T("Tvoje guilda nemá zakoupené rozšíření na úkoly.",
              "Your guild hasn't purchased the Quests expansion.")
        );
        return;
    }

    // Lazy-assign přes sdílenou logiku v quests.cpp
    GuildVillage::GV_EnsureGuildQuestsAssignedForGuild(gid);

    auto rows = LoadGuildQuestsForCmd(gid, rt);
    bool en = (LangOpt() == Lang::EN);

    char const* headCs = (rt == ResetType::Daily) ? "Denní úkol" : "Týdenní úkol";
    char const* headEn = (rt == ResetType::Daily) ? "Daily quest" : "Weekly quest";
    std::string headBase = en ? headEn : headCs;

    if (rows.empty())
    {
        handler->SendSysMessage(
            (headBase + std::string(": ") +
             (en ? CWhite("No quest.") : CWhite("Žádný úkol."))).c_str()
        );

        {
            std::string s = std::string(en ? "Progress: " : "Postup: ") + CWhite("—");
            handler->SendSysMessage(s.c_str());
        }
        {
            std::string s = std::string(en ? "Reward: " : "Odměna: ") + CWhite("—");
            handler->SendSysMessage(s.c_str());
        }
        return;
    }

    uint32 total = (uint32)rows.size();

    // Jednoduchý režim: jen jeden úkol => bez stránkování
    if (total <= 1)
    {
        CmdGuildQuestRow const& row = rows[0];

        // Titulek = quest_name (fallback na info, pak „Quest“)
        std::string title = en ? row.name_en : row.name_cs;
        if (title.empty()) title = en ? row.info_en : row.info_cs;
        if (title.empty()) title = en ? "Quest" : "Úkol";

        handler->SendSysMessage(headBase.c_str());
        handler->SendSysMessage((std::string(en ? "Quest: " : "Úkol: ") + CWhite(title)).c_str());

        // Popis (z info_cs/en) – hned nad „Postup:“
        {
            std::string desc = en ? row.info_en : row.info_cs;
            if (!desc.empty())
            {
                std::string label = en ? "Info: " : "Info: ";
                handler->SendSysMessage((label + CWhite(desc)).c_str());
            }
        }

        // Progress – hodnota bíle
        {
            std::string progLabel = en ? "Progress: " : "Postup: ";
            std::string progVal   = std::to_string(row.progress) + "/" + std::to_string(row.goal);
            handler->SendSysMessage((progLabel + CWhite(progVal)).c_str());
        }

        // Reward – BuildRewardLine už řeší barvy/link
        std::string rewardLine = BuildRewardLine(row.questId);
        handler->SendSysMessage(rewardLine.c_str());

        // Status beze změny (má vlastní barvy)
        {
            std::string st = en ? "Status: " : "Stav: ";
            if (row.completed)
                st += en ? "|cff00ff00Quest completed|r"
                         : "|cff00ff00Úkol splněný|r";
            else
                st += en ? "|cffff0000Quest not completed yet|r"
                         : "|cffff0000Úkol ještě není dokončen|r";
            handler->SendSysMessage(st.c_str());
        }
        return;
    }

    // --- víc questů => stránkování (1 quest = 1 strana) ---
    if (page == 0) page = 1;
    if (page > total)
    {
        if (en)
            handler->SendSysMessage(Acore::StringFormat("Invalid page. Use 1-{}.", total).c_str());
        else
            handler->SendSysMessage(Acore::StringFormat("Neplatná stránka. Použij 1-{}.", total).c_str());
        return;
    }

    CmdGuildQuestRow const& row = rows[page - 1];

    // Nadpis s číslem slotu a info o stránce
    std::string head;
    if (en)
        head = Acore::StringFormat("{} #{} (page {}/{})", headBase, (uint32)row.slot, page, total);
    else
        head = Acore::StringFormat("{} #{} (strana {}/{})", headBase, (uint32)row.slot, page, total);

    // Titulek = quest_name (fallbacky jako výše)
    std::string title = en ? row.name_en : row.name_cs;
    if (title.empty()) title = en ? row.info_en : row.info_cs;
    if (title.empty()) title = en ? "Quest" : "Úkol";

    handler->SendSysMessage(head.c_str());
    handler->SendSysMessage((std::string(en ? "Quest: " : "Úkol: ") + CWhite(title)).c_str());

    // Popis nad „Postup:“
    {
        std::string desc = en ? row.info_en : row.info_cs;
        if (!desc.empty())
        {
            std::string label = en ? "Info: " : "Info: ";
            handler->SendSysMessage((label + CWhite(desc)).c_str());
        }
    }

    // Progress – hodnota bíle
    {
        std::string progLabel = en ? "Progress: " : "Postup: ";
        std::string progVal   = std::to_string(row.progress) + "/" + std::to_string(row.goal);
        handler->SendSysMessage((progLabel + CWhite(progVal)).c_str());
    }

    // Reward – viz BuildRewardLine
    {
        std::string rewardLine = BuildRewardLine(row.questId);
        handler->SendSysMessage(rewardLine.c_str());
    }

    // Status beze změny
    {
        std::string st = en ? "Status: " : "Stav: ";
        if (row.completed)
            st += en ? "|cff00ff00Quest completed|r"
                     : "|cff00ff00Úkol splněný|r";
        else
            st += en ? "|cffff0000Quest not completed yet|r"
                     : "|cffff0000Úkol ještě není dokončen|r";
        handler->SendSysMessage(st.c_str());
    }
}

    // === Single command handler: ".village …" / ".v …" ===
    static bool HandleVillage(ChatHandler* handler, char const* args)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player) return false;

        std::string a = args ? Trim(args) : std::string();
        std::string al = Lower(a);

        // help
        if (al == "help" || al == "?")
        {
            if (LangOpt() == Lang::EN)
            {
                handler->SendSysMessage(R"(
|cffffd000[Guild Village]|r – commands:
|cff00ff00.village info|r
Show complete village info (materials, production, expeditions, bosses).
Aliases: |cff00ff00.village i|r, |cff00ff00.village info|r, |cff00ff00.v i|r, |cff00ff00.v info|r
-----------------------------
|cff00ff00.village expedition|r
Show active expeditions and remaining time.
Aliases: |cff00ff00.village e|r, |cff00ff00.v e|r, |cff00ff00.v expedition|r
-----------------------------
|cff00ff00.village boss|r
Show raid boss status (alive / respawn time).
Aliases: |cff00ff00.village b|r, |cff00ff00.v b|r, |cff00ff00.v boss|r
-----------------------------
|cff00ff00.village currency|r
Show your guild materials and caps.
Aliases: |cff00ff00.village c|r, |cff00ff00.v c|r, |cff00ff00.v currency|r
-----------------------------
|cff00ff00.village production|r
Show current production (active material, tick and amount).
Aliases: |cff00ff00.village p|r, |cff00ff00.v p|r, |cff00ff00.v production|r
-----------------------------
|cff00ff00.village questdaily [page]|r
Show daily guild quests (one quest per page).
Aliases: |cff00ff00.village qd|r, |cff00ff00.v qd|r
-----------------------------
|cff00ff00.village questweekly [page]|r
Show weekly guild quests (one quest per page).
Aliases: |cff00ff00.village qw|r, |cff00ff00.v qw|r)");
				if (BackEnabled())
				{
					handler->SendSysMessage(R"(
-----------------------------
|cff00ff00.village teleport|r
Teleport you to the guild village (personal point if set).
Aliases: |cff00ff00.village tp|r, |cff00ff00.v tp|r, |cff00ff00.village teleport|r, |cff00ff00.v teleport|r
-----------------------------
|cff00ff00.village tp set|r
Set your personal teleport point inside the village.
Aliases: |cff00ff00.v tp set|r
-----------------------------
|cff00ff00.village back|r
Return from the village to your last saved position.
Aliases: |cff00ff00.v back|r)");
				}
				else
				{
					handler->SendSysMessage(R"(
|cff00ff00.village teleport|r
Teleport you to the guild village (personal point if set).
Aliases: |cff00ff00.village tp|r, |cff00ff00.v tp|r, |cff00ff00.village teleport|r, |cff00ff00.v teleport|r
-----------------------------
|cff00ff00.village tp set|r
Set your personal teleport point inside the village.
Aliases: |cff00ff00.v tp set|r)");
				}
			}
			else
			{
				handler->SendSysMessage(R"(
|cffffd000[Guildovní vesnice]|r – příkazy:
|cff00ff00.village info|r
Zobrazí kompletní info o vesnici (suroviny, produkce, expedice, bossové).
Alias: |cff00ff00.village i|r, |cff00ff00.village info|r, |cff00ff00.v i|r, |cff00ff00.v info|r
-----------------------------
|cff00ff00.village expedition|r
Ukáže probíhající expedice a zbývající čas.
Alias: |cff00ff00.village e|r, |cff00ff00.v e|r, |cff00ff00.v expedition|r
-----------------------------
|cff00ff00.village boss|r
Zobrazí stav bossů (naživu / čas respawnu).
Alias: |cff00ff00.village b|r, |cff00ff00.v b|r, |cff00ff00.v boss|r
-----------------------------
|cff00ff00.village currency|r
Zobrazí guildovní suroviny a případné capy.
Alias: |cff00ff00.village c|r, |cff00ff00.v c|r, |cff00ff00.v currency|r
-----------------------------
|cff00ff00.village production|r
Zobrazí aktuální produkci (materiál, interval, množství).
Alias: |cff00ff00.village p|r, |cff00ff00.v p|r, |cff00ff00.v production|r
-----------------------------
|cff00ff00.village questdaily [strana]|r
Zobrazí denní guildovní úkoly (jeden úkol = jedna strana).
Alias: |cff00ff00.village qd|r, |cff00ff00.v qd|r
-----------------------------
|cff00ff00.village questweekly [strana]|r
Zobrazí týdenní guildovní úkoly (jeden úkol = jedna strana).
Alias: |cff00ff00.village qw|r, |cff00ff00.v qw|r)");
				if (BackEnabled())
				{
					handler->SendSysMessage(R"(
-----------------------------
|cff00ff00.village teleport|r
Teleportuje tě do guildovní vesnice (pokud máš osobní bod, použije se ten).
Alias: |cff00ff00.village tp|r, |cff00ff00.v tp|r, |cff00ff00.village teleport|r, |cff00ff00.v teleport|r
-----------------------------
|cff00ff00.village tp set|r
Nastaví tvůj osobní bod teleportu ve vesnici.
Alias: |cff00ff00.v tp set|r
-----------------------------
|cff00ff00.village back|r
Návrat z vesnice na poslední uloženou pozici.
Alias: |cff00ff00.v back|r)");
				}
				else
				{
					handler->SendSysMessage(R"(
|cff00ff00.village teleport|r
Teleportuje tě do guildovní vesnice (pokud máš osobní bod, použije se ten).
Alias: |cff00ff00.village tp|r, |cff00ff00.v tp|r, |cff00ff00.village teleport|r, |cff00ff00.v teleport|r
-----------------------------
|cff00ff00.village tp set|r
Nastaví tvůj osobní bod teleportu ve vesnici.
Alias: |cff00ff00.v tp set|r)");
				}
			}
			return true;
		}

		// --- BACK: .village back / .v back / ---
		{
			std::string tok1, rest;
			SplitFirstToken(al, tok1, rest);
		
			if (IsBackArg(tok1))
			{
				
				if (!BackEnabled())
				{
					handler->SendSysMessage(T("Funkce návratu je na tomto serveru vypnutá.",
											"The back/return feature is disabled on this server."));
					return true;
				}
				// povoleno pouze z vesnice
				if (player->GetMapId() != DefMap())
				{
					handler->SendSysMessage(T("Příkaz lze použít pouze uvnitř guildovní vesnice.",
											"This command can only be used inside the guild village."));
					return true;
				}
		
				// bezpečnost: žádný BG a žádný combat (vesnice není instance)
				if (player->InBattleground())
				{
					handler->SendSysMessage(T("V bojišti/aréne se nemůžeš teleportovat.",
											"You can't teleport while in a battleground/arena."));
					return true;
				}
				if (player->IsInCombat())
				{
					handler->SendSysMessage(T("Nemůžeš teleportovat během boje.",
											"You can't teleport while in combat."));
					return true;
				}
		
				uint32 pguid = player->GetGUID().GetCounter();
				uint32 map = 0; double x=0,y=0,z=0; float o=0.f;
		
				if (!LoadBackPoint(pguid, map, x, y, z, o))
				{
					handler->SendSysMessage(T("Nemáš uloženou cílovou pozici pro návrat.",
											"You have no saved position to return to."));
					return true;
				}
		
				// návrat – po úspěchu smazat bod
				player->TeleportTo(map, x, y, z, o);
				ClearBackPoint(pguid);
		
				handler->SendSysMessage(T("Teleportuji zpět na poslední pozici mimo vesnici…",
										"Teleporting back to your last position outside the village…"));
				return true;
			}
		}

        // --- TELEPORT / TELEPORT SET ---
        {
            std::string tok1, rest;
            SplitFirstToken(al, tok1, rest);

            if (IsTeleportArg(tok1))
            {
                if (!player->GetGuild())
                {
                    handler->SendSysMessage(T("Nejsi v žádné guildě.", "You are not in a guild."));
                    return true;
                }

                // ".village tp set" => uložit osobní bod
                if (!rest.empty() && IsSetArg(Lower(rest)))
                {
                    SavePersonalVillageTp(player, handler);
                    return true;
                }
				
				// PvP blok
                if (player->InBattleground())
                {
                    handler->SendSysMessage(T("V bojišti/aréne nemůžeš teleportovat.",
                                              "You can't teleport while in a battleground/arena."));
                    return true;
                }
				
				// Combat blok
				if (player->IsInCombat())
				{
					handler->SendSysMessage(T("Nemůžeš teleportovat během boje.",
											"You can't teleport while in combat."));
					return true;
				}
				
				// Instance blok
				if (Map* m = player->GetMap())
				{
					if (m->Instanceable())
					{
						handler->SendSysMessage(T("V instanci (dungeon/raid) nelze teleportovat.",
												"Teleport is not allowed inside instances (dungeon/raid)."));
						return true;
					}
				}


                // 1) zkusit osobní bod
                uint32 map = 0, phaseMaskPersonal = 0;
                double x = 0, y = 0, z = 0; float o = 0.f;

                bool hasPersonal = LoadPersonalVillageTp(player, map, x, y, z, o, phaseMaskPersonal);

                // 2) default bod z customs.gv_guild (fallback)
                uint32 mapDef = 0, phaseMaskDef = 0;
                double xDef = 0, yDef = 0, zDef = 0; float oDef = 0.f;

                if (QueryResult res = WorldDatabase.Query(
                        "SELECT map, positionx, positiony, positionz, orientation, phase "
                        "FROM customs.gv_guild WHERE guild={}", player->GetGuildId()))
                {
                    Field* f = res->Fetch();
                    mapDef      = f[0].Get<uint32>();
                    xDef        = f[1].Get<double>();
                    yDef        = f[2].Get<double>();
                    zDef        = f[3].Get<double>();
                    oDef        = f[4].Get<float>();
                    phaseMaskDef= f[5].Get<uint32>();
                }
                else
                {
                    handler->SendSysMessage(T("Tvá guilda nevlastní guildovní vesnici.",
                                              "Your guild does not own a guild village."));
                    return true;
                }

                // 3) vybrat, co použít
                uint32 useMap = hasPersonal ? map : mapDef;
                double useX   = hasPersonal ? x   : xDef;
                double useY   = hasPersonal ? y   : yDef;
                double useZ   = hasPersonal ? z   : zDef;
                float  useO   = hasPersonal ? o   : oDef;
                uint32 usePhase = hasPersonal ? (phaseMaskPersonal ? phaseMaskPersonal : phaseMaskDef) : phaseMaskDef;

                // stash phase => PlayerScript ji aplikuje po dokončení TeleportTo
                auto* stash = player->CustomData.GetDefault<GVPhaseData>("gv_phase");
                stash->phaseMask = usePhase;
				
				SaveBackPointIfEligible(player);
				
                player->TeleportTo(useMap, useX, useY, useZ, useO);
                handler->SendSysMessage(
                    hasPersonal
                    ? T("Teleportuji na tvůj osobní bod ve vesnici…",
                        "Teleporting to your personal village point…")
                    : T("Teleportuji do guildovní vesnice…",
                        "Teleporting to the guild village…")
                );
                return true;
            }
        }
		
		// --- QUESTY: .village questdaily / questweekly (+ volitelná stránka) ---
        {
            std::string tok1, rest;
            SplitFirstToken(al, tok1, rest);

            if (IsQuestDailyArg(tok1) || IsQuestWeeklyArg(tok1))
            {
                uint32 page = 1;
                if (!rest.empty())
                {
                    bool numeric = std::all_of(rest.begin(), rest.end(),
                                               [](unsigned char c){ return std::isdigit(c); });
                    if (numeric)
                        page = (uint32)std::strtoul(rest.c_str(), nullptr, 10);
                }

                ResetType rt = IsQuestDailyArg(tok1) ? ResetType::Daily : ResetType::Weekly;
                SendQuestListPaged(player, handler, rt, page);
                return true;
            }
        }

        // aliasy pro jednotlivé sekce
        bool isInfo =
            (al == "i" || al == "in" || al == "inf" || al == "info");

        bool isExpedition =
            (IsExpeditionArg(al));

        bool isBoss =
            (al == "b" || al == "boss");

        bool isCurrency =
            (al == "c" || al == "currency");

        bool isProduction =
            (al == "p" || al == "prod" || al == "production");

        if (isInfo || isExpedition || isBoss || isCurrency || isProduction)
        {
            GuildVillageProduction::GuildCurrency cur;
            GuildVillage::Names::All const* names = nullptr;
            if (!PrepareVillageStatus(player, handler, cur, names))
                return true;

            // === info (všechno dohromady) ===
            if (isInfo)
            {
                handler->SendSysMessage(
                    T("|cff00ff00[Guildovní vesnice]|r – informace",
                      "|cff00ff00[Guild Village]|r – info")
                );

                // suroviny
                SendCurrencyBlock(handler, cur, *names);

                // produkce
                SendProductionBlock(player, handler, cur, *names);

                // expedice
                SendExpeditionBlock(player, handler);

                // bossové
                SendBossBlock(player, handler);

                return true;
            }

            // === jen expedice ===
            if (isExpedition)
            {
                SendExpeditionBlock(player, handler);
                return true;
            }

            // === jen currency ===
            if (isCurrency)
            {
                SendCurrencyBlock(handler, cur, *names);
                return true;
            }

            // === jen production ===
            if (isProduction)
            {
                SendProductionBlock(player, handler, cur, *names);
                return true;
            }

            // === jen boss ===
            if (isBoss)
            {
                SendBossBlock(player, handler);
                return true;
            }
        }

        // fallback
        handler->SendSysMessage(
            T("Špatný příkaz. Použij .village help",
              "Wrong command. Use .village help")
        );
        return true;
    }


    class GuildVillageCommandScript : public CommandScript
    {
    public:
        GuildVillageCommandScript() : CommandScript("GuildVillageCommandScript") { }

        std::vector<Acore::ChatCommands::ChatCommandBuilder> GetCommands() const override
        {
            using namespace Acore::ChatCommands;

            auto& fn = HandleVillage;
            ChatCommandBuilder village("village", fn, SEC_PLAYER, Console::No);
            ChatCommandBuilder vshort ("v",       fn, SEC_PLAYER, Console::No); // alias .v …

            std::vector<ChatCommandBuilder> out;
            out.emplace_back(village);
            out.emplace_back(vshort);
            return out;
        }
    };
}

// --- na úplném konci guild_village_commands.cpp ---
void RegisterGuildVillageCommands()
{
    new GuildVillageCommandScript();
    new guild_village_PlayerPhase();
}
