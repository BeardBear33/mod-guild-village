# mod-guild-village

### 🇨🇿 [Czech version](README.md)

## Description (EN)
This module allows you to:  
- Create a guild village  
- Add creatures and gameobjects  
- Use custom currencies for village expansion  
- Manage all settings easily  
- Configurable village limit, with optional auto-cleanup when the Guild Master is inactive.
- Daily and Weekly guild quests

### Installation / Requirements
For the autoupdater to function correctly, it is necessary to ensure that the database user from `(WorldDatabaseInfo) – "127.0.0.1;3306;acore;acore;acore_world"`  
has permissions for the new `customs` database as well:

```
GRANT CREATE ON *.* TO 'acore'@'127.0.0.1';
GRANT ALL PRIVILEGES ON customs.* TO 'acore'@'127.0.0.1';
FLUSH PRIVILEGES;
```

# ⚠️ WARNING: IMPORTANT CHANGE IN DATABASE STRUCTURE ⚠️
> **Applies only to those who downloaded the module before November 27, 2025.**

- The latest update can be found in customs/archive/updates/014_important_update.sql  
Don’t forget to apply all previous updates as well if you skipped any of them.  
- After applying 014_important_update.sql, the module will update itself automatically when the server starts.

**Optional:**
- Add this line to worldserver.conf:  
  Logger.gv.customs=3,Console Server

##

### ⚠️ Warning
The module uses its own **entries** in the following tables:  
`gameobject_template`, `creature_template`, `creature_template_model`, `game_graveyard`, `graveyard_zone`, `fishing_loot_template`, `points_of_interest`.  
Ensure that these IDs are not already occupied in your `acore_world` database:

- **Custom gameobject (`gameobject_template.entry`):**  
  - `990203`,`990204`

- **Custom creatures (`creature_template.entry`):**  
  - `987400`–`987430`, `987440`–`987454`

- **Custom PoI (`points_of_interest.Entry`):**  
  - `1200`–`1250`

- **Custom Graveyard (`game_graveyard.Entry`):**  
  - `2000`

If your database already contains records with these IDs, adjust them in the module and SQL to a different free range.

SmartAI for cooking and riding trainers has been removed (movement only).

**Notes:**  
- The module uses its own `customs` database for additional village management tables.  
- `gv_gameobject_template` – Add records here to pre-spawn objects **before** village creation. Applied only once during purchase.  
- `gv_creature_template` – Add records here to pre-spawn creatures **before** village creation. Applied only once during purchase.

### Commands
.village teleport / tp
.v teleport / tp
➝ Teleports to the guild village.

.village tp set
.v tp set
➝ Saves a teleport point.

.village info / i
.v info / i
➝ Displays guild+boss+production status.

.village production / p
.v production /p
➝ show production status

.village currency / c
.v currency / c
➝ show materials & caps

.village boss / b
.v boss / b
➝ show bosses status

.village expedition / e
.v expedition / e
➝ show expedition status

.village questdaily / qd
.v questdaily / qd
➝ show active daily quest
> If more than one quest is enabled, pagination is used to switch between the quest pages.  
> Example: .village questdaily 5

.village questweekly / qw
.v questweekly / qw
➝ show active weekly quest
> If more than one quest is enabled, pagination is used to switch between the quest pages.  
> Example: .village questweekly 5

### Admin Commands
.gv list [PAGE]
➝ Lists the guild name and its ID
Example: .gv list 15

.gv create [GUILDID] [ignorecap]  
➝ Manually creates a guild village for the specified guild (without cost check).  
If **GUILDID** is not provided, the guild of the current GM will be used.  
The optional parameter **ignorecap** (0/1) determines whether to ignore the limit defined by `GuildVillage.MaxVillages` (useful for testing or events).  
Example (own guild): `.gv create`  
Example (another guild, ignoring cap): `.gv create 42 1`  

.gv delete GUILDID  
➝ Completely removes the specified guild’s village from the database (`customs.gv_guild`, `customs.gv_currency`, `customs.gv_upgrades`, `creature`, `gameobject`) and also deletes all related objects from the world.  
Use with caution — this command permanently deletes all data associated with the village.  
Example: `.gv delete 42`

.gv reset GUILDID  
➝ Resets the entire village for the specified guild: instantly despawns creatures and objects in its **phaseMask** on map **37**, deletes related records (`customs.gv_upgrades`, `creature`, `gameobject`, resets `timber/stone/iron/crystal` in `customs.gv_currency`), and then **reinstalls** the base layout (`base`).  
Example: `.gv reset 48`

.gv creature ENTRY [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]  
➝ Adds a **creature template** to `customs.gv_creature_template` for the **base** layout on map **37** at the player’s current position.  
Defaults (if not specified): `MOVEMENTTYPE=1`, `SPAWNDIST=15`, `SPAWNTIMESECS=300`.  
Example (entry only): `.gv creature 12345`  
Example (all parameters): `.gv creature 12345 0 5 600`

.gv excreature EXPANSIONKEY ENTRY FACTION [MOVEMENTTYPE SPAWNDIST SPAWNTIMESECS]  
➝ Adds an **expansion creature template** to `customs.gv_expansion_creatures` (`expansion_key`), map **37**, at player position.  
`FACTION` can be `0/1/2` or `both/alliance/horde`.  
Defaults: `MOVEMENTTYPE=0`, `SPAWNDIST=0`, `SPAWNTIMESECS=300`.  
Example (minimum): `.gv excreature vendor_armorer 12345 both`  
Example (full): `.gv excreature vendor_armorer 12345 alliance 1 10 900`

.gv object ENTRY [SPAWNTIMESECS]  
➝ Adds a **gameobject template** to `customs.gv_gameobject_template` for the **base** layout on map **37** at the player’s current position. Rotation is set to `0/0/0/0`.  
Default: `SPAWNTIMESECS=0`.  
Example (default): `.gv object 178123`  
Example (with respawn): `.gv object 178123 120`

.gv exobject EXPANSIONKEY ENTRY FACTION [SPAWNTIMESECS]  
➝ Adds an **expansion gameobject template** to `customs.gv_expansion_gameobjects` (`expansion_key`), map **37**, at player’s position. Rotation `0/0/0/0`.  
`FACTION` can be `0/1/2` or `both/alliance/horde`.  
Default: `SPAWNTIMESECS=0`.  
Example (default): `.gv exobject vendor_armorer 178123 horde`  
Example (with respawn): `.gv exobject vendor_armorer 178123 2 300`

> Notes:  
> - All “add” commands take **coordinates and orientation from the player’s current position**, store **map=37** and layout `base` (for expansions, `expansion_key` as specified).  
> - `reset` applies **instantly** (live creatures and objects vanish immediately) and performs a **live reinstallation** of the base layout.

## Database tables and their descriptions
**WIP**

