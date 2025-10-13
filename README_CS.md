# mod-guild-village 

### 🇬🇧 [English version](README_EN.md)

## Popis (CZ)  
Tento modul umožňuje:  
- Vytvořit guildovní vesnici  
- Přidávat creature a gameobjecty  
- Vlastní currency pro nákup rozšíření vesnice 
- Jednoduchá správa všech nastavení

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
  - `987400`-`987421`, `987450`-`987454`

- **Vlastni PoI (`points_of_interest.Entry`):**
  - `1200`-`1246`
 
- **Vlastni Graveyard (`game_graveyard.Entry`):**
  - `2000`
  - 
Pokud máš v databázi již jiné záznamy s těmito ID, je potřeba čísla v modulu i v SQL posunout na jiný volný rozsah.

Odebrán smartAI pro cooking trenéra a ridding trenéra (pouze pohyb)

**Poznámky:**
- Modul používá vlastní DB `customs` v tabulkách je dodatečná správa vesnice
- gv_gameobject_template - Pokud chcete aby bylo něco vygenerováno ještě před vytvořením vesnice tak stačí přidat záznam zde. Pozor tato tabulka se aplikuje pouze jednou při zakoupení vesnice.
- gv_creature_template - Pokud chcete aby bylo něco vygenerováno ještě před vytvořením vesnice tak stačí přidat záznam zde. Pozor tato tabulka se aplikuje pouze jednou při zakoupení vesnice.

### Příkazy
.village teleport
➝ Teleportuje do guild vesnice.

.village status
➝ Zobrazí stav currency pro guildu.

### Admin Příkazy
.gv reset GUILDID
➝ Resetuje celou vesnici pro danou guildu
Příklad: .gv reset 48

.gv creature
➝ WIP
Příklad: .gv creature

.gv excreature
➝ WIP
Příklad: .gv excreature

.gv object
➝ WIP
Příklad: .gv object

.gv exobject
➝ WIP
Příklad: .gv exobject

## License
This module is licensed under the [GNU General Public License v3.0 (GPL-3.0)](LICENSE).


