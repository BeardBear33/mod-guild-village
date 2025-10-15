// modules/mod-guild-village/src/guild_village_rest.cpp

#include "ScriptMgr.h"
#include "Player.h"

namespace GuildVillage
{
    // === Geometrie (mapa a polygon) ===
    static constexpr uint32 GV_MAP_ID = 37;

    struct Pt { float x, y; };

    // Čtyři rohy (XY), Z je omezen zvlášť
    static constexpr Pt POLY[] = {
        {  859.37213f, -111.94566f }, // 1
        {  712.58050f,  284.43683f }, // 2
        { 1156.02890f,  513.30300f }, // 3
        { 1336.03150f,  130.17856f }  // 4
    };
    static constexpr size_t POLY_N = sizeof(POLY) / sizeof(POLY[0]);

    // Z rozsah
    static constexpr float Z_MIN = 253.0f;
    static constexpr float Z_MAX = 397.0f;

    // Ray-casting 2D test (bod v polygonu)
    static bool PointInPolygon(float x, float y)
    {
        bool inside = false;
        for (size_t i = 0, j = POLY_N - 1; i < POLY_N; j = i++)
        {
            const Pt& pi = POLY[i];
            const Pt& pj = POLY[j];

            const bool crosses = ((pi.y > y) != (pj.y > y)) &&
                                 (x < (pj.x - pi.x) * (y - pi.y) / (pj.y - pi.y) + pi.x);
            if (crosses)
                inside = !inside;
        }
        return inside;
    }

    // Kompletní podmínka: správná mapa + XY + Z
    static inline bool Contains(uint32 mapId, float x, float y, float z)
    {
        if (mapId != GV_MAP_ID)
            return false;
        if (z < Z_MIN || z > Z_MAX)
            return false;
        return PointInPolygon(x, y);
    }

    class GuildVillage_RestZone : public PlayerScript
    {
    public:
        GuildVillage_RestZone() : PlayerScript("GuildVillage_RestZone") { }

        void OnPlayerLogin(Player* player) override
        {
            Apply(player);
        }

        void OnPlayerUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override
        {
            Apply(player);
        }

        // VOLÁ SE KAŽDÝ TICK – udržuje rest-flag proti vnitřnímu resetu
        void OnPlayerAfterUpdate(Player* player, uint32 /*diff*/) override
        {
            Apply(player);
        }

        // Před logoutem ještě jednou “přitlačit” rest flag
        void OnPlayerBeforeLogout(Player* player) override
        {
            if (!player) return;
            const bool inside = Contains(player->GetMapId(),
                                         player->GetPositionX(),
                                         player->GetPositionY(),
                                         player->GetPositionZ());
            if (inside)
                player->SetRestFlag(REST_FLAG_IN_TAVERN);
        }

    private:
        void Apply(Player* player)
        {
            if (!player) return;

            const bool inside = Contains(player->GetMapId(),
                                         player->GetPositionX(),
                                         player->GetPositionY(),
                                         player->GetPositionZ());

            if (inside)
                player->SetRestFlag(REST_FLAG_IN_TAVERN);
            else
                player->RemoveRestFlag(REST_FLAG_IN_TAVERN);
        }
    };
} // namespace GuildVillage

// Export registrátoru
void RegisterGuildVillageRest()
{
    new GuildVillage::GuildVillage_RestZone();
}
