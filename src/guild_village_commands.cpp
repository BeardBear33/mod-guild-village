// modules/mod-guild-village/src/guild_village_commands.cpp

#include "ScriptMgr.h"
#include "Config.h"
#include "Chat.h"
#include "ChatCommand.h" // Acore::ChatCommands::ChatCommandBuilder
#include "Player.h"
#include "Guild.h"
#include "DatabaseEnv.h"
#include "DataMap.h"     // <— kvůli per-player “stash”

#include <string>
#include <algorithm>
#include <optional>
#include <vector>
#include <cctype>

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

    // === Material display names from config (localized) ===
    struct MatNames { std::string timber, stone, iron, crystal; };

    static MatNames GetMaterialNames()
    {
        MatNames m;
        if (LangOpt() == Lang::EN)
        {
            m.timber  = sConfigMgr->GetOption<std::string>("GuildVillage.MaterialEN.Timber",  "Timber");
            m.stone   = sConfigMgr->GetOption<std::string>("GuildVillage.MaterialEN.Stone",   "Stone");
            m.iron    = sConfigMgr->GetOption<std::string>("GuildVillage.MaterialEN.Iron",    "Iron");
            m.crystal = sConfigMgr->GetOption<std::string>("GuildVillage.MaterialEN.Crystal", "Crystal");
        }
        else
        {
            m.timber  = sConfigMgr->GetOption<std::string>("GuildVillage.Material.Timber",  "Dřevo");
            m.stone   = sConfigMgr->GetOption<std::string>("GuildVillage.Material.Stone",   "Kámen");
            m.iron    = sConfigMgr->GetOption<std::string>("GuildVillage.Material.Iron",    "Železo");
            m.crystal = sConfigMgr->GetOption<std::string>("GuildVillage.Material.Crystal", "Krystal");
        }
        return m;
    }
	
	// === Currency caps from config ===
	static inline bool CapsEnabled()
	{
		return sConfigMgr->GetOption<bool>("GuildVillage.CurrencyCap.Enabled", true);
	}
	static inline uint32 CapTimber()  { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Timber",   1000); }
	static inline uint32 CapStone()   { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Stone",    1000); }
	static inline uint32 CapIron()    { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Iron",     1000); }
	static inline uint32 CapCrystal() { return sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Crystal",  1000); }

    // === Data holders ===
    struct GuildCurrency { uint64 timber=0, stone=0, iron=0, crystal=0; };

    static std::optional<GuildCurrency> LoadGuildCurrency(uint32 guildId)
    {
        std::string sql =
            "SELECT timber, stone, iron, crystal "
            "FROM customs.gv_currency "
            "WHERE guildId=" + std::to_string(guildId);

        if (QueryResult res = WorldDatabase.Query(sql))
        {
            Field* f = res->Fetch();
            GuildCurrency c;
            c.timber  = f[0].Get<uint64>();
            c.stone   = f[1].Get<uint64>();
            c.iron    = f[2].Get<uint64>();
            c.crystal = f[3].Get<uint64>();
            return c;
        }
        return std::nullopt; // gilda nemá vesnici
    }

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

    // === Phase helpers ===
    struct GVPhaseData : public DataMap::Base
    {
        uint32 phaseMask = 0; // požadovaná phase po teleportu
    };

    static uint32 GetNormalPhase(Player* plr)
    {
        if (plr->IsGameMaster())
            return PHASEMASK_ANYWHERE;

        uint32 p = plr->GetPhaseByAuras();
        return p ? p : PHASEMASK_NORMAL;
    }

    class guild_village_PlayerPhase : public PlayerScript
    {
    public:
        guild_village_PlayerPhase() : PlayerScript("guild_village_PlayerPhase") { }

        void OnPlayerLogin(Player* player) override
        {
            ApplyGVPhaseIfNeeded(player);
        }

        void OnPlayerUpdateZone(Player* player, uint32 newZone, uint32 /*newArea*/) override
        {
            ApplyGVPhaseIfNeeded(player, newZone);
        }

    private:
        void ApplyGVPhaseIfNeeded(Player* player, uint32 knownZone = 0)
        {
            if (!player) return;

            uint32 zone = knownZone ? knownZone : player->GetZoneId();
            // tvoje vesnice: map 37, zone 268 (z .gps)
            bool inVillage = (player->GetMapId() == 37) && (zone == 268);

            auto* stash = player->CustomData.GetDefault<GVPhaseData>("gv_phase");
            if (inVillage)
            {
                if (stash->phaseMask)
                    player->SetPhaseMask(stash->phaseMask, true);
            }
            else
            {
                player->SetPhaseMask(GetNormalPhase(player), true);
            }
        }
    };

    // === Single command handler: ".village [status|teleport]" ===
    static bool HandleVillage(ChatHandler* handler, char const* args)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player) return false;

        std::string a = args ? Trim(args) : std::string();

        // help
        if (a.empty() || Lower(a) == "help" || Lower(a) == "?")
        {
            if (LangOpt() == Lang::EN)
            {
                handler->SendSysMessage("|cff00ff00[Guild Village]|r – commands:");
                handler->SendSysMessage("  .village status   – show your guild materials");
                handler->SendSysMessage("  .village teleport – teleports you to the guild village");
            }
            else
            {
                handler->SendSysMessage("|cff00ff00[Gildovní vesnice]|r – příkazy:");
                handler->SendSysMessage("  .village status   – zobrazí materiály tvojí gildy");
                handler->SendSysMessage("  .village teleport – teleportuje tě do guild vesnice");
            }
            return true;
        }

        // ".village teleport"
        if (Lower(a) == "teleport")
        {
            if (!player->GetGuild())
            {
                handler->SendSysMessage(T("Nejsi v žádné gildě.", "You are not in a guild."));
                return true;
            }

            // načti village vč. phase
            std::string q =
                "SELECT map, positionx, positiony, positionz, orientation, phase "
                "FROM customs.gv_guild WHERE guild=" + std::to_string(player->GetGuildId());

            if (QueryResult res = WorldDatabase.Query(q))
            {
                Field* f = res->Fetch();
                uint32 map = f[0].Get<uint32>();
                double x   = f[1].Get<double>();
                double y   = f[2].Get<double>();
                double z   = f[3].Get<double>();
                float  o   = f[4].Get<float>();
                uint32 phaseMask = f[5].Get<uint32>(); // jednový bit (2..31)

                // stash požadované phase – nasadí se po teleportu v PlayerScriptu
                auto* stash = player->CustomData.GetDefault<GVPhaseData>("gv_phase");
                stash->phaseMask = phaseMask;

                player->TeleportTo(map, x, y, z, o);
                handler->SendSysMessage(T("Teleportuji do guildovní vesnice…", "Teleporting to the guild village…"));
            }
            else
            {
                handler->SendSysMessage(T("Tvá gilda nevlastní guildovní vesnici.",
                                          "Your guild does not own a guild village."));
            }
            return true;
        }

        // ".village status"
        if (Lower(a) == "status")
        {
            if (!player->GetGuild())
            {
                handler->SendSysMessage(T("Nejsi v žádné gildě.", "You are not in a guild."));
                return true;
            }

            if (!CanUseStatus(player))
            {
                handler->SendSysMessage(T("Na tento příkaz nemáš oprávnění.",
                                          "You are not allowed to use this command."));
                return true;
            }

            auto curOpt = LoadGuildCurrency(player->GetGuildId());
            if (!curOpt)
            {
                handler->SendSysMessage(T("Tvá gilda nevlastní guildovní vesnici.",
                                          "Your guild does not own a guild village."));
                return true;
            }

            auto cur = *curOpt;
            auto M = GetMaterialNames();

            handler->SendSysMessage(T("|cff00ff00[Gildovní vesnice]|r – stav prostředků",
                                      "|cff00ff00[Guild Village]|r – currency status"));
            auto sendLine = [&](std::string const& name, uint64 cur, uint32 cap)
			{
				std::string line = "|cff00ffff" + name + ":|r " + std::to_string(cur);
				if (CapsEnabled())
				{
					if (cap == 0) line += " / ∞";
					else          line += " / " + std::to_string(cap);
				}
				handler->SendSysMessage(line.c_str());
			};
			
			sendLine(M.timber,  cur.timber,  CapTimber());
			sendLine(M.stone,   cur.stone,   CapStone());
			sendLine(M.iron,    cur.iron,    CapIron());
			sendLine(M.crystal, cur.crystal, CapCrystal());
            return true;
        }

        // fallback help
        if (LangOpt() == Lang::EN)
        {
            handler->SendSysMessage("|cff00ff00[Guild Village]|r – commands:");
            handler->SendSysMessage("  .village status   – show your guild materials");
            handler->SendSysMessage("  .village teleport – teleports you to the guild village");
        }
        else
        {
            handler->SendSysMessage("|cff00ff00[Gildovní vesnice]|r – příkazy:");
            handler->SendSysMessage("  .village status   – zobrazí materiály tvojí gildy");
            handler->SendSysMessage("  .village teleport – teleportuje tě do guild vesnice");
        }
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

            std::vector<ChatCommandBuilder> out;
            out.emplace_back(village);
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
