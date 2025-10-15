// modules/mod-guild-village/src/guild_village_disband.cpp

#include "ScriptMgr.h"
#include "Guild.h"
#include "DatabaseEnv.h"
#include "Config.h"
#include "Log.h"
#include <optional>
#include <string>

namespace GuildVillage
{
    static inline uint32 DefMap()
    {
        return sConfigMgr->GetOption<uint32>("GuildVillage.Default.Map", 37);
    }

    static inline uint32 PhaseIdForGuild(uint32 guildId)
    {
        return guildId + 10;
    }

    static std::optional<uint32> LoadGuildPhase(uint32 guildId)
    {
        if (QueryResult r = WorldDatabase.Query(
                "SELECT phase FROM customs.gv_guild WHERE guild={}", guildId))
            return (*r)[0].Get<uint32>();
        return std::nullopt;
    }

    // Hlavní mazání pro danou guildu
    static void WipeGuildVillage(uint32 guildId)
    {
        // Zjistit phase z DB (preferovaná), jinak fallback na přesné ID z guildId
        uint32 phaseId = LoadGuildPhase(guildId).value_or(PhaseIdForGuild(guildId));

        // 1) customs: měny a upgrady
        WorldDatabase.Execute("DELETE FROM customs.gv_currency WHERE guildId={}", guildId);
        WorldDatabase.Execute("DELETE FROM customs.gv_upgrades WHERE guildId={}", guildId);

        // 2) world spawny pro danou phase na mapě vesnice
        WorldDatabase.Execute(
            "DELETE FROM creature WHERE map={} AND phaseMask={}", DefMap(), phaseId);
        WorldDatabase.Execute(
            "DELETE FROM gameobject WHERE map={} AND phaseMask={}", DefMap(), phaseId);

        // 3) AŽ NAKONEC záznam o vesnici
        WorldDatabase.Execute("DELETE FROM customs.gv_guild WHERE guild={}", guildId);

        LOG_INFO("modules", "GV: Disband cleanup done for guild {} (phaseId={})", guildId, phaseId);
    }

    class guild_village_Disband : public GuildScript
    {
    public:
        guild_village_Disband() : GuildScript("guild_village_Disband") { }

        void OnDisband(Guild* guild) override
        {
            if (!guild)
                return;

            WipeGuildVillage(guild->GetId());
        }
    };
} // namespace GuildVillage

// ---------- Registrace ----------
void RegisterGuildVillageDisband()
{
    new GuildVillage::guild_village_Disband();
}
