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
    static void SendProductionBlock(Player* player,
                                    ChatHandler* handler,
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
            default: matName = "Unknown"; break;
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
                handler->SendSysMessage("|cff00ff00[Guild Village]|r – commands:");
                handler->SendSysMessage(" .village info – show village info (all)");
                handler->SendSysMessage("    Aliases: .village i / in / inf / info");
                handler->SendSysMessage("              .v i / in / inf / info");

                handler->SendSysMessage(" .village expedition – show active expeditions");
                handler->SendSysMessage("    Aliases: .village e / exp / expedition");
                handler->SendSysMessage("              .v e / exp / expedition");

                handler->SendSysMessage(" .village boss – show bosses status");
                handler->SendSysMessage("    Aliases: .village b / boss");
                handler->SendSysMessage("              .v b / boss");

                handler->SendSysMessage(" .village currency – show materials & caps");
                handler->SendSysMessage("    Aliases: .village c / currency");
                handler->SendSysMessage("              .v c / currency");

                handler->SendSysMessage(" .village production – show production status");
                handler->SendSysMessage("    Aliases: .village p / production");
                handler->SendSysMessage("              .v p / production");

                handler->SendSysMessage(" .village teleport – teleports you to the guild village");
                handler->SendSysMessage("    Aliases: .village tp / teleport");
                handler->SendSysMessage("              .v tp / teleport");
            }
            else
            {
                handler->SendSysMessage("|cff00ff00[Guildovní vesnice]|r – příkazy:");
                handler->SendSysMessage(" .village info – zobrazí kompletní info");
                handler->SendSysMessage("    Alias: .village i / in / inf / info");
                handler->SendSysMessage("            .v i / in / inf / info");

                handler->SendSysMessage(" .village expedition – ukáže probíhající expedice");
                handler->SendSysMessage("    Alias: .village e / exp / expedition");
                handler->SendSysMessage("            .v e / exp / expedition");

                handler->SendSysMessage(" .village boss – zobrazí stav bossů");
                handler->SendSysMessage("    Alias: .village b / boss");
                handler->SendSysMessage("            .v b / boss");

                handler->SendSysMessage(" .village currency – zobrazí suroviny a cap");
                handler->SendSysMessage("    Alias: .village c / currency");
                handler->SendSysMessage("            .v c / currency");

                handler->SendSysMessage(" .village production – zobrazí produkci");
                handler->SendSysMessage("    Alias: .village p / production");
                handler->SendSysMessage("            .v p / production");

                handler->SendSysMessage(" .village teleport – teleportuje tě do guild vesnice");
                handler->SendSysMessage("    Alias: .village tp / teleport");
                handler->SendSysMessage("            .v tp / teleport");
            }
            return true;
        }

        // --- TELEPORT ---
        if (IsTeleportArg(al))
        {
            if (!player->GetGuild())
            {
                handler->SendSysMessage(T("Nejsi v žádné guildě.", "You are not in a guild."));
                return true;
            }

            if (player->InBattleground())
            {
                handler->SendSysMessage(T("V bojišti/aréne nemůžeš teleportovat.",
                                          "You can't teleport while in a battleground/arena."));
                return true;
            }

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
                uint32 phaseMask = f[5].Get<uint32>();

                auto* stash = player->CustomData.GetDefault<GVPhaseData>("gv_phase");
                stash->phaseMask = phaseMask;

                player->TeleportTo(map, x, y, z, o);
                handler->SendSysMessage(T("Teleportuji do guildovní vesnice…", "Teleporting to the guild village…"));
            }
            else
            {
                handler->SendSysMessage(T("Tvá guilda nevlastní guildovní vesnici.",
                                          "Your guild does not own a guild village."));
            }
            return true;
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
