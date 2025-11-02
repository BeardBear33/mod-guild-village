#pragma once

#include <string>
#include "Define.h"

namespace GuildVillage { namespace Names {

enum class Mat : uint8 { Material1, Material2, Material3, Material4 };

struct Unit
{
    std::string sg;
    std::string du;
    std::string pl;
};

struct StatusLabels
{
    std::string material1;
    std::string material2;
    std::string material3;
    std::string material4;
	std::string gold;
};

struct UnitLabels
{
    Unit material1;
    Unit material2;
    Unit material3;
    Unit material4;
	Unit gold;
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
std::string Label(Mat m);

// Vrátí správný tvar jednotky podle počtu:
std::string CountName(Mat m, uint64 n);

// NOVÉ: vrátí správný tvar jednotky pro gold podle počtu
std::string CountNameGold(uint64 n);

// Postaví string ceny z více materiálů
std::string CostLine(uint32 mat1, uint32 mat2, uint32 mat3, uint32 mat4);

// NOVÉ: totéž, ale navíc přidá zlato (gold)
std::string CostLine(uint32 mat1, uint32 mat2, uint32 mat3, uint32 mat4, uint32 gold);

}} // namespace GuildVillage::Names
