# mod-guild-village

### 🇨🇿 [Czech version](README_CS.md)

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

## Database tables and their descriptions
[Wikipedia](https://github.com/BeardBear33/mod-guild-village/wiki/%5BEN%5D-Documentation)
