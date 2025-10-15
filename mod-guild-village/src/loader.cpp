// modules/mod-guild-village/src/loader.cpp
#include "ScriptMgr.h"

// dopředné deklarace "lokálních" registrátorů z ostatních cpp
void RegisterGuildVillageCommands();
void RegisterGuildVillageCreate();
void RegisterGuildVillageUpgrade();
void RegisterGuildVillageRespawn();
void RegisterGuildVillageLoot();
void RegisterGuildVillageGM();
void RegisterGuildVillageRest();
void RegisterGuildVillageDisband();
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
