// modules/mod-guild-village/src/guild_village_bot.cpp

#include "ScriptMgr.h"
#include "Config.h"
#include "Player.h"
#include "Guild.h"
#include "DatabaseEnv.h"
#include "Chat.h"
#include "StringFormat.h"
#include "Log.h"
#include "GameTime.h"

#include <optional>
#include <unordered_map>
#include <cstdint>

namespace GuildVillage
{
    // ------- Konfig: mapa vesnice a "bezpečné" fallback lokace -------
    static inline uint32 DefMap()
    {
        return sConfigMgr->GetOption<uint32>("GuildVillage.Default.Map", 37);
    }

    struct SafeLoc { uint32 map; float x, y, z, o; };

    static SafeLoc SafeAlliance()
    {
        SafeLoc d;
        d.map = sConfigMgr->GetOption<uint32>("GuildVillage.Safe.Alliance.Map", 0);         // Eastern Kingdoms
        d.x   = sConfigMgr->GetOption<float>("GuildVillage.Safe.Alliance.X", -8833.38f);    // Stormwind
        d.y   = sConfigMgr->GetOption<float>("GuildVillage.Safe.Alliance.Y",   628.62f);
        d.z   = sConfigMgr->GetOption<float>("GuildVillage.Safe.Alliance.Z",    94.00f);
        d.o   = sConfigMgr->GetOption<float>("GuildVillage.Safe.Alliance.O",     1.06f);
        return d;
    }

    static SafeLoc SafeHorde()
    {
        SafeLoc d;
        d.map = sConfigMgr->GetOption<uint32>("GuildVillage.Safe.Horde.Map", 1);            // Kalimdor
        d.x   = sConfigMgr->GetOption<float>("GuildVillage.Safe.Horde.X",   1629.85f);      // Orgrimmar
        d.y   = sConfigMgr->GetOption<float>("GuildVillage.Safe.Horde.Y",  -4373.64f);
        d.z   = sConfigMgr->GetOption<float>("GuildVillage.Safe.Horde.Z",    31.55f);
        d.o   = sConfigMgr->GetOption<float>("GuildVillage.Safe.Horde.O",     3.69f);
        return d;
    }

    // ------- Helpery -------
    static inline bool IsGMBypass(Player* p) { return p && p->IsGameMaster(); }
    static inline bool IsInVillageMap(Player* p) { return p && p->GetMapId() == DefMap(); }

    // Má hráč guildu, která má záznam v customs.gv_guild?
    static bool HasGuildVillage(Player* p)
    {
        if (!p) return false;
        Guild* g = p->GetGuild();
        if (!g) return false;

        if (QueryResult r = WorldDatabase.Query("SELECT 1 FROM customs.gv_guild WHERE guild={}", g->GetId()))
            return true;
        return false;
    }

    static void TeleportToSafeCity(Player* p)
    {
        if (!p) return;
        SafeLoc loc = (p->GetTeamId() == TEAM_ALLIANCE) ? SafeAlliance() : SafeHorde();
        p->TeleportTo(loc.map, loc.x, loc.y, loc.z, loc.o);
    }

    static void Enforce(Player* p)
    {
        if (!p || IsGMBypass(p))
            return;

        // Cizinec (nebo hráč bez guildy) nemá co dělat v mapě vesnice
        if (IsInVillageMap(p) && !HasGuildVillage(p))
            TeleportToSafeCity(p);
    }

    class guild_village_BotGuard : public PlayerScript
    {
    public:
        guild_village_BotGuard() : PlayerScript("guild_village_BotGuard") { }

        // Před jakýmkoli teleportem – bloknout cílovou vesnici cizincům
        [[nodiscard]] bool OnPlayerBeforeTeleport(Player* player, uint32 mapId, float /*x*/, float /*y*/, float /*z*/, float /*orientation*/, uint32 /*options*/, Unit* /*target*/) override
        {
            if (!player || IsGMBypass(player))
                return true;

            if (mapId == DefMap() && !HasGuildVillage(player))
            {
                TeleportToSafeCity(player);
                return false; // zrušit původní teleport do vesnice
            }
            return true;
        }

		void OnPlayerLogin(Player* player) override
		{
			if (!player || IsGMBypass(player)) return;
			Enforce(player); // pokud se probudil rovnou ve vesnici a není „oprávněný“, poslat pryč
		}
		
		void OnPlayerMapChanged(Player* player) override
		{
			if (!player || IsGMBypass(player)) return;
			Enforce(player); // po jakémkoli teleportu na jinou mapu
		}
		
		void OnPlayerUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override
		{
			if (!player || IsGMBypass(player)) return;
			Enforce(player); // přesun v rámci mapy mezi zónami
		}
    };
} // namespace GuildVillage

// ------- Registrace pro loader.cpp -------
void RegisterGuildVillageBot()
{
    new GuildVillage::guild_village_BotGuard();
}
