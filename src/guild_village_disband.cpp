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

    // Jednobitová phase per guild (2..31)
    static inline uint32 CalcGuildPhaseMask(uint32 guildId)
    {
        uint32 bitIndex = (guildId % 30) + 1;
        return (1u << bitIndex);
    }

    // Načte phase z customs.gv_guild (pokud existuje)
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
        // Zjistit phase z DB , jinak fallback výpočet
        uint32 phaseMask = LoadGuildPhase(guildId).value_or(CalcGuildPhaseMask(guildId));

        // 1) customs.gv_currency a customs.gv_upgrades podle sloupce guildId
        WorldDatabase.Execute("DELETE FROM customs.gv_currency WHERE guildId={}", guildId);
        WorldDatabase.Execute("DELETE FROM customs.gv_upgrades WHERE guildId={}", guildId);

        // 2) world spawny pro tuto phase 
        WorldDatabase.Execute(
            "DELETE FROM creature WHERE map={} AND phaseMask={}", DefMap(), phaseMask);
        WorldDatabase.Execute(
            "DELETE FROM gameobject WHERE map={} AND phaseMask={}", DefMap(), phaseMask);

        // 3) AŽ NAKONEC smazat z customs.gv_guild (sloupec 'guild')
        WorldDatabase.Execute("DELETE FROM customs.gv_guild WHERE guild={}", guildId);

        LOG_INFO("modules", "GV: Disband cleanup done for guild {} (phaseMask={})", guildId, phaseMask);
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
