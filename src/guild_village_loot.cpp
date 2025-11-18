// modules/mod-guild-village/src/guild_village_loot.cpp

#include "ScriptMgr.h"
#include "Player.h"
#include "Guild.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Chat.h"
#include "Config.h"
#include "Group.h"
#include "Log.h"
#include "gv_names.h"

#include <unordered_map>
#include <vector>
#include <string>
#include <random>
#include <algorithm>

namespace GuildVillage
{
    // ===== Config: mapa vesnice (sjednoceno s ostatními soubory) =====
    static inline uint32 DefMap()
    {
        return sConfigMgr->GetOption<uint32>("GuildVillage.Default.Map", 37);
    }

    // ===== Config flags =====
    static bool  CFG_ENABLED         = true;
    static bool  CFG_ONLY_IN_VILLAGE = true;   // omezit drop jen na mapu vesnice
    static bool  CFG_DEBUG           = false;
    static bool  CFG_NOTIFY          = true;
    static bool  CFG_CAP_ENABLED     = true;
    static uint32 CAP_material1   = 1000;
    static uint32 CAP_material2    = 1000;
    static uint32 CAP_material3     = 1000;
    static uint32 CAP_material4  = 1000;

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

    // ===== Loot row =====
    enum class Cur : uint8 { Material1, Material2, Material3, Material4, All };

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
        return p && (p->GetMapId() == DefMap());
    }

	static bool ParseCurrency(std::string s, Cur& out)
	{
		std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	
		if (s == "material1"  || s == "material1"  || s == "material1")  { out = Cur::Material1;  return true; }
		if (s == "material2"   || s == "material2"  || s == "material2")  { out = Cur::Material2;   return true; }
		if (s == "material3"    || s == "material3" || s == "material3") { out = Cur::Material3;    return true; }
		if (s == "material4" || s == "material4")                  { out = Cur::Material4; return true; }
		if (s == "all")       { out = Cur::All;       return true; }
	
		return false; // neznámá měna
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
	
				if (minA > maxA)
					std::swap(minA, maxA);
	
				Cur cur;
				if (!ParseCurrency(curS, cur))
				{
					LOG_WARN("guildvillage", "Unknown currency '%s' in customs.gv_loot (entry %u) — skipping.", curS.c_str(), entry);
					continue; // přeskočí neznámou měnu
				}
	
				LootRow row { cur, std::max(0.f, std::min(chance, 100.f)), minA, maxA };
				s_loot[entry].push_back(row);
			} while (res->NextRow());
		}
	}

    static void OnConfigLoad()
    {
        CFG_ENABLED         = sConfigMgr->GetOption<bool>("GuildVillage.Loot.Enabled", true);
        CFG_ONLY_IN_VILLAGE = sConfigMgr->GetOption<bool>("GuildVillage.Loot.OnlyInVillage", true);
        CFG_DEBUG           = sConfigMgr->GetOption<bool>("GuildVillage.Loot.Debug", false);
        CFG_NOTIFY          = sConfigMgr->GetOption<bool>("GuildVillage.Loot.Notify", true);
        CFG_CAP_ENABLED     = sConfigMgr->GetOption<bool>("GuildVillage.CurrencyCap.Enabled", true);
        CAP_material1          = sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material1",   1000);
        CAP_material2           = sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material2",    1000);
        CAP_material3            = sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material3",     1000);
        CAP_material4         = sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material4",  1000);
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
        uint32 material1=0, material2=0, material3=0, material4=0;
        bool Any() const { return material1||material2||material3||material4; }
    };

    static void AddGain(Gain& g, Cur c, uint32 amount)
    {
        switch (c)
        {
            case Cur::Material1:  g.material1  += amount; break;
            case Cur::Material2:   g.material2   += amount; break;
            case Cur::Material3:    g.material3    += amount; break;
            case Cur::Material4: g.material4 += amount; break;
        }
    }

    static Gain ApplyGainToGuild(uint32 guildId, Gain const& g)
    {
        Gain applied{};

        if (!g.Any())
            return applied;

        if (!CFG_CAP_ENABLED)
        {
            // bez capu
            WorldDatabase.DirectExecute(Acore::StringFormat(
                "UPDATE customs.gv_currency SET "
                "material1=material1+{}, material2=material2+{}, material3=material3+{}, material4=material4+{}, last_update=NOW() "
                "WHERE guildId={}", g.material1, g.material2, g.material3, g.material4, guildId).c_str());

            return g;
        }

        // načíst aktuální stav
        uint32 curmat1=0, curmat2=0, curmat3=0, curmat4=0;
        if (QueryResult q = WorldDatabase.Query(
                "SELECT material1, material2, material3, material4 FROM customs.gv_currency WHERE guildId={}", guildId))
        {
            Field* f = q->Fetch();
            curmat1=f[0].Get<uint32>(); curmat2=f[1].Get<uint32>();
            curmat3=f[2].Get<uint32>(); curmat4=f[3].Get<uint32>();
        }
        else
        {
            return applied;
        }

        auto room = [](uint32 cur, uint32 cap)->uint32
        {
            if (cap == 0) return UINT32_MAX; // 0 = bez limitu
            if (cur >= cap) return 0;
            return cap - cur;
        };

        uint32 addmat1 = std::min(g.material1,  room(curmat1, CAP_material1));
        uint32 addmat2 = std::min(g.material2,   room(curmat2, CAP_material2));
        uint32 addmat3 = std::min(g.material3,    room(curmat3, CAP_material3));
        uint32 addmat4 = std::min(g.material4, room(curmat4, CAP_material4));

        if (!(addmat1 || addmat2 || addmat3 || addmat4))
            return applied;

        WorldDatabase.DirectExecute(Acore::StringFormat(
            "UPDATE customs.gv_currency SET "
            "material1=material1+{}, material2=material2+{}, material3=material3+{}, material4=material4+{}, last_update=NOW() "
            "WHERE guildId={}", addmat1, addmat2, addmat3, addmat4, guildId).c_str());

        applied.material1  = addmat1;
        applied.material2   = addmat2;
        applied.material3    = addmat3;
        applied.material4 = addmat4;
        return applied;
    }

    static void DebugMsg(Player* p, std::string const& msg)
    {
        if (!CFG_DEBUG || !p) return;
        ChatHandler(p->GetSession()).SendSysMessage(("[GV-LOOT] " + msg).c_str());
    }

    // === Broadcast helper: poslat zprávu celé party/raidu poblíž killer-a (default 100 yd) ===
    static void BroadcastToGroup(Player* killer, std::string const& msg, float rangeYards = 100.f)
    {
        if (!killer) return;

        if (Group* grp = killer->GetGroup())
        {
            for (GroupReference* itr = grp->GetFirstMember(); itr; itr = itr->next())
            {
                if (Player* m = itr->GetSource())
                {
                    if (m->IsInWorld() && m->GetMapId() == killer->GetMapId() &&
                        killer->GetDistance(m) <= rangeYards)
                    {
                        ChatHandler(m->GetSession()).SendSysMessage(msg.c_str());
                    }
                }
            }
        }
        else
        {
            ChatHandler(killer->GetSession()).SendSysMessage(msg.c_str());
        }
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
			float roll = frand(0.f, 100.f);
			if (roll <= row.chance)
			{
				uint32 amount = RandInRange(row.minAmount, row.maxAmount);
				if (!amount)
					continue;
		
				Cur dropCur = row.cur;
				if (dropCur == Cur::All)
				{
					// rovnoměrně vyber jeden z 4 materiálů
					uint32 r = urand(0, 3); // 0..3 odpovídá pořadí v enumu
					dropCur = static_cast<Cur>(r);
				}
		
				AddGain(gain, dropCur, amount);
			}
		}

        if (!gain.Any())
            return;

        Gain applied = ApplyGainToGuild(g->GetId(), gain);

        // Co bylo uříznuto capem (gain - applied)
        Gain blocked{};
        blocked.material1  = (gain.material1  > applied.material1)  ? (gain.material1  - applied.material1)  : 0;
        blocked.material2   = (gain.material2   > applied.material2)   ? (gain.material2   - applied.material2)   : 0;
        blocked.material3    = (gain.material3    > applied.material3)    ? (gain.material3    - applied.material3)    : 0;
        blocked.material4 = (gain.material4 > applied.material4) ? (gain.material4 - applied.material4) : 0;

        if (!applied.Any())
        {
            auto const& N = GuildVillage::Names::Get();

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

            if (blocked.material1)  addCap(N.status.material1,  CAP_material1);
            if (blocked.material2)   addCap(N.status.material2,   CAP_material2);
            if (blocked.material3)    addCap(N.status.material3,    CAP_material3);
            if (blocked.material4) addCap(N.status.material4, CAP_material4);

            if (first == false)
                ChatHandler(killer->GetSession()).SendSysMessage(capMsg.c_str());

            return;
        }

        if (CFG_NOTIFY)
		{
			using namespace GuildVillage::Names;
		
			std::string msg = std::string("|cff00ff00[Guild Village]|r ") +
				T("Získáno: ", "Gained: ");
		
			bool first = true;
			auto add = [&](Mat m, uint32 v)
			{
				if (!v) return;
				if (!first) msg += ", ";
				// "+3 prkna" / "+3 material1s"
				msg += "+" + std::to_string(v) + " " + CountName(m, v);
				first = false;
			};
		
			add(Mat::Material1,  applied.material1);
			add(Mat::Material2,   applied.material2);
			add(Mat::Material3,    applied.material3);
			add(Mat::Material4, applied.material4);
		
			BroadcastToGroup(killer, msg);
		}

        // Pokud cap něco ořízl
        if (blocked.material1 || blocked.material2 || blocked.material3 || blocked.material4)
        {
            auto const& N = GuildVillage::Names::Get();

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

            addCut(N.status.material1,  blocked.material1,  CAP_material1);
            addCut(N.status.material2,   blocked.material2,   CAP_material2);
            addCut(N.status.material3,    blocked.material3,    CAP_material3);
            addCut(N.status.material4, blocked.material4, CAP_material4);

            if (!first)
                // původně: ChatHandler(killer->GetSession()).SendSysMessage(capMsg.c_str());
                BroadcastToGroup(killer, capMsg);
        }

		// volitelně hláška – respektuje názvy z configu
		if (CFG_DEBUG)
		{
			using namespace GuildVillage::Names;
		
			std::string msg = T("Zisk: ", "Gain: ");
			bool first = true;
			auto add = [&](Mat m, uint32 v)
			{
				if (!v) return;
				if (!first) msg += ", ";
				// "+3 prkna" / "+3 material1s"
				msg += "+" + std::to_string(v) + " " + CountName(m, v);
				first = false;
			};
		
			add(Mat::Material1,  applied.material1);
			add(Mat::Material2,   applied.material2);
			add(Mat::Material3,    applied.material3);
			add(Mat::Material4, applied.material4);
		
			DebugMsg(killer, msg);
		}
    }

    static inline void ForceSaveRespawn(Creature* killed)
    {
        if (!killed)
            return;

        if (!killed->GetSpawnId())
            return;

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
            if (killed->GetMapId() == DefMap()) ForceSaveRespawn(killed);
        }

        void OnPlayerCreatureKilledByPet(Player* petOwner, Creature* killed) override
        {
            ProcessKill(petOwner, killed);
            if (killed->GetMapId() == DefMap()) ForceSaveRespawn(killed);
        }
    };
}

// Export
void RegisterGuildVillageLoot()
{
    new GuildVillage::Loot_World();
    new GuildVillage::Loot_Player();
}
