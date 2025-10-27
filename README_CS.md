# mod-guild-village 

### 🇬🇧 [English version](README_EN.md)

## Popis (CZ)  
Tento modul umožňuje:  
- Vytvořit guildovní vesnici  
- Přidávat creature a gameobjecty  
- Vlastní currency pro nákup rozšíření vesnice 
- Jednoduchá správa všech nastavení
- Nastavitelný limit pro vesnice, možnost nastavit automatické čištění při neaktivitě Guild Mastera

### Požadavky  
Před použitím je nutné zajistit, aby uživatel databáze z `WorldDatabaseInfo` (standardně `acore`) měl práva i na novou databázi `customs`:  

```sql
GRANT ALL PRIVILEGES ON customs.* TO 'acore'@'localhost';
FLUSH PRIVILEGES;
```

### ⚠️ Upozornění
Modul používá vlastní **entry** v tabulkách `gameobject_template`, `creature_template`, `creature_template_model`, `game_graveyard`, `graveyard_zone`, `fishing_loot_template`, `points_of_interest`.  
Je nutné zajistit, že tato ID nejsou v `acore_world` již obsazená jiným obsahem:

- **Vlastní gameobject (`gameobject_template.entry`):**
  - `990203`

- **lastní creatures (`creature_template.entry`):**
  - `987400`-`987430`, `987440`-`987454`

- **Vlastni PoI (`points_of_interest.Entry`):**
  - `1200`-`1250`
 
- **Vlastni Graveyard (`game_graveyard.Entry`):**
  - `2000`
  
Pokud máš v databázi již jiné záznamy s těmito ID, je potřeba čísla v modulu i v SQL posunout na jiný volný rozsah.

Odebrán smartAI pro cooking trenéra a ridding trenéra (pouze pohyb)

**Poznámky:**
- Modul používá vlastní DB `customs` v tabulkách je dodatečná správa vesnice
- gv_gameobject_template - Pokud chcete aby bylo něco vygenerováno ještě před vytvořením vesnice tak stačí přidat záznam zde. Pozor tato tabulka se aplikuje pouze jednou při zakoupení vesnice.
- gv_creature_template - Pokud chcete aby bylo něco vygenerováno ještě před vytvořením vesnice tak stačí přidat záznam zde. Pozor tato tabulka se aplikuje pouze jednou při zakoupení vesnice.

### Příkazy
.village teleport / tp
.v teleport / tp
➝ Teleportuje do guild vesnice.

.village i / in / inf / info
.v i / in / inf / info
➝ Zobrazí stav currency+boss+produkci pro guildu.

.village production / p
.v production /p
➝ Zobrazí produkci

.village currency / c
.v currency / c
➝ Zobrazí suroviny a cap

.village boss / b
.v boss / b
➝ Zobrazí stav bossů

### Admin Příkazy
.gv list [PAGE]
➝ Vypíše název guildy a její ID
Příklad: .gv list 15

.gv create [GUILDID] [ignorecap]
➝ Vytvoří guildovní vesnici pro danou guildu ručně (bez kontroly ceny).
Pokud GUILDID není zadán, použije se gilda, ve které se GM právě nachází.
Volitelný parametr ignorecap (0/1) určuje, zda se má ignorovat limit z GuildVillage.MaxVillages (např. pro testovací nebo eventové účely).
Příklad (vlastní gilda): .gv create
Příklad (jiná gilda, ignoruje limit): .gv create 42 1

.gv delete GUILDID
➝ Kompletně odstraní vesnici dané gildy z databáze (customs.gv_guild, customs.gv_currency, customs.gv_upgrades, creature, gameobject) a zároveň odstraní všechny objekty ze světa.
Používej opatrně — příkaz trvale maže veškerá data spojená s vesnicí.
Příklad: .gv delete 42

.gv reset GUILDID  
➝ Resetuje celou vesnici pro danou guildu: okamžitě despawne creatury/objekty v jejím **phaseMask** na mapě **37**, smaže související záznamy (`customs.gv_upgrades`, `creature`, `gameobject`, v `customs.gv_currency` vynuluje `timber/stone/iron/crystal`) a pak **znovu nainstaluje** základní layout (`base`).  
Příklad: `.gv reset 48`

.gv creature ENTRY [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]  
➝ Přidá **creature template** do `customs.gv_creature_template` pro layout **base** na mapě **37** v **aktuální pozici hráče**.  
Defaulty (pokud neuvedeš rozšířené parametry): `MOVEMENTTYPE=1`, `SPAWNDIST=15`, `SPAWNTIMESECS=300`.  
Příklad (jen entry): `.gv creature 12345`  
Příklad (všechny parametry): `.gv creature 12345 0 5 600`

.gv excreature EXPANSIONKEY ENTRY FACTION [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]  
➝ Přidá **expansion creature template** do `customs.gv_expansion_creatures` (klíč `expansion_key`), mapa **37**, pozice z hráče. `FACTION` může být `0/1/2` nebo `both/alliance/horde`.  
Defaulty: `MOVEMENTTYPE=0`, `SPAWNDIST=0`, `SPAWNTIMESECS=300`.  
Příklad (minimum): `.gv excreature vendor_armorer 12345 both`  
Příklad (všechny parametry): `.gv excreature vendor_armorer 12345 alliance 1 10 900`

.gv object ENTRY [SPAWNTIMESECS]  
➝ Přidá **gameobject template** do `customs.gv_gameobject_template` pro layout **base** na mapě **37** v **aktuální pozici hráče**. Rotace se nastaví na `0/0/0/0`.  
Default: `SPAWNTIMESECS=0`.  
Příklad (default): `.gv object 178123`  
Příklad (s respawnem): `.gv object 178123 120`

.gv exobject EXPANSIONKEY ENTRY FACTION [SPAWNTIMESECS]  
➝ Přidá **expansion gameobject template** do `customs.gv_expansion_gameobjects` (klíč `expansion_key`), mapa **37**, pozice z hráče. Rotace `0/0/0/0`. `FACTION` může být `0/1/2` nebo `both/alliance/horde`.  
Default: `SPAWNTIMESECS=0`.  
Příklad (default): `.gv exobject vendor_armorer 178123 horde`  
Příklad (s respawnem): `.gv exobject vendor_armorer 178123 2 300`

> Poznámky:
> - Všechny “add” příkazy berou **souřadnice a orientaci z aktuální pozice hráče**, ukládají **map=37** a layout `base` (pro expansion varianty `expansion_key` dle zadání).
> - `reset` se projeví **ihned** (živé objekty a creatury v dané fázi zmizí bez restartu) a po wipe proběhne **live reinstalace** základního layoutu.

## License
This module is licensed under the [GNU General Public License v3.0 (GPL-3.0)](LICENSE).


