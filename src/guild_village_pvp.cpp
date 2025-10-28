// modules/mod-guild-village/src/guild_village_pvp.cpp

#include "ScriptMgr.h"
#include "Player.h"
#include "Guild.h"
#include "Battleground.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "WorldSession.h"
#include "Group.h"
#include "gv_names.h"

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <algorithm>
#include <cmath>

namespace GuildVillage
{
    // =====================================
    // Forward declarations / extern
    // =====================================

    // Implementováno v guild_village_create.cpp
    extern bool GuildHasVillage(uint32 guildId);

    // =====================================
    // Lokální konfig cache pro PvP
    // =====================================

    // Master enable pro celý PvP systém
    static bool   CFG_PVP_ENABLE                = true;

    // Odměny za world PvP honorable kill
    static bool   CFG_PVP_WORLDKILL_ENABLE      = true;

    // Jestli posíláme hráčům hlášky
    static bool   CFG_PVP_NOTIFY                = true;

    // Jestli se v BG win má gildě připsat odměna za KAŽDÉHO jejího člena
    // true  = každému guildí hráči zvlášť (může dostat víc násobků)
    // false = jen jednou za danou guildu per BG instance
    static bool   CFG_PVP_MULTIGUILD_REWARD     = false;

    // WorldPvP reward values (Mat1..4)
    static uint32 CFG_WK_MAT1 = 0;
    static uint32 CFG_WK_MAT2 = 0;
    static uint32 CFG_WK_MAT3 = 0;
    static uint32 CFG_WK_MAT4 = 0;

    // Battleground win reward values per BG typeId
    struct BgReward
    {
        uint32 m1 = 0;
        uint32 m2 = 0;
        uint32 m3 = 0;
        uint32 m4 = 0;
    };

    // Předvyplníme v OnAfterConfigLoad
    // Klíč = BattlegroundTemplate.ID:
    // 1  = Alterac Valley
    // 2  = Warsong Gulch
    // 3  = Arathi Basin
    // 7  = Eye of the Storm
    // 9  = Strand of the Ancients
    // 30 = Isle of Conquest
    // 32 = Random Battleground
    static std::unordered_map<uint32 /*bgId*/, BgReward> s_bgReward;

    // =====================================
    // Shared locale helpers (copy z loot/create stylu)
    // =====================================

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

    // =====================================
    // Gain a ApplyGainToGuild (copy přizpůsobený z guild_village_loot.cpp)
    // =====================================

    // Cap settings (sdílíme stejnou logiku capu jako loot)
    static bool   CFG_CAP_ENABLED     = true;
    static uint32 CAP_material1       = 1000;
    static uint32 CAP_material2       = 1000;
    static uint32 CAP_material3       = 1000;
    static uint32 CAP_material4       = 1000;

    struct Gain
    {
        uint32 material1 = 0;
        uint32 material2 = 0;
        uint32 material3 = 0;
        uint32 material4 = 0;

        bool Any() const { return material1 || material2 || material3 || material4; }
    };

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
                "WHERE guildId={}",
                g.material1, g.material2, g.material3, g.material4, guildId).c_str());

            return g;
        }

        // načtení aktuálního stavu
        uint32 cur1 = 0, cur2 = 0, cur3 = 0, cur4 = 0;
        if (QueryResult q = WorldDatabase.Query(
                "SELECT material1, material2, material3, material4 FROM customs.gv_currency WHERE guildId={}", guildId))
        {
            Field* f = q->Fetch();
            cur1 = f[0].Get<uint32>();
            cur2 = f[1].Get<uint32>();
            cur3 = f[2].Get<uint32>();
            cur4 = f[3].Get<uint32>();
        }
        else
        {
            // pokud neexistuje řádek v gv_currency -> nic nepíšeme
            return applied;
        }

        auto room = [](uint32 cur, uint32 cap)->uint32
        {
            if (cap == 0) return UINT32_MAX; // 0 = bez limitu
            if (cur >= cap) return 0;
            return cap - cur;
        };

        uint32 add1 = std::min(g.material1, room(cur1, CAP_material1));
        uint32 add2 = std::min(g.material2, room(cur2, CAP_material2));
        uint32 add3 = std::min(g.material3, room(cur3, CAP_material3));
        uint32 add4 = std::min(g.material4, room(cur4, CAP_material4));

        if (!(add1 || add2 || add3 || add4))
            return applied;

        WorldDatabase.DirectExecute(Acore::StringFormat(
            "UPDATE customs.gv_currency SET "
            "material1=material1+{}, material2=material2+{}, material3=material3+{}, material4=material4+{}, last_update=NOW() "
            "WHERE guildId={}",
            add1, add2, add3, add4, guildId).c_str());

        applied.material1 = add1;
        applied.material2 = add2;
        applied.material3 = add3;
        applied.material4 = add4;
        return applied;
    }

    // =====================================
    // Broadcast helper (copy z loot.cpp)
    // =====================================

    static void BroadcastToGroup(Player* src, std::string const& msg, float rangeYards = 100.f)
    {
        if (!src)
            return;

        if (Group* grp = src->GetGroup())
        {
            for (GroupReference* itr = grp->GetFirstMember(); itr; itr = itr->next())
            {
                if (Player* m = itr->GetSource())
                {
                    if (m->IsInWorld() && m->GetMapId() == src->GetMapId() &&
                        src->GetDistance(m) <= rangeYards)
                    {
                        ChatHandler(m->GetSession()).SendSysMessage(msg.c_str());
                    }
                }
            }
        }
        else
        {
            ChatHandler(src->GetSession()).SendSysMessage(msg.c_str());
        }
    }

    // =====================================
    // Pomocná funkce: sestavení a poslání hlášky o přídělu měny
    // reasonText není player-facing, je to jen fallback pro debug
    // broadcastGroup = true => BroadcastToGroup(killer, msg)
    // broadcastGroup = false => jen hráči
    // =====================================

    static void AnnounceGain(Player* player, Gain const& applied, Gain const& blocked, bool broadcastGroup)
    {
        if (!player)
            return;

        using namespace GuildVillage::Names;

        // Pokud nic nepřibylo a všechno bylo zablokované capem => řekneme cap info
        if (!applied.Any() && (blocked.material1 || blocked.material2 || blocked.material3 || blocked.material4))
        {
            auto const& N = Names::Get();

            std::string capMsg = std::string("|cffff5555[Guild Village]|r ") +
                T("Limit dosažen: ", "Limit reached: ");
            bool first = true;

            auto addCap = [&](std::string const& label, uint32 capVal)
            {
                if (!first) capMsg += ", ";
                capMsg += label;
                capMsg += T(" (cap ", " (cap ");
                capMsg += std::to_string(capVal);
                capMsg += ")";
                first = false;
            };

            if (blocked.material1) addCap(N.status.material1, CAP_material1);
            if (blocked.material2) addCap(N.status.material2, CAP_material2);
            if (blocked.material3) addCap(N.status.material3, CAP_material3);
            if (blocked.material4) addCap(N.status.material4, CAP_material4);

            if (!first)
            {
                if (broadcastGroup)
                    BroadcastToGroup(player, capMsg);
                else
                    ChatHandler(player->GetSession()).SendSysMessage(capMsg.c_str());
            }

            return;
        }

        // Normální hláška o zisku
        if (applied.Any())
        {
            std::string msg = std::string("|cff00ff00[Guild Village]|r ") +
                T("Získáno: ", "Gained: ");

            bool first = true;
            auto add = [&](Names::Mat m, uint32 v)
            {
                if (!v) return;
                if (!first) msg += ", ";
                msg += "+" + std::to_string(v) + " " + Names::CountName(m, v);
                first = false;
            };

            add(Names::Mat::Material1, applied.material1);
            add(Names::Mat::Material2, applied.material2);
            add(Names::Mat::Material3, applied.material3);
            add(Names::Mat::Material4, applied.material4);

            if (broadcastGroup)
                BroadcastToGroup(player, msg);
            else
                ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
        }

        // Info o tom co neprošlo kvůli capu (jen pokud něco opravdu bylo bloknuté)
        if (blocked.material1 || blocked.material2 || blocked.material3 || blocked.material4)
        {
            auto const& N = Names::Get();

            std::string capMsg = std::string("|cffff5555[Guild Village]|r ") +
                T("Limit – nepřipsáno kvůli capu: ", "Limit – not added due to cap: ");
            bool first = true;
            auto addCut = [&](std::string const& label, uint32 v, uint32 capVal)
            {
                if (!v) return;
                if (!first) capMsg += ", ";
                capMsg += label;
                capMsg += " (";
                capMsg += std::to_string(capVal);
                capMsg += ")";
                first = false;
            };

            if (blocked.material1) addCut(N.status.material1, blocked.material1, CAP_material1);
            if (blocked.material2) addCut(N.status.material2, blocked.material2, CAP_material2);
            if (blocked.material3) addCut(N.status.material3, blocked.material3, CAP_material3);
            if (blocked.material4) addCut(N.status.material4, blocked.material4, CAP_material4);

            if (!first)
            {
                if (broadcastGroup)
                    BroadcastToGroup(player, capMsg);
                else
                    ChatHandler(player->GetSession()).SendSysMessage(capMsg.c_str());
            }
        }
    }

    // =====================================
    // Helper: připsání odměny gildě (Gain -> ApplyGainToGuild -> AnnounceGain)
    // =====================================

    static void GiveGuildVillageReward(Player* player, uint32 guildId, Gain const& gainRaw, bool broadcastGroup)
    {
        if (!player)
            return;
        if (!guildId)
            return;
        if (!gainRaw.Any())
            return;

        // Aplikuj cap / update DB
        Gain applied = ApplyGainToGuild(guildId, gainRaw);

        // Spočti bloknuté množství
        Gain blocked;
        blocked.material1 = (gainRaw.material1 > applied.material1) ? (gainRaw.material1 - applied.material1) : 0;
        blocked.material2 = (gainRaw.material2 > applied.material2) ? (gainRaw.material2 - applied.material2) : 0;
        blocked.material3 = (gainRaw.material3 > applied.material3) ? (gainRaw.material3 - applied.material3) : 0;
        blocked.material4 = (gainRaw.material4 > applied.material4) ? (gainRaw.material4 - applied.material4) : 0;

        if (CFG_PVP_NOTIFY)
            AnnounceGain(player, applied, blocked, broadcastGroup);
    }

    // =====================================
    // WORLD PVP HONOR KILL HOOK
    //
    // Použijeme PlayerScript::OnPlayerVictimRewardAfter
    // Tahle funkce se volá uvnitř Player::RewardHonor() V OKAMŽIKU,
    // kdy engine spočítal honor_f za PvP kill hráče (ne BG objektivy).
    //
    // My chceme:
    //  - Jen pokud honor_f > 0
    //  - Jen mimo BG/areny => killer->GetBattleground() == nullptr
    //  - Killerova guilda má village
    //  - Připsat CFG_WK_MAT1..4 té guildě
    // =====================================

    class PvP_Player : public PlayerScript
    {
    public:
        PvP_Player() : PlayerScript("guild_village_PvP_Player") { }

        void OnPlayerVictimRewardAfter(Player* killer,
                                       Player* victim,
                                       uint32& /*killer_title*/,
                                       uint32& /*victim_rank*/,
                                       float& honor_f) override
        {
            if (!CFG_PVP_ENABLE || !CFG_PVP_WORLDKILL_ENABLE)
                return;

            if (!killer || !victim)
                return;

            // Musí to být skutečný honorable kill (engine už zkontroloval level diff atd.)
            if (honor_f <= 0.0f)
                return;

            // Musí být world PvP, ne BG / ne arena
            if (killer->GetBattleground())
                return;

            // Musí být v guildě
            uint32 guildId = killer->GetGuildId();
            if (!guildId)
                return;

            // Guilda musí mít vesnici
            if (!GuildHasVillage(guildId))
                return;

            // Připrav Gain
            Gain g;
            g.material1 = CFG_WK_MAT1;
            g.material2 = CFG_WK_MAT2;
            g.material3 = CFG_WK_MAT3;
            g.material4 = CFG_WK_MAT4;

            if (!g.Any())
                return;

            // Připíšeme měnu gildě
            // Broadcast ve world PvP dává smysl celé skupině poblíž (stejný styl jako loot)
            GiveGuildVillageReward(killer, guildId, g, /*broadcastGroup=*/true);
        }
    };

    // =====================================
    // BATTLEGROUND WIN HOOK
    //
    // AllBattlegroundScript::OnBattlegroundEndReward(bg, player, winnerTeamId)
    //
    // Volá se pro KAŽDÉHO hráče při konci BG, při udělení BG rewardů.
    //
    // My chceme:
    //  - jen pokud BG není arena
    //  - jen pokud playerova frakce == winnerTeamId  => hráč byl na vítězné straně
    //  - player má guildu a guilda má village
    //  - najdeme bgTypeId (1=AV, 2=WSG, 3=AB, 7=EOTS, 9=SotA, 30=IoC, 32=Random)
    //  - načteme odměnu pro tohle BG ze s_bgReward
    //
    // MultipleSameGuildReward:
    //   - pokud false:
    //        jedna guilda dostane odměnu jen jednou za konkrétní instance ID BG
    //        (tzn. pokud je v BG 5 členů té samé guildy, reward jen 1x)
    //
    //   - pokud true:
    //        pokaždé když hook doběhne pro členy té guildy, gildě se znovu připíše
    //
    // Pozn.: Battleground má instance ID a typ. Použijeme oboje.
    // =====================================

    class PvP_Battleground : public AllBattlegroundScript
    {
    public:
        PvP_Battleground() : AllBattlegroundScript("guild_village_PvP_BG") { }

        void OnBattlegroundEndReward(Battleground* bg, Player* player, TeamId winnerTeamId) override
        {
            if (!CFG_PVP_ENABLE)
                return;
            if (!bg || !player)
                return;

            // Arena? nechceme
            if (bg->isArena())
                return;

            // Musí být na vítězné straně
            if (player->GetTeamId() != winnerTeamId)
                return;

            // Guilda?
            uint32 guildId = player->GetGuildId();
            if (!guildId)
                return;

            // Guilda musí mít vesnici
            if (!GuildHasVillage(guildId))
                return;

            // Zjistit typ BG (ID podle battleground_template.ID)
            // Pozn.: v jádře máš GetBgTypeID(bool GetRandom=false).
            // Pro naše účely chceme "reálný" typ, ne random queue alias.
            uint32 bgTypeId = (uint32)bg->GetBgTypeID(false);

            auto it = s_bgReward.find(bgTypeId);
            if (it == s_bgReward.end())
                return;

            BgReward const& reward = it->second;

            // Pokud žádná odměna nenastavena pro tohle BG, skip
            if (!(reward.m1 || reward.m2 || reward.m3 || reward.m4))
                return;

            // MultipleSameGuildReward = false?
            // Musíme si pohlídat, jestli už tahle guilda za tenhle konkrétní bg instance nedostala.
            static std::unordered_map<uint32 /*instanceId*/, std::unordered_set<uint32 /*guildId*/>> rewardedGuilds;

            if (!CFG_PVP_MULTIGUILD_REWARD)
            {
                uint32 instanceId = bg->GetInstanceID();

                auto& setRef = rewardedGuilds[instanceId];
                if (setRef.find(guildId) != setRef.end())
                {
                    // už jsme tuhle guildu v tomhle BG vyplatili
                    return;
                }

                // Markni jako vyplacenou
                setRef.insert(guildId);
            }

            // Připrav Gain
            Gain g;
            g.material1 = reward.m1;
            g.material2 = reward.m2;
            g.material3 = reward.m3;
            g.material4 = reward.m4;

            if (!g.Any())
                return;

            // Připiš gildě currency
            // V BG kontextu nechceme spamovat půlku raid groupy,
            // takže broadcastGroup = false => jen tomu hráči
            GiveGuildVillageReward(player, guildId, g, /*broadcastGroup=*/false);
        }
    };

    // =====================================
    // CONFIG LOADER
    //
    // Tohle se spustí po načtení configu (stejně jako v guild_village_loot.cpp)
    //
    // =====================================

    static void LoadPvPConfig()
    {
        // Přepínače
        CFG_PVP_ENABLE            = sConfigMgr->GetOption<bool>("GuildVillage.PvP.Enable", true);
        CFG_PVP_WORLDKILL_ENABLE  = sConfigMgr->GetOption<bool>("GuildVillage.PvP.WorldKill.Enable", true);
        CFG_PVP_NOTIFY            = sConfigMgr->GetOption<bool>("GuildVillage.PvP.Notify", true);
        CFG_PVP_MULTIGUILD_REWARD = sConfigMgr->GetOption<bool>("GuildVillage.PvP.MultipleSameGuildReward", false);

        // Cap nastavení sdílíme s lootem => stejné klíče jako v guild_village_loot.cpp
        CFG_CAP_ENABLED = sConfigMgr->GetOption<bool>("GuildVillage.CurrencyCap.Enabled", true);
        CAP_material1   = sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material1",   1000);
        CAP_material2   = sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material2",   1000);
        CAP_material3   = sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material3",   1000);
        CAP_material4   = sConfigMgr->GetOption<uint32>("GuildVillage.CurrencyCap.Material4",   1000);

        // World PvP kill odměna
        CFG_WK_MAT1 = sConfigMgr->GetOption<uint32>("GuildVillage.PvP.WorldKill.Mat1", 0);
        CFG_WK_MAT2 = sConfigMgr->GetOption<uint32>("GuildVillage.PvP.WorldKill.Mat2", 0);
        CFG_WK_MAT3 = sConfigMgr->GetOption<uint32>("GuildVillage.PvP.WorldKill.Mat3", 0);
        CFG_WK_MAT4 = sConfigMgr->GetOption<uint32>("GuildVillage.PvP.WorldKill.Mat4", 0);

        // BG rewards per BG name (matching your request: no BG1.*, just readable names)
        auto loadBG = [](char const* prefixMat, BgReward& out)
        {
            // prefixMat např. "GuildVillage.PvP.AlteracValley"
            std::string p(prefixMat);
            out.m1 = sConfigMgr->GetOption<uint32>((p + ".Mat1").c_str(), 0);
            out.m2 = sConfigMgr->GetOption<uint32>((p + ".Mat2").c_str(), 0);
            out.m3 = sConfigMgr->GetOption<uint32>((p + ".Mat3").c_str(), 0);
            out.m4 = sConfigMgr->GetOption<uint32>((p + ".Mat4").c_str(), 0);
        };

        BgReward av, wsg, ab, eots, sota, ioc, rb;

        loadBG("GuildVillage.PvP.AlteracValley",       av);   // ID 1
        loadBG("GuildVillage.PvP.WarsongGulch",        wsg);  // ID 2
        loadBG("GuildVillage.PvP.ArathiBasin",         ab);   // ID 3
        loadBG("GuildVillage.PvP.EyeOfTheStorm",       eots); // ID 7
        loadBG("GuildVillage.PvP.StrandOfTheAncients", sota); // ID 9
        loadBG("GuildVillage.PvP.IsleOfConquest",      ioc);  // ID 30
        loadBG("GuildVillage.PvP.RandomBattleground",  rb);   // ID 32

        s_bgReward.clear();
        s_bgReward.emplace(1,  av);
        s_bgReward.emplace(2,  wsg);
        s_bgReward.emplace(3,  ab);
        s_bgReward.emplace(7,  eots);
        s_bgReward.emplace(9,  sota);
        s_bgReward.emplace(30, ioc);
        s_bgReward.emplace(32, rb);
    }

    class PvP_World : public WorldScript
    {
    public:
        PvP_World() : WorldScript("guild_village_PvP_World") { }

        void OnAfterConfigLoad(bool /*reload*/) override
        {
            LoadPvPConfig();
        }
    };

} // namespace GuildVillage


// =====================================
// REGISTRACE do loaderu
// =====================================
void RegisterGuildVillagePvP()
{
    new GuildVillage::PvP_World();
    new GuildVillage::PvP_Player();
    new GuildVillage::PvP_Battleground();
}
