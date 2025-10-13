#include "ScriptMgr.h"
#include "Player.h"
#include "Guild.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Chat.h"
#include "Config.h"

#include <unordered_map>
#include <vector>
#include <string>
#include <random>
#include <algorithm>

namespace GuildVillage
{
    // ===== Config flags =====
    static bool  CFG_ENABLED         = true;
    static bool  CFG_ONLY_IN_VILLAGE = true;  // map=37, zone=268
    static bool  CFG_DEBUG           = false;
    static bool  CFG_NOTIFY = true;
    static bool  CFG_CAP_ENABLED = true;
    static uint32 CAP_TIMBER   = 1000;
    static uint32 CAP_STONE    = 1000;
    static uint32 CAP_IRON     = 1000;
    static uint32 CAP_CRYSTAL  = 1000;

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

    // ===== Loot row =====
    enum class Cur : uint8 { Timber, Stone, Iron, Crystal };

    struct LootRow
    {
        Cur     cur;
        float   chance;       // 0..100
        uint32  minAmount;
        uint32  maxAmount;
    };

    // entry -> loot rows
    static std::unordered_map<uint32, std::vector<LootRow>> s_loot;

    // --- helpers ---
    static inline bool InVillage(Player* p)
    {
        return (p->GetMapId() == 37 && p->GetZoneId() == 268);
    }

    static Cur ParseCurrency(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        if (s == "timber"  || s == "drevo" || s == "dřevo") return Cur::Timber;
        if (s == "stone"   || s == "kamen" || s == "kámen") return Cur::Stone;
        if (s == "iron"    || s == "zelezo"|| s == "železo")return Cur::Iron;
        // default -> crystal
        return Cur::Crystal;
    }

    static void LoadLootTable()
    {
        s_loot.clear();

        if (QueryResult res = WorldDatabase.Query(
            "SELECT entry, currency, chance, min_amount, max_amount "
            "FROM customs.gv_loot"))
        {
            do
            {
                Field* f = res->Fetch();
                uint32 entry      = f[0].Get<uint32>();
                std::string curS  = f[1].Get<std::string>();
                float  chance     = f[2].Get<float>();
                uint32 minA       = f[3].Get<uint32>();
                uint32 maxA       = f[4].Get<uint32>();

                if (minA > maxA) std::swap(minA, maxA);
                LootRow row { ParseCurrency(curS), std::max(0.f, std::min(chance, 100.f)), minA, maxA };
                s_loot[entry].push_back(row);
            } while (res->NextRow());
        }
    }

    static void OnConfigLoad()
    {
        CFG_ENABLED         = sConfigMgr->GetOption<bool>("GuildVillage.Loot.Enabled", true);
        CFG_ONLY_IN_VILLAGE = sConfigMgr->GetOption<bool>("GuildVillage.Loot.OnlyInVillage", true);
        CFG_DEBUG           = sConfigMgr->GetOption<bool>("GuildVillage.Loot.Debug", false);
        CFG_NOTIFY = sConfigMgr->GetOption<bool>("GuildVillage.Loot.Notify", true);
        CFG_CAP_ENABLED = sConfigMgr->GetOption<bool>("GuildVillage.CurrencyCap.Enabled", true);
        CAP_TIMBER      = sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Timber",   1000);
        CAP_STONE       = sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Stone",    1000);
        CAP_IRON        = sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Iron",     1000);
        CAP_CRYSTAL     = sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Crystal",  1000);
        LoadLootTable();
    }

    // Náhodné číslo <min;max>
    static uint32 RandInRange(uint32 a, uint32 b)
    {
        if (a == b) return a;
        return urand(a, b);
    }

    struct Gain
    {
        uint32 timber=0, stone=0, iron=0, crystal=0;
        bool Any() const { return timber||stone||iron||crystal; }
    };

    static void AddGain(Gain& g, Cur c, uint32 amount)
    {
        switch (c)
        {
            case Cur::Timber:  g.timber  += amount; break;
            case Cur::Stone:   g.stone   += amount; break;
            case Cur::Iron:    g.iron    += amount; break;
            case Cur::Crystal: g.crystal += amount; break;
        }
    }

    static Gain ApplyGainToGuild(uint32 guildId, Gain const& g)
    {
        Gain applied{}; // co se opravdu připíše

        if (!g.Any())
            return applied;

        if (!CFG_CAP_ENABLED)
        {
            // bez capu -> připíšeme vše
            WorldDatabase.DirectExecute(Acore::StringFormat(
                "UPDATE customs.gv_currency SET "
                "timber=timber+{}, stone=stone+{}, iron=iron+{}, crystal=crystal+{}, last_update=NOW() "
                "WHERE guildId={}", g.timber, g.stone, g.iron, g.crystal, guildId).c_str());

            return g; // připsali jsme přesně to, co přišlo
        }

        // načti aktuální stav
        uint32 curTim=0, curSto=0, curIro=0, curCry=0;
        if (QueryResult q = WorldDatabase.Query(
                "SELECT timber, stone, iron, crystal FROM customs.gv_currency WHERE guildId={}", guildId))
        {
            Field* f = q->Fetch();
            curTim=f[0].Get<uint32>(); curSto=f[1].Get<uint32>();
            curIro=f[2].Get<uint32>(); curCry=f[3].Get<uint32>();
        }
        else
        {
            // řádek by měl existovat po koupi vesnice; pokud ne, radši nic nedělej
            return applied;
        }

        auto room = [](uint32 cur, uint32 cap)->uint32
        {
            if (cap == 0) return UINT32_MAX; // 0 = bez limitu
            if (cur >= cap) return 0;
            return cap - cur;
        };

        uint32 addTim = std::min(g.timber,  room(curTim, CAP_TIMBER));
        uint32 addSto = std::min(g.stone,   room(curSto, CAP_STONE));
        uint32 addIro = std::min(g.iron,    room(curIro, CAP_IRON));
        uint32 addCry = std::min(g.crystal, room(curCry, CAP_CRYSTAL));

        if (!(addTim || addSto || addIro || addCry))
            return applied; // už jsme na capu všude

        WorldDatabase.DirectExecute(Acore::StringFormat(
            "UPDATE customs.gv_currency SET "
            "timber=timber+{}, stone=stone+{}, iron=iron+{}, crystal=crystal+{}, last_update=NOW() "
            "WHERE guildId={}", addTim, addSto, addIro, addCry, guildId).c_str());

        applied.timber  = addTim;
        applied.stone   = addSto;
        applied.iron    = addIro;
        applied.crystal = addCry;
        return applied;
    }

    static void DebugMsg(Player* p, std::string const& msg)
    {
        if (!CFG_DEBUG || !p) return;
        ChatHandler(p->GetSession()).SendSysMessage(("[GV-LOOT] " + msg).c_str());
    }

    static void ProcessKill(Player* killer, Creature* killed)
    {
        if (!CFG_ENABLED || !killer || !killed)
            return;

        if (CFG_ONLY_IN_VILLAGE && !InVillage(killer))
            return;

        Guild* g = killer->GetGuild();
        if (!g)
            return;

        uint32 entry = killed->GetEntry();
        auto it = s_loot.find(entry);
        if (it == s_loot.end())
            return;

        Gain gain;

        for (LootRow const& row : it->second)
        {
            // roll
            float roll = frand(0.f, 100.f);
            if (roll <= row.chance)
            {
                uint32 amount = RandInRange(row.minAmount, row.maxAmount);
                if (amount > 0)
                    AddGain(gain, row.cur, amount);
            }
        }

        if (!gain.Any())
            return;

        Gain applied = ApplyGainToGuild(g->GetId(), gain);

        // Co bylo uříznuto capem (gain - applied)
        Gain blocked{};
        blocked.timber  = (gain.timber  > applied.timber)  ? (gain.timber  - applied.timber)  : 0;
        blocked.stone   = (gain.stone   > applied.stone)   ? (gain.stone   - applied.stone)   : 0;
        blocked.iron    = (gain.iron    > applied.iron)    ? (gain.iron    - applied.iron)    : 0;
        blocked.crystal = (gain.crystal > applied.crystal) ? (gain.crystal - applied.crystal) : 0;

        if (!applied.Any())
        {
            // Nic se nepřipsalo – cap(y) plné. Vypiš co je na maximu.
            auto M = GetMaterialNames();

            std::string capMsg = std::string("|cffff5555[Guild Village]|r ") +
                T("Limit dosažen: ", "Limit reached: ");
            bool first = true;
            auto addCap = [&](std::string const& n, uint32 cap)
            {
                if (!first) capMsg += ", ";
                capMsg += n;
                capMsg += T(" (cap ", " (cap ");
                capMsg += std::to_string(cap);
                capMsg += ")";
                first = false;
            };

            if (blocked.timber)  addCap(M.timber,  CAP_TIMBER);
            if (blocked.stone)   addCap(M.stone,   CAP_STONE);
            if (blocked.iron)    addCap(M.iron,    CAP_IRON);
            if (blocked.crystal) addCap(M.crystal, CAP_CRYSTAL);

            // Pokud by se náhodou nic neblokovalo (teoreticky), tak nepiš nic
            if (first == false)
                ChatHandler(killer->GetSession()).SendSysMessage(capMsg.c_str());

            return;
        }

        if (CFG_NOTIFY)
        {
            auto M = GetMaterialNames();
            std::string msg = std::string("|cff00ff00[Guild Village]|r ") +
                T("Získáno: ", "Gained: ");
            bool first = true;
            auto add = [&](std::string const& n, uint32 v)
            {
                if (!v) return;
                if (!first) msg += ", ";
                msg += n; msg += " +"; msg += std::to_string(v);
                first = false;
            };
            add(M.timber,  applied.timber);
            add(M.stone,   applied.stone);
            add(M.iron,    applied.iron);
            add(M.crystal, applied.crystal);

            ChatHandler(killer->GetSession()).SendSysMessage(msg.c_str());
        }

        // Pokud cap něco ořízl, přidej informativní řádku
        if (blocked.timber || blocked.stone || blocked.iron || blocked.crystal)
        {
            auto M = GetMaterialNames();

            std::string capMsg = std::string("|cffff5555[Guild Village]|r ") +
                T("Limit – nepřipsáno kvůli capu: ", "Limit – not added due to cap: ");
            bool first = true;
            auto addCut = [&](std::string const& n, uint32 v, uint32 cap)
            {
                if (!v) return;
                if (!first) capMsg += ", ";
                capMsg += n; capMsg += " (";
                capMsg += std::to_string(cap); capMsg += ")";
                first = false;
            };

            addCut(M.timber,  blocked.timber,  CAP_TIMBER);
            addCut(M.stone,   blocked.stone,   CAP_STONE);
            addCut(M.iron,    blocked.iron,    CAP_IRON);
            addCut(M.crystal, blocked.crystal, CAP_CRYSTAL);

            if (!first)
                ChatHandler(killer->GetSession()).SendSysMessage(capMsg.c_str());
        }

        // volitelně hláška
        if (CFG_DEBUG)
        {
            std::string msg = "Zisk: ";
            bool first = true;
            auto add = [&](char const* n, uint32 v)
            {
                if (!v) return;
                if (!first) msg += ", ";
                msg += n; msg += " +"; msg += std::to_string(v);
                first = false;
            };
            add("Dřevo",   applied.timber);
            add("Kámen",   applied.stone);
            add("Železo",  applied.iron);
            add("Krystal", applied.crystal);
            DebugMsg(killer, msg);
        }
    }

    static inline void ForceSaveRespawn(Creature* killed)
    {
        if (!killed)
            return;

        // Ujisti se, že je to perzistentní spawn (má spawnId), jinak není co zapisovat
        if (!killed->GetSpawnId())
            return;

        // Pokud je respawn nastaven (1 den, 15 s, apod.), přinuť okamžitý zápis do characters DB
        // SaveRespawnTime() ukládá „čas do respawnu“ (od teď), platí s worldserver.conf: SaveRespawnTimeImmediately = 1
        killed->SaveRespawnTime();
    }

    // ===== Scripts =====

    class Loot_World : public WorldScript
    {
    public:
        Loot_World() : WorldScript("guild_village_Loot_World") { }
        void OnAfterConfigLoad(bool /*reload*/) override { OnConfigLoad(); }
    };

    class Loot_Player : public PlayerScript
    {
    public:
        Loot_Player() : PlayerScript("guild_village_Loot_Player") { }

        void OnPlayerCreatureKill(Player* killer, Creature* killed) override
        {
            ProcessKill(killer, killed);
            if (killed->GetMapId() == 37) ForceSaveRespawn(killed);
            return;
        }

        void OnPlayerCreatureKilledByPet(Player* petOwner, Creature* killed) override
        {
            ProcessKill(petOwner, killed);
            if (killed->GetMapId() == 37) ForceSaveRespawn(killed);
            return;
        }
    };
}

// Export
void RegisterGuildVillageLoot()
{
    new GuildVillage::Loot_World();
    new GuildVillage::Loot_Player();
}
