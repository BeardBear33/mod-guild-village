// modules/mod-guild-village/src/guild_village_disband.cpp

#include "ScriptMgr.h"
#include "Guild.h"
#include "DatabaseEnv.h"
#include "Config.h"
#include "Log.h"

#include <optional>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>

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

    // --- Pomůcka: dávkové DELETE v characters DB podle GUIDů -------------
    static void DeleteByGuidBatches(std::string const& table, std::vector<uint32> const& guids, size_t batch = 500)
    {
        if (guids.empty())
            return;

        for (size_t i = 0; i < guids.size(); i += batch)
        {
            size_t j = std::min(i + batch, guids.size());
            std::ostringstream inlist;
            for (size_t k = i; k < j; ++k)
            {
                if (k != i) inlist << ',';
                inlist << guids[k];
            }

            // složit hotový SQL string pro pool (žádné {} placeholdery)
            std::string sql = "DELETE FROM " + table + " WHERE guid IN (" + inlist.str() + ")";
            CharacterDatabase.Execute(sql);
        }
    }

    // Hlavní mazání pro danou guildu
    static void WipeGuildVillage(uint32 guildId)
    {
        // Zjistit phase z DB (preferovaná), jinak fallback na přesné ID z guildId
        uint32 phaseId = LoadGuildPhase(guildId).value_or(PhaseIdForGuild(guildId));

        // 1) customs: měny a upgrady
        WorldDatabase.Execute(
			"DELETE FROM customs.gv_currency WHERE guildId={}",
			guildId
		);
			
        WorldDatabase.Execute(
			"DELETE FROM customs.gv_upgrades WHERE guildId={}",
			guildId
		);
		
		// 1.1) expedice: aktivní mise, loot, stav expedic pro guildu
		WorldDatabase.Execute(
			"DELETE FROM customs.gv_expedition_active WHERE guildId={}",
			guildId
		);
	
		WorldDatabase.Execute(
			"DELETE FROM customs.gv_expedition_loot WHERE guildId={}",
			guildId
		);
	
		WorldDatabase.Execute(
			"DELETE FROM customs.gv_expedition_guild WHERE guildId={}",
			guildId
		);
		
		// 1.2) Vyčistit teleportační bod
		WorldDatabase.Execute(
			"DELETE FROM customs.gv_teleport_player WHERE guild={}",
			guildId
		);
		
		// 1.3) Vyčistit guild questy
		WorldDatabase.Execute(
			"DELETE FROM customs.gv_guild_quests WHERE guildId={}",
			guildId
		);

        // 1.5) Vyčistit respawny v characters.* pro GUIDy této phase
        {
            std::vector<uint32> creatureGuids;
            if (QueryResult qc = WorldDatabase.Query(
                    "SELECT guid FROM creature WHERE map={} AND phaseMask={}", DefMap(), phaseId))
            {
                do { creatureGuids.emplace_back(qc->Fetch()[0].Get<uint32>()); }
                while (qc->NextRow());
            }

            std::vector<uint32> goGuids;
            if (QueryResult qg = WorldDatabase.Query(
                    "SELECT guid FROM gameobject WHERE map={} AND phaseMask={}", DefMap(), phaseId))
            {
                do { goGuids.emplace_back(qg->Fetch()[0].Get<uint32>()); }
                while (qg->NextRow());
            }

            DeleteByGuidBatches("creature_respawn", creatureGuids);
            DeleteByGuidBatches("gameobject_respawn", goGuids);

            LOG_INFO("modules", "GV: Cleared respawns for guild {} (phaseId={}, map={}, creatures={}, gos={})",
                     guildId, phaseId, DefMap(), creatureGuids.size(), goGuids.size());
        }
		
        // 2) world spawny pro danou phase na mapě vesnice
        WorldDatabase.Execute(
            "DELETE FROM creature WHERE map={} AND phaseMask={}", DefMap(), phaseId);
        WorldDatabase.Execute(
            "DELETE FROM gameobject WHERE map={} AND phaseMask={}", DefMap(), phaseId);

		// 2.5) produkce (aktivní sloty + koupené ranky produkce)
		WorldDatabase.Execute(
			"DELETE FROM customs.gv_production_active WHERE guildId={}", guildId);
		
		WorldDatabase.Execute(
			"DELETE FROM customs.gv_production_upgrade WHERE guildId={}", guildId);

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
