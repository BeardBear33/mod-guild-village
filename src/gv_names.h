#pragma once

#include <string>
#include "Define.h" // kvůli uint8/uint32/uint64

namespace GuildVillage { namespace Names {

// 4 materiály – tvoje nová schéma
enum class Mat : uint8 { Material1, Material2, Material3, Material4 };

// Jednotky pro počítání kusů v loot/ceně
// sg = 1 kus
// du = 2-4 kusy (čeština), v angličtině to klidně může být stejné jako pl
// pl = 5+ kusů
struct Unit
{
    std::string sg;
    std::string du;
    std::string pl;
};

// Štítky pro status (".v status", gossip 'Zobrazit stav materiálu')
// -> tohle jsou názvy měn jako celek, ne jednotky
//    např. "Dřevo", "Kámen", "Železo", "Krystaly"
struct StatusLabels
{
    std::string material1;
    std::string material2;
    std::string material3;
    std::string material4;
};

// Jednotkové názvy (sg/du/pl) pro loot hlášky a cost line v gossipu
struct UnitLabels
{
    Unit material1;
    Unit material2;
    Unit material3;
    Unit material4;
};

// Všechno pohromadě
struct All
{
    StatusLabels status;
    UnitLabels   units;
};

// Vrátí cache s načtenými názvy z configu
All const& Get();

// Vrátí status label pro daný materiál
// (to, co vidíš ve .v status a v "Show currency status")
std::string Label(Mat m);

// Vrátí správný tvar jednotky podle počtu:
// 1  -> sg
// 2-4-> du
// 5+ -> pl
// fallbacky řešíme uvnitř (když něco chybí v configu)
std::string CountName(Mat m, uint64 n);

// Postaví string ceny z více materiálů
// např. "5 prken + 2 kameny"
// vrátí "Free"/"Zdarma", pokud jsou všechny čtyři 0
std::string CostLine(uint32 mat1, uint32 mat2, uint32 mat3, uint32 mat4);

}} // namespace GuildVillage::Names
