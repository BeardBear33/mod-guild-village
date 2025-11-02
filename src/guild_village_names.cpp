#include "Config.h"
#include "gv_names.h"
#include <algorithm>
#include <vector>
#include <sstream>

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

    // ---- Status labels (názvy materiálů) ----
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
    // NOVÉ: Gold label
    g_all.status.gold = G(
        "GuildVillage.Material.Gold",
        "GuildVillage.MaterialEN.Gold",
        "Zlato",
        "Gold"
    );

    auto GS = [&](char const* csKey, char const* enKey, char const* defCS, char const* defEN) -> std::string
    {
        return sConfigMgr->GetOption<std::string>(en ? enKey : csKey, en ? defEN : defCS);
    };

    // Material1
    g_all.units.material1.sg = GS("GuildVillage.MaterialUnit.Material1.Singular", "GuildVillage.MaterialUnitEN.Material1.Singular", "Material1", "Material1");
    g_all.units.material1.du = GS("GuildVillage.MaterialUnit.Material1.Dual",     "GuildVillage.MaterialUnitEN.Material1.Dual",     "Material1", "Material1");
    g_all.units.material1.pl = GS("GuildVillage.MaterialUnit.Material1.Plural",   "GuildVillage.MaterialUnitEN.Material1.Plural",   "Material1", "Material1");

    // Material2
    g_all.units.material2.sg = GS("GuildVillage.MaterialUnit.Material2.Singular", "GuildVillage.MaterialUnitEN.Material2.Singular", "Material2", "Material2");
    g_all.units.material2.du = GS("GuildVillage.MaterialUnit.Material2.Dual",     "GuildVillage.MaterialUnitEN.Material2.Dual",     "Material2", "Material2");
    g_all.units.material2.pl = GS("GuildVillage.MaterialUnit.Material2.Plural",   "GuildVillage.MaterialUnitEN.Material2.Plural",   "Material2", "Material2");

    // Material3
    g_all.units.material3.sg = GS("GuildVillage.MaterialUnit.Material3.Singular", "GuildVillage.MaterialUnitEN.Material3.Singular", "Material3", "Material3");
    g_all.units.material3.du = GS("GuildVillage.MaterialUnit.Material3.Dual",     "GuildVillage.MaterialUnitEN.Material3.Dual",     "Material3", "Material3");
    g_all.units.material3.pl = GS("GuildVillage.MaterialUnit.Material3.Plural",   "GuildVillage.MaterialUnitEN.Material3.Plural",   "Material3", "Material3");

    // Material4
    g_all.units.material4.sg = GS("GuildVillage.MaterialUnit.Material4.Singular", "GuildVillage.MaterialUnitEN.Material4.Singular", "Material4", "Material4");
    g_all.units.material4.du = GS("GuildVillage.MaterialUnit.Material4.Dual",     "GuildVillage.MaterialUnitEN.Material4.Dual",     "Material4", "Material4");
    g_all.units.material4.pl = GS("GuildVillage.MaterialUnit.Material4.Plural",   "GuildVillage.MaterialUnitEN.Material4.Plural",   "Material4", "Material4");

    // NOVÉ: GOLD jednotky (výchozí rozumné tvary)
    // CZ defaulty: 1 "zlato", 2–4 "zlaté", 5+ "zlatých"
    // EN defaulty: "gold", "gold", "gold" (bez počtu nebo jako "gold coins" si můžeš přepsat v .conf)
    g_all.units.gold.sg = GS(
        "GuildVillage.MaterialUnit.Gold.Singular",
        "GuildVillage.MaterialUnitEN.Gold.Singular",
        "zlato",
        "gold"
    );
    g_all.units.gold.du = GS(
        "GuildVillage.MaterialUnit.Gold.Dual",
        "GuildVillage.MaterialUnitEN.Gold.Dual",
        "zlaté",
        "gold"
    );
    g_all.units.gold.pl = GS(
        "GuildVillage.MaterialUnit.Gold.Plural",
        "GuildVillage.MaterialUnitEN.Gold.Plural",
        "zlatých",
        "gold"
    );

    auto fixUnit = [](Unit& u)
    {
        if (u.du.empty())
            u.du = u.pl;
        if (u.pl.empty())
            u.pl = u.du;
        if (u.sg.empty())
            u.sg = !u.du.empty() ? u.du : u.pl;
    };

    fixUnit(g_all.units.material1);
    fixUnit(g_all.units.material2);
    fixUnit(g_all.units.material3);
    fixUnit(g_all.units.material4);
    fixUnit(g_all.units.gold); // <-- NOVÉ

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

static Unit const& UnitGold()
{
    return Get().units.gold;
}

std::string CountName(Mat m, uint64 n)
{
    auto const& u = UnitOf(m);

    if (n == 1 && !u.sg.empty()) return u.sg;
    if (n >= 2 && n <= 4 && !u.du.empty()) return u.du;
    if (!u.pl.empty()) return u.pl;

    return Label(m);
}

std::string CountNameGold(uint64 n)
{
    auto const& u = UnitGold();

    if (n == 1 && !u.sg.empty()) return u.sg;
    if (n >= 2 && n <= 4 && !u.du.empty()) return u.du;
    if (!u.pl.empty()) return u.pl;

    // fallback
    return IsEN() ? "gold" : "zlato";
}

static std::string JoinParts(std::vector<std::string> const& parts)
{
    if (parts.empty())
        return "";
    std::ostringstream oss;
    oss << parts[0];
    for (size_t i = 1; i < parts.size(); ++i)
        oss << " + " << parts[i];
    return oss.str();
}

std::string CostLine(uint32 mat1, uint32 mat2, uint32 mat3, uint32 mat4)
{
    // volá novou overload s gold=0 (zpětná kompatibilita)
    return CostLine(mat1, mat2, mat3, mat4, 0);
}

std::string CostLine(uint32 mat1, uint32 mat2, uint32 mat3, uint32 mat4, uint32 gold)
{
    bool en = IsEN();

    if (mat1 == 0 && mat2 == 0 && mat3 == 0 && mat4 == 0 && gold == 0)
        return en ? "Free" : "Zdarma";

    std::vector<std::string> parts;
    parts.reserve(5);

    if (mat1) parts.push_back(std::to_string(mat1) + " " + CountName(Mat::Material1, mat1));
    if (mat2) parts.push_back(std::to_string(mat2) + " " + CountName(Mat::Material2, mat2));
    if (mat3) parts.push_back(std::to_string(mat3) + " " + CountName(Mat::Material3, mat3));
    if (mat4) parts.push_back(std::to_string(mat4) + " " + CountName(Mat::Material4, mat4));
    if (gold) parts.push_back(std::to_string(gold) + " " + CountNameGold(gold));

    return JoinParts(parts);
}

}} // namespace
