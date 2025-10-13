// modules/mod-guild-village/src/loader.cpp
#include "ScriptMgr.h"

// dopředné deklarace "lokálních" registrátorů z ostatních cpp
void RegisterGuildVillageCommands();   // z guild_village_commands.cpp
void RegisterGuildVillageCreate();     // z guild_village_create.cpp
void RegisterGuildVillageUpgrade();    // z guild_village_upgrade.cpp
void RegisterGuildVillageRespawn();    // z guild_village_respawn.cpp
void RegisterGuildVillageLoot();       // z guild_village_loot.cpp
void RegisterGuildVillageGM();         // z guild_village_gm.cpp
void RegisterGuildVillageRest();       // z guild_village_rest.cpp
void RegisterGuildVillageDisband();    // z guild_village_disband.cpp
void RegisterGuildVillageWhere();
void RegisterGuildVillageBot();
void RegisterGuildVillageTeleporter();
void RegisterGuildVillageVoltrix();
void RegisterGuildVillageThranok();
void RegisterGuildVillageThalgron();
void RegisterGuildVillageThalor();

// jediný export modulu
void Addmod_guild_villageScripts()
{
    RegisterGuildVillageCommands();
    RegisterGuildVillageCreate();
    RegisterGuildVillageUpgrade();
    RegisterGuildVillageRespawn();
    RegisterGuildVillageLoot();
    RegisterGuildVillageGM();
	RegisterGuildVillageRest();
	RegisterGuildVillageDisband();
	RegisterGuildVillageWhere();
	RegisterGuildVillageBot();
	RegisterGuildVillageTeleporter();
	RegisterGuildVillageVoltrix();
	RegisterGuildVillageThranok();
	RegisterGuildVillageThalgron();
	RegisterGuildVillageThalor();
}
