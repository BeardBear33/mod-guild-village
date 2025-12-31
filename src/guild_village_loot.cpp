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
#include "ObjectMgr.h"
#include "gv_names.h"

#include <ctime>
#include <unordered_map>
#include <vector>
#include <string>
#include <random>
#include <algorithm>

namespace GuildVillage
{
    // ===== Config: mapa vesnice =====
    static inline uint32 DefMap()
    {
        return sConfigMgr->GetOption<uint32>("GuildVillage.Default.Map", 37);
    }

    // ===== Configy =====
    static bool  CFG_ENABLED         = true;
    static bool  CFG_ONLY_IN_VILLAGE = true;
    static bool  CFG_DEBUG           = false;
    static bool  CFG_NOTIFY          = true;
    static bool  CFG_CAP_ENABLED     = true;
    static uint32 CAP_material1   = 1000;
    static uint32 CAP_material2    = 1000;
    static uint32 CAP_material3     = 1000;
    static uint32 CAP_material4  = 1000;

    // === Lokalizace (cs|en) ===
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
    enum class Cur : uint8 { Material1, Material2, Material3, Material4, Random };

    struct LootRow
    {
        Cur     cur;
        float   chance;
        uint32  minAmount;
        uint32  maxAmount;
    };

    static std::unordered_map<uint32, std::vector<LootRow>> s_loot;

    // --- helpers ---
    static inline bool InVillage(Player* p)
    {
        return p && (p->GetMapId() == DefMap());
    }

	static bool ParseCurrency(std::string s, Cur& out)
	{
		auto ltrim = [](std::string& x){ x.erase(0, x.find_first_not_of(" \t\r\n")); };
		auto rtrim = [](std::string& x){ x.erase(x.find_last_not_of(" \t\r\n") + 1); };
		ltrim(s); rtrim(s);
	
		std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	
		if (s == "random" || s == "all") { out = Cur::Random;     return true; }
		if (s == "material1")            { out = Cur::Material1;  return true; }
		if (s == "material2")            { out = Cur::Material2;  return true; }
		if (s == "material3")            { out = Cur::Material3;  return true; }
		if (s == "material4")            { out = Cur::Material4;  return true; }
	
		return false;
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
					continue;
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
			case Cur::Material1: g.material1 += amount; break;
			case Cur::Material2: g.material2 += amount; break;
			case Cur::Material3: g.material3 += amount; break;
			case Cur::Material4: g.material4 += amount; break;
			case Cur::Random: /* handled elsewhere */   break;
		}
	}

	static void AddRandomSplit(Gain& g, uint32 count)
	{
		if (!count)
			return;
	
		if (count == 1)
		{
			uint32 r = urand(0, 3);
			Cur c = static_cast<Cur>(r);
			AddGain(g, c, 1);
			return;
		}
	
		uint32 k;
		uint32 roll = urand(1, 100);
		if (roll <= 40)
			k = 2;
		else if (roll <= 75)
			k = 3;
		else
			k = 4;
	
		if (count < k)
			k = count;
	
		uint8 idx[4] = {0, 1, 2, 3};
	
		for (uint8 i = 0; i < 4; ++i)
		{
			uint8 j = urand(i, 3);
			std::swap(idx[i], idx[j]);
		}
	
		uint32 tmp[4] = {0, 0, 0, 0};
	
		for (uint32 i = 0; i < k; ++i)
			++tmp[idx[i]];
	
		uint32 remaining = count - k;
	
		while (remaining--)
		{
			uint32 r = urand(0, k - 1);
			++tmp[idx[r]];
		}
	
		AddGain(g, Cur::Material1, tmp[0]);
		AddGain(g, Cur::Material2, tmp[1]);
		AddGain(g, Cur::Material3, tmp[2]);
		AddGain(g, Cur::Material4, tmp[3]);
	}

    static Gain ApplyGainToGuild(uint32 guildId, Gain const& g)
    {
        Gain applied{};

        if (!g.Any())
            return applied;

        if (!CFG_CAP_ENABLED)
        {
            WorldDatabase.DirectExecute(Acore::StringFormat(
                "UPDATE customs.gv_currency SET "
                "material1=material1+{}, material2=material2+{}, material3=material3+{}, material4=material4+{}, last_update=NOW() "
                "WHERE guildId={}", g.material1, g.material2, g.material3, g.material4, guildId).c_str());

            return g;
        }

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
            if (cap == 0) return UINT32_MAX;
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
		if (!CFG_DEBUG || !p)
			return;
	
		if (WorldSession* s = p->GetSession())
			ChatHandler(s).SendSysMessage(("[GV-LOOT] " + msg).c_str());
	}

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
                        if (WorldSession* s = m->GetSession())
							ChatHandler(s).SendSysMessage(msg.c_str());
                    }
                }
            }
        }
        else
        {
            if (WorldSession* s = killer->GetSession())
				ChatHandler(s).SendSysMessage(msg.c_str());
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
		
				if (row.cur == Cur::Random)
				{
					AddRandomSplit(gain, amount);
				}
				else
				{
					AddGain(gain, row.cur, amount);
				}
			}
		}

        if (!gain.Any())
            return;

        Gain applied = ApplyGainToGuild(g->GetId(), gain);

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
			{
				if (WorldSession* s = killer->GetSession())
					ChatHandler(s).SendSysMessage(capMsg.c_str());
			}

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
				msg += "+" + std::to_string(v) + " " + CountName(m, v);
				first = false;
			};
		
			add(Mat::Material1,  applied.material1);
			add(Mat::Material2,   applied.material2);
			add(Mat::Material3,    applied.material3);
			add(Mat::Material4, applied.material4);
		
			BroadcastToGroup(killer, msg);
		}

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
                BroadcastToGroup(killer, capMsg);
        }

		if (CFG_DEBUG)
		{
			using namespace GuildVillage::Names;
		
			std::string msg = T("Zisk: ", "Gain: ");
			bool first = true;
			auto add = [&](Mat m, uint32 v)
			{
				if (!v) return;
				if (!first) msg += ", ";
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

	static inline void CreatureRespawn(Creature* c)
	{
		if (!c)
			return;
	
		if (c->GetMapId() != DefMap())
			return;
	
		if (!c->GetSpawnId())
			return;
	
		if (CreatureData const* data = c->GetCreatureData())
		{
			uint32 delay = data->spawntimesecs;
			c->SetRespawnDelay(delay);
			c->SetRespawnTime(delay + c->GetCorpseDelay());
		}
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
			CreatureRespawn(killed);
        }

        void OnPlayerCreatureKilledByPet(Player* petOwner, Creature* killed) override
        {
            ProcessKill(petOwner, killed);
			CreatureRespawn(killed);
        }
    };
}

// Export
void RegisterGuildVillageLoot()
{
    new GuildVillage::Loot_World();
    new GuildVillage::Loot_Player();
}
