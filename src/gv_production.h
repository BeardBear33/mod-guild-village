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

    std::optional<GuildCurrency> SyncGuildProduction(uint32 guildId);
	
	struct ProdStatusForMat
    {
        bool   active;
        uint32 amountPerTick;
        float  hoursPerTick;
    };

    ProdStatusForMat GetProductionStatus(uint32 guildId, uint8 materialId);
	
    uint8 GetCurrentlyActiveMaterial(uint32 guildId);

}

#endif // GUILD_VILLAGE_PRODUCTION_API_H
