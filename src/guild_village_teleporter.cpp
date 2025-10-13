// modules/mod-guild-village/src/guild_village_teleporter.cpp

#include "ScriptMgr.h"
#include "Player.h"
#include "GameObject.h"
#include "GossipDef.h"
#include "ScriptedGossip.h"
#include "Chat.h"
#include "WorldSession.h"
#include "DatabaseEnv.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "ObjectMgr.h"
#include "Log.h"

#include <vector>
#include <string>
#include <algorithm>

namespace GuildVillage
{
    // ===== Lokalizace (stejně jako v guild_village_upgrade.cpp) =====
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

    // ===== Základní nastavení =====
    static inline uint32 DefMap() { return sConfigMgr->GetOption<uint32>("GuildVillage.Default.Map", 37); }
    static constexpr uint32 GV_TELEPORTER_ENTRY = 990203; // tvůj GO entry
    static constexpr float  kHideIfWithinYards  = 3.0f;   // prah pro skrytí "tohoto" teleporteru

    // Guildovní phaseMask z DB
    static uint32 LoadGuildVillagePhaseMask(uint32 guildId)
    {
        if (!guildId) return 0;
        if (QueryResult r = WorldDatabase.Query("SELECT phase FROM customs.gv_guild WHERE guild={}", guildId))
            return (*r)[0].Get<uint32>();
        return 0;
    }

    // ===== Menu model (bez require_phase/require_key/target_map) =====
    struct TeleRow
    {
        uint32 id;
        std::string label;   // "CS\nEN"
        float  x, y, z, o;
        int    sortIndex;
    };

    static inline void SplitLabels(std::string const& joined, std::string& outCs, std::string& outEn)
    {
        auto pos = joined.find('\n');
        if (pos == std::string::npos) { outCs = joined; outEn = joined; return; }
        outCs = joined.substr(0, pos);
        outEn = joined.substr(pos + 1);
        if (outCs.empty()) outCs = outEn;
        if (outEn.empty()) outEn = outCs;
    }

    static std::vector<TeleRow> LoadAllRowsForEntry(uint32 entry)
    {
        std::vector<TeleRow> v;
        if (QueryResult qr = WorldDatabase.Query(
                "SELECT id, label_cs, label_en, x, y, z, o, sort_index "
                "FROM customs.gv_teleport_menu WHERE teleporter_entry={} ORDER BY sort_index, id",
                entry))
        {
            do
            {
                Field* f = qr->Fetch();
                TeleRow r;
                r.id            = f[0].Get<uint32>();
                std::string cs  = f[1].Get<std::string>();
                std::string enL = f[2].Get<std::string>();
                r.x             = f[3].Get<float>();
                r.y             = f[4].Get<float>();
                r.z             = f[5].Get<float>();
                r.o             = f[6].Get<float>();
                r.sortIndex     = f[7].Get<int>();
                r.label         = cs + "\n" + enL;
                v.emplace_back(std::move(r));
            }
            while (qr->NextRow());
        }
        return v;
    }

    static uint32 Encode(uint32 id) { return id & 0x7FFFFFFF; }
    static uint32 Decode(uint32 a)  { return a & 0x7FFFFFFF; }

    // ===== GameObjectScript =====
    class gv_teleporter_go : public GameObjectScript
    {
    public:
        gv_teleporter_go() : GameObjectScript("gv_teleporter_go") { }

        bool OnGossipHello(Player* player, GameObject* go) override
        {
            if (!player || !go)
                return false;

            Guild* g = player->GetGuild();
            if (!g)
            {
                ChatHandler(player->GetSession()).SendSysMessage(T("Nejsi v gildě.", "You are not in a guild."));
                return true;
            }

            uint32 gvPhase = LoadGuildVillagePhaseMask(g->GetId());
            if (!gvPhase)
            {
                ChatHandler(player->GetSession()).SendSysMessage(T("Tvoje gilda nevlastní vesnici.", "Your guild does not own a village."));
                return true;
            }

            // Link vlastnictví přes phaseMask GO vs. guildy
            if ((go->GetPhaseMask() & gvPhase) == 0)
            {
                ChatHandler(player->GetSession()).SendSysMessage(T("Tento teleporter nepatří tvé gildě.", "This teleporter does not belong to your guild."));
                return true;
            }

            uint32 entry = go->GetEntry();
            if (!entry) entry = GV_TELEPORTER_ENTRY;

            auto rows = LoadAllRowsForEntry(entry);
            if (rows.empty())
            {
                ChatHandler(player->GetSession()).SendSysMessage(T("Žádné cíle nejsou nastavené.", "No destinations configured."));
                return true;
            }

            bool useEN = (LangOpt() == Lang::EN);
            ClearGossipMenuFor(player);

            // pozice aktuálního teleporteru
            float gx = go->GetPositionX();
            float gy = go->GetPositionY();
            float gz = go->GetPositionZ();
            float hide2 = kHideIfWithinYards * kHideIfWithinYards;

            uint32 shown = 0;
            for (auto& r : rows)
            {
                // Skryj položku, která odpovídá "tomuto" teleporteru (podle blízkosti pozice)
                float dx = gx - r.x;
                float dy = gy - r.y;
                float dz = gz - r.z;
                float dist2 = dx*dx + dy*dy + dz*dz;
                if (dist2 < hide2)
                    continue;

                std::string cs, en; SplitLabels(r.label, cs, en);
                std::string label = useEN ? en : cs;
                AddGossipItemFor(player, GOSSIP_ICON_TAXI, label, GOSSIP_SENDER_MAIN, Encode(r.id));
                ++shown;
            }

            if (!shown)
            {
                ChatHandler(player->GetSession()).SendSysMessage(T("Žádné jiné cíle nejsou k dispozici.", "No other destinations available."));
                return true;
            }

            SendGossipMenuFor(player, 1, go->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, GameObject* /*go*/, uint32 /*sender*/, uint32 action) override
        {
            if (!player)
                return false;

            CloseGossipMenuFor(player);

            Guild* g = player->GetGuild();
            if (!g) return true;

            uint32 gvPhase = LoadGuildVillagePhaseMask(g->GetId());
            if (!gvPhase) return true;

            uint32 rowId = Decode(action);
            QueryResult qr = WorldDatabase.Query(
                "SELECT x, y, z, o FROM customs.gv_teleport_menu WHERE id={}", rowId);

            if (!qr)
            {
                ChatHandler(player->GetSession()).PSendSysMessage(T("Cíl už není k dispozici.", "Destination is no longer available."));
                return true;
            }

            Field* f = qr->Fetch();
            float x = f[0].Get<float>();
            float y = f[1].Get<float>();
            float z = f[2].Get<float>();
            float o = f[3].Get<float>();

            // Nastav hráči správnou phase a portni vždy na mapu vesnice
            player->SetPhaseMask(gvPhase, true);
            player->TeleportTo(DefMap(), x, y, z, o);
            return true;
        }
    };
} // namespace GuildVillage

// ===== Registrace v jednotném stylu =====
void RegisterGuildVillageTeleporter()
{
    new GuildVillage::gv_teleporter_go();
}
