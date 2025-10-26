#ifndef GUILD_VILLAGE_PRODUCTION_API_H
#define GUILD_VILLAGE_PRODUCTION_API_H

#include "Define.h"
#include <optional>
#include <cstdint>

namespace GuildVillageProduction
{
    struct GuildCurrency
    {
        uint64 material1 = 0;
        uint64 material2 = 0;
        uint64 material3 = 0;
        uint64 material4 = 0;
    };

    // Vrátí finální stav měny po nasyncování produkce.
    // Pokud guilda nemá záznam v gv_currency (nemá vesnici), vrátí std::nullopt.
    std::optional<GuildCurrency> SyncGuildProduction(uint32 guildId);
	
	struct ProdStatusForMat
    {
        bool   active;        // true = guild aktuálně vyrábí tenhle materiál
        uint32 amountPerTick; // kolik kusů přidá jeden tick
        float  hoursPerTick;  // jak často tick (v hodinách)
    };

    // vrátí stav pro konkrétní materialId (1..4)
    ProdStatusForMat GetProductionStatus(uint32 guildId, uint8 materialId);
	
	// vrátí materialId, který se právě vyrábí (pokud něco jede), jinak 0
    uint8 GetCurrentlyActiveMaterial(uint32 guildId);

}

#endif // GUILD_VILLAGE_PRODUCTION_API_H
