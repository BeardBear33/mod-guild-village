// modules/mod-guild-village/src/guild_village_respawn.cpp

#include "ScriptMgr.h"
#include "Player.h"
#include "Guild.h"
#include "DatabaseEnv.h"
#include "DataMap.h"
#include "Chat.h"

// ==== Shared per-player stash (stejné jméno/typ jako v commands.cpp) ====
struct GVPhaseData : public DataMap::Base
{
    uint32 phaseMask = 0;     // cílová phase guildy (jednobitová maska)
};

struct GVRepopState : public DataMap::Base
{
    bool handledVillageRepop = false;
};

// ==== DB helpers ====
static bool LoadVillageRow(uint32 guildId, uint32& map, float& x, float& y, float& z, float& o, uint32& phaseMask)
{
    if (QueryResult res = WorldDatabase.Query(
        "SELECT map, positionx, positiony, positionz, orientation, phase "
        "FROM customs.gv_guild WHERE guild={}", guildId))
    {
        Field* f = res->Fetch();
        map       = f[0].Get<uint32>();
        x         = f[1].Get<float>();
        y         = f[2].Get<float>();
        z         = f[3].Get<float>();
        o         = f[4].Get<float>();
        phaseMask = f[5].Get<uint32>();
        return true;
    }
    return false;
}

static bool FindSpiritHealerPos(uint32 map, uint32 phaseMask, float& x, float& y, float& z, float& o)
{
    if (QueryResult res = WorldDatabase.Query(
        "SELECT position_x, position_y, position_z, orientation "
        "FROM creature WHERE id1=6491 AND map={} AND phaseMask={} LIMIT 1",
        map, phaseMask))
    {
        Field* f = res->Fetch();
        x = f[0].Get<float>();
        y = f[1].Get<float>();
        z = f[2].Get<float>();
        o = f[3].Get<float>();
        return true;
    }
    return false;
}

// ==== PlayerScript ====
class guild_village_Respawn : public PlayerScript
{
public:
    guild_village_Respawn() : PlayerScript("guild_village_Respawn") { }

    // Hráč stiskl "Release Spirit" -> stal se duchem.
    void OnPlayerReleasedGhost(Player* player) override
    {
        if (!player)
            return;

        Guild* g = player->GetGuild();
        if (!g)
            return;

        if (!(player->GetMapId() == 37 && player->GetZoneId() == 268))
            return;

        uint32 map = 0, phaseMask = 0;
        float baseX=0, baseY=0, baseZ=0, baseO=0;
        if (!LoadVillageRow(g->GetId(), map, baseX, baseY, baseZ, baseO, phaseMask))
            return;

        player->CustomData.GetDefault<GVPhaseData>("gv_phase")->phaseMask = phaseMask;

        float sx=baseX, sy=baseY, sz=baseZ, so=baseO;
        (void)FindSpiritHealerPos(map, phaseMask, sx, sy, sz, so);

        player->CustomData.GetDefault<GVRepopState>("gv_repop")->handledVillageRepop = true;

        player->TeleportTo(map, sx, sy, sz, so);

        ChatHandler(player->GetSession()).SendSysMessage(
            "|cff00ff00[Guild Village]|r Duch vzkříšení tě přivolal do tvojí guildovní vesnice…");
    }

    [[nodiscard]] bool OnPlayerCanRepopAtGraveyard(Player* player) override
    {
        if (!player)
            return true;

        auto* st = player->CustomData.GetDefault<GVRepopState>("gv_repop");
        if (st->handledVillageRepop)
        {
            st->handledVillageRepop = false;
            return false;
        }

        return true;
    }
};

// ==== Export registrace z tohoto .cpp ====
void RegisterGuildVillageRespawn()
{
    new guild_village_Respawn();
}
