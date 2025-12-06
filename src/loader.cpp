// modules/mod-guild-village/src/loader.cpp
#include "ScriptMgr.h"

// dopředné deklarace "lokálních" registrátorů z ostatních cpp
void RegisterGuildVillageCustomsUpdater();
void RegisterGuildVillageCommands();
void RegisterGuildVillageAoe();
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
void RegisterGuildVillageProduction();
void RegisterGuildVillagePvP();
void RegisterGuildVillageExpeditions();
void RegisterGuildVillageExpeditionsMissions();
void RegisterGuildVillageVoltrix();
void RegisterGuildVillageThranok();
void RegisterGuildVillageThalgron();
void RegisterGuildVillageThalor();
void RegisterGuildVillageQuests();
void RegisterGuildVillageQuestsWiring();

// jediný export modulu
void Addmod_guild_villageScripts()
{
	RegisterGuildVillageCustomsUpdater();
	RegisterGuildVillageCommands();
	RegisterGuildVillageAoe();
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
	RegisterGuildVillageProduction();
	RegisterGuildVillagePvP();
	RegisterGuildVillageExpeditions();
	RegisterGuildVillageExpeditionsMissions();
	RegisterGuildVillageVoltrix();
	RegisterGuildVillageThranok();
	RegisterGuildVillageThalgron();
	RegisterGuildVillageThalor();
	RegisterGuildVillageQuests();
	RegisterGuildVillageQuestsWiring();
}
