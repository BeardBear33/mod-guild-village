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

#include <string>
#include <algorithm>
#include <optional>
#include <vector>
#include <cctype>
#include <ctime>

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

    // --- map id vesnice (stejné jako jinde v modu)
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

    // --- najdi GUID spawnu bossa pro danou phase (a mapu vesnice)
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
            return Acore::StringFormat("|cff00ffff{}:|r {}", BossName(b), T("neznámý stav", "unknown"));

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

        // 5) jinak vypiš datum/čas respawnu
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
        return std::nullopt;
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

    // --- Teleport alias helper ---
    static inline bool IsTeleportArg(std::string sLower)
    {
        return sLower == "teleport" || sLower == "tp";
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

        // vesnice: pevně mapa 37 (zone/area může přijít později)
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

    // === Single command handler: ".village …" / ".v …" ===
    static bool HandleVillage(ChatHandler* handler, char const* args)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player) return false;

        std::string a = args ? Trim(args) : std::string();
        std::string al = Lower(a);

        // help
        if (a.empty() || al == "help" || al == "?")
        {
            if (LangOpt() == Lang::EN)
            {
                handler->SendSysMessage("|cff00ff00[Guild Village]|r – commands:");
                handler->SendSysMessage("  .village status      – show village info (materials + bosses)");
                handler->SendSysMessage("  .village teleport    – teleports you to the guild village");
                handler->SendSysMessage("  Aliases: .village tp | .v teleport | .v tp");
            }
            else
            {
                handler->SendSysMessage("|cff00ff00[Gildovní vesnice]|r – příkazy:");
                handler->SendSysMessage("  .village status      – zobrazí informace o vesnici (materiály + bossové)");
                handler->SendSysMessage("  .village teleport    – teleportuje tě do guild vesnice");
                handler->SendSysMessage("  Alias: .village tp | .v teleport | .v tp");
            }
            return true;
        }

        // Teleport aliases
        if (IsTeleportArg(al))
        {
            if (!player->GetGuild())
            {
                handler->SendSysMessage(T("Nejsi v žádné gildě.", "You are not in a guild."));
                return true;
            }

            // ❌ blokace v BG/Aréně
            if (player->InBattleground())
            {
                handler->SendSysMessage(T("V bojišti/aréne nemůžeš teleportovat.",
                                          "You can't teleport while in a battleground/arena."));
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
                uint32 phaseMask = f[5].Get<uint32>(); // přesné ID fáze

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
        if (al == "status")
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

            // --- hlavička + currency
            handler->SendSysMessage(T("|cff00ff00[Gildovní vesnice]|r – informace (materiály + bossové)",
                                      "|cff00ff00[Guild Village]|r – info (materials + bosses)"));
            auto sendLine = [&](std::string const& name, uint64 curVal, uint32 cap)
            {
                std::string line = "|cff00ffff" + name + ":|r " + std::to_string(curVal);
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

            // --- Boss statusy
            handler->SendSysMessage(T("|cff00ff00[Bossové]|r", "|cff00ff00[Bosses]|r"));
            for (BossDef const& b : kBosses)
                handler->SendSysMessage(BossStatusLine(player->GetGuildId(), b).c_str());

            return true;
        }

        // fallback help
        if (LangOpt() == Lang::EN)
        {
            handler->SendSysMessage("|cff00ff00[Guild Village]|r – commands:");
            handler->SendSysMessage("  .village status   – show village info (materials + bosses)");
            handler->SendSysMessage("  .village teleport – teleports you to the guild village");
            handler->SendSysMessage("  Aliases: .village tp | .v teleport | .v tp");
        }
        else
        {
            handler->SendSysMessage("|cff00ff00[Gildovní vesnice]|r – příkazy:");
            handler->SendSysMessage("  .village status   – zobrazí informace o vesnici (materiály + bossové)");
            handler->SendSysMessage("  .village teleport – teleportuje tě do guild vesnice");
            handler->SendSysMessage("  Alias: .village tp | .v teleport | .v tp");
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
