#include "Config.h"
#include "gv_names.h"
#include <algorithm>
#include <vector>

namespace GuildVillage { namespace Names {

static All  g_all;
static bool g_loaded = false;

static bool IsEN()
{
    auto loc = sConfigMgr->GetOption<std::string>("GuildVillage.Locale","cs");
    std::transform(loc.begin(), loc.end(), loc.begin(), ::tolower);
    return (loc == "en" || loc == "english");
}

static void Load()
{
    bool en = IsEN();

    auto G = [&](char const* csKey, char const* enKey, char const* defCS, char const* defEN) -> std::string
    {
        return sConfigMgr->GetOption<std::string>(en ? enKey : csKey, en ? defEN : defCS);
    };

    // --- Statusové názvy měn jako celků (zobrazují se ve .v status / gossip status)
    // Např. "Dřevo", "Kámen", "Železo", "Krystaly"
    g_all.status.material1 = G(
        "GuildVillage.Material.Material1",
        "GuildVillage.MaterialEN.Material1",
        "Material1",
        "Material1"
    );

    g_all.status.material2 = G(
        "GuildVillage.Material.Material2",
        "GuildVillage.MaterialEN.Material2",
        "Material2",
        "Material2"
    );

    g_all.status.material3 = G(
        "GuildVillage.Material.Material3",
        "GuildVillage.MaterialEN.Material3",
        "Material3",
        "Material3"
    );

    g_all.status.material4 = G(
        "GuildVillage.Material.Material4",
        "GuildVillage.MaterialEN.Material4",
        "Material4",
        "Material4"
    );

    // --- Jednotkové názvy pro loot/cenu ---
    // Tohle jsou kusové tvary ("1 prkno" / "2 prkna" / "5 prken")
    // Každý materiál má 3 config keys:
    //   Singular = tvar pro 1
    //   Dual     = tvar pro 2-4
    //   Plural   = tvar pro 5+
    //
    // V EN klidně nastavíš Dual == Plural ("crystals", "stones"...)
    auto GS = [&](char const* csKey, char const* enKey, char const* defCS, char const* defEN) -> std::string
    {
        return sConfigMgr->GetOption<std::string>(en ? enKey : csKey, en ? defEN : defCS);
    };

    // Material1
    g_all.units.material1.sg = GS(
        "GuildVillage.MaterialUnit.Material1.Singular",
        "GuildVillage.MaterialUnitEN.Material1.Singular",
        "Material1",
        "Material1"
    );
    g_all.units.material1.du = GS(
        "GuildVillage.MaterialUnit.Material1.Dual",
        "GuildVillage.MaterialUnitEN.Material1.Dual",
        "Material1",
        "Material1"
    );
    g_all.units.material1.pl = GS(
        "GuildVillage.MaterialUnit.Material1.Plural",
        "GuildVillage.MaterialUnitEN.Material1.Plural",
        "Material1",
        "Material1"
    );

    // Material2
    g_all.units.material2.sg = GS(
        "GuildVillage.MaterialUnit.Material2.Singular",
        "GuildVillage.MaterialUnitEN.Material2.Singular",
        "Material2",
        "Material2"
    );
    g_all.units.material2.du = GS(
        "GuildVillage.MaterialUnit.Material2.Dual",
        "GuildVillage.MaterialUnitEN.Material2.Dual",
        "Material2",
        "Material2"
    );
    g_all.units.material2.pl = GS(
        "GuildVillage.MaterialUnit.Material2.Plural",
        "GuildVillage.MaterialUnitEN.Material2.Plural",
        "Material2",
        "Material2"
    );

    // Material3
    g_all.units.material3.sg = GS(
        "GuildVillage.MaterialUnit.Material3.Singular",
        "GuildVillage.MaterialUnitEN.Material3.Singular",
        "Material3",
        "Material3"
    );
    g_all.units.material3.du = GS(
        "GuildVillage.MaterialUnit.Material3.Dual",
        "GuildVillage.MaterialUnitEN.Material3.Dual",
        "Material3",
        "Material3"
    );
    g_all.units.material3.pl = GS(
        "GuildVillage.MaterialUnit.Material3.Plural",
        "GuildVillage.MaterialUnitEN.Material3.Plural",
        "Material3",
        "Material3"
    );

    // Material4
    g_all.units.material4.sg = GS(
        "GuildVillage.MaterialUnit.Material4.Singular",
        "GuildVillage.MaterialUnitEN.Material4.Singular",
        "Material4",
        "Material4"
    );
    g_all.units.material4.du = GS(
        "GuildVillage.MaterialUnit.Material4.Dual",
        "GuildVillage.MaterialUnitEN.Material4.Dual",
        "Material4",
        "Material4"
    );
    g_all.units.material4.pl = GS(
        "GuildVillage.MaterialUnit.Material4.Plural",
        "GuildVillage.MaterialUnitEN.Material4.Plural",
        "Material4",
        "Material4"
    );

    // --- bezpečné fallbacky, kdyby něco chybělo v configu ---
    auto fixUnit = [](Unit& u)
    {
        // pokud není dual, použij plural
        if (u.du.empty())
            u.du = u.pl;
        // pokud není plural, použij dual
        if (u.pl.empty())
            u.pl = u.du;
        // pokud by chyběl sg, prostě ho nastavíme na du/pl, ať se to nikdy nerozbije
        if (u.sg.empty())
            u.sg = !u.du.empty() ? u.du : u.pl;
    };

    fixUnit(g_all.units.material1);
    fixUnit(g_all.units.material2);
    fixUnit(g_all.units.material3);
    fixUnit(g_all.units.material4);

    g_loaded = true;
}

All const& Get()
{
    if (!g_loaded)
        Load();
    return g_all;
}

static std::string const& StatusOf(Mat m)
{
    auto const& S = Get().status;
    switch (m)
    {
        case Mat::Material1: return S.material1;
        case Mat::Material2: return S.material2;
        case Mat::Material3: return S.material3;
        case Mat::Material4: return S.material4;
    }
    // never hit, just silence compiler
    return S.material1;
}

std::string Label(Mat m)
{
    return StatusOf(m);
}

static Unit const& UnitOf(Mat m)
{
    auto const& U = Get().units;
    switch (m)
    {
        case Mat::Material1: return U.material1;
        case Mat::Material2: return U.material2;
        case Mat::Material3: return U.material3;
        case Mat::Material4: return U.material4;
    }
    return U.material1;
}

std::string CountName(Mat m, uint64 n)
{
    auto const& u = UnitOf(m);

    // 1 → sg
    if (n == 1 && !u.sg.empty())
        return u.sg;

    // 2-4 → du (české tvary "2 kameny", "3 krystaly")
    // v EN prostě nastavíš du == pl, takže to bude jednotný
    if (n >= 2 && n <= 4 && !u.du.empty())
        return u.du;

    // 5+ → pl
    if (!u.pl.empty())
        return u.pl;

    // ultimate fallback: label materiálu (z .v status)
    return Label(m);
}

std::string CostLine(uint32 mat1, uint32 mat2, uint32 mat3, uint32 mat4)
{
    bool en = IsEN();

    if (mat1 == 0 && mat2 == 0 && mat3 == 0 && mat4 == 0)
        return en ? "Free" : "Zdarma";

    std::vector<std::string> parts;
    parts.reserve(4);

    if (mat1)
        parts.push_back(std::to_string(mat1) + " " + CountName(Mat::Material1, mat1));
    if (mat2)
        parts.push_back(std::to_string(mat2) + " " + CountName(Mat::Material2, mat2));
    if (mat3)
        parts.push_back(std::to_string(mat3) + " " + CountName(Mat::Material3, mat3));
    if (mat4)
        parts.push_back(std::to_string(mat4) + " " + CountName(Mat::Material4, mat4));

    std::string out = parts.front();
    for (size_t i = 1; i < parts.size(); ++i)
        out += " + " + parts[i];

    return out;
}

}} // namespace GuildVillage::Names
