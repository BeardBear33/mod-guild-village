-- =========================================================
-- Guild Village / Expeditions initialization
-- Migration 2025-11-02
-- Target schema: `customs`
-- =========================================================


-- -------------------------------------------------
-- ALTER TABLE (must run first)
-- -------------------------------------------------
-- Přidání sloupců info_cs a info_en pokud ještě neexistují
-- (tento krok musí proběhnout před následujícími INSERT příkazy)

-- Bezpečné přidání sloupce info_cs
SET @col_exists_info_cs := (
    SELECT COUNT(*)
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = 'customs'
      AND TABLE_NAME = 'gv_upgrade_catalog'
      AND COLUMN_NAME = 'info_cs'
);

SET @sql_info_cs := IF(
    @col_exists_info_cs = 0,
    'ALTER TABLE `customs`.`gv_upgrade_catalog` ADD COLUMN `info_cs` TEXT NULL AFTER `label_en`',
    'SELECT 1'
);

PREPARE stmt_info_cs FROM @sql_info_cs;
EXECUTE stmt_info_cs;
DEALLOCATE PREPARE stmt_info_cs;


-- Bezpečné přidání sloupce info_en
SET @col_exists_info_en := (
    SELECT COUNT(*)
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = 'customs'
      AND TABLE_NAME = 'gv_upgrade_catalog'
      AND COLUMN_NAME = 'info_en'
);

SET @sql_info_en := IF(
    @col_exists_info_en = 0,
    'ALTER TABLE `customs`.`gv_upgrade_catalog` ADD COLUMN `info_en` TEXT NULL AFTER `info_cs`',
    'SELECT 1'
);

PREPARE stmt_info_en FROM @sql_info_en;
EXECUTE stmt_info_en;
DEALLOCATE PREPARE stmt_info_en;


-- -------------------------------------------------
-- gv_upgrade_poi
-- -------------------------------------------------

INSERT INTO `gv_upgrade_poi`
    (`id`, `expansion_key`, `faction`, `map`, `pos_x`, `pos_y`, `icon`, `flags`, `poi_id`, `name_cs`, `name_en`)
VALUES
    (49, 'bg_arena', 0, 37, 997.877, 344.597, 7, 0, 1248, 'Vendor: PvP', 'Vendor: PvP'),
    (50, 'teleporter', 0, 37, 1066, 279.232, 7, 0, 1249, 'Teleportér', 'Teleporter')
ON DUPLICATE KEY UPDATE
    `expansion_key` = VALUES(`expansion_key`),
    `faction`       = VALUES(`faction`),
    `map`           = VALUES(`map`),
    `pos_x`         = VALUES(`pos_x`),
    `pos_y`         = VALUES(`pos_y`),
    `icon`          = VALUES(`icon`),
    `flags`         = VALUES(`flags`),
    `poi_id`        = VALUES(`poi_id`),
    `name_cs`       = VALUES(`name_cs`),
    `name_en`       = VALUES(`name_en`);

-- -------------------------------------------------
-- gv_upgrade_catalog
-- -------------------------------------------------

INSERT INTO `gv_upgrade_catalog`
    (`id`, `category`, `expansion_key`, `label_cs`, `label_en`, `info_cs`, `info_en`,
     `cost_material1`, `cost_material2`, `cost_material3`, `cost_material4`, `sort_order`)
VALUES
    (49, 'others',  'guards',
     'Stráže vesnice', 'Village Guards',
     'Pridává guardy kterí vám reknou cestu pokud neco hledáte.',
     'Adds guards who will tell you the way if you’re looking for something.',
     40, 42, 48, 72, 1),

    (50, 'objects', 'ore_pack1',
     'Balík rudy I', 'Ore Bundle I',
     'Spawnovací sada minerálů v oblasti vesnice.',
     'A spawning set of minerals in the village area.',
     0, 120, 80, 0, 1),

    (51, 'objects', 'ore_pack2',
     'Balík rudy II', 'Ore Bundle II',
     'Spawnovací sada minerálů v oblasti vesnice.',
     'A spawning set of minerals in the village area.',
     0, 160, 120, 0, 1),

    (52, 'objects', 'ore_pack3',
     'Balík rudy III', 'Ore Bundle III',
     'Spawnovací sada minerálů v oblasti vesnice.',
     'A spawning set of minerals in the village area.',
     0, 200, 160, 0, 1),

    (53, 'objects', 'herb_pack1',
     'Balík kytek I', 'Herb Pack I',
     'Spawnovací sada kytek v oblasti vesnice.',
     'A set of herb spawns in the village area.',
     120, 0, 0, 80, 1),

    (54, 'objects', 'herb_pack2',
     'Balík kytek II', 'Herb Pack II',
     'Spawnovací sada kytek v oblasti vesnice.',
     'A set of herb spawns in the village area.',
     160, 0, 0, 120, 1),

    (55, 'objects', 'herb_pack3',
     'Balík kytek III', 'Herb Pack III',
     'Spawnovací sada kytek v oblasti vesnice.',
     'A set of herb spawns in the village area.',
     20, 0, 0, 160, 1),

    (56, 'others',  'monster_pack1',
     'Balík monster I', 'Monster Pack I',
     'Spawnovací sada monster ze kterých padá material.',
     'A set of monsters that drop materials.',
     90, 80, 50, 120, 1),

    (57, 'others',  'monster_pack2',
     'Balík monster II', 'Monster Pack II',
     'Spawnovací sada monster ze kterých padá material.',
     'A set of monsters that drop materials.',
     130, 120, 90, 160, 1),

    (58, 'others',  'monster_pack3',
     'Balík monster III', 'Monster Pack III',
     'Spawnovací sada monster ze kterých padá material.',
     'A set of monsters that drop materials.',
     170, 160, 130, 200, 1),

    (59, 'objects', 'teleporter',
     'Základna Expedic', 'Expedition Base',
     'Pridává teleport na expedicní základnu.',
     'Adds a teleport to the expedition base.',
     50, 30, 190, 360, 1),

    (60, 'objects', 'stone_path',
     'Kamená cesta', 'Stone Path',
     'Pouze dekorativní prvek',
     'Decorative element only.',
     0, 90, 0, 0, 1),

    (61, 'objects', 'outdoor_lamp',
     'Venkovní lampy', 'Outdoor Lamps',
     'Pouze dekorativní prvek',
     'Decorative element only.',
     15, 30, 20, 50, 1),

    (62, 'objects', 'indoor_lamp',
     'Lampy (Interiér)', 'Inside Lamps',
     'Pouze dekorativní prvek',
     'Decorative element only.',
     15, 30, 20, 50, 1),

    (63, 'others',  'material_production',
     'Výrobní Jednotka', 'Production Unit',
     'Spawnuje NPC které vyrábí material pro vaši vesnici.',
     'Spawns an NPC that produces materials for your village.',
     200, 200, 200, 200, 1),

    (64, 'vendor',  'bg_arena',
     'Vendor: PvP', 'Vendor: PvP',
     'Pridává npc pro nakup PvP vybaveni',
     'Adds an NPC for purchasing PvP gear.',
     0, 0, 18, 24, 1)

ON DUPLICATE KEY UPDATE
    `category`        = VALUES(`category`),
    `expansion_key`   = VALUES(`expansion_key`),
    `label_cs`        = VALUES(`label_cs`),
    `label_en`        = VALUES(`label_en`),
    `info_cs`         = VALUES(`info_cs`),
    `info_en`         = VALUES(`info_en`),
    `cost_material1`  = VALUES(`cost_material1`),
    `cost_material2`  = VALUES(`cost_material2`),
    `cost_material3`  = VALUES(`cost_material3`),
    `cost_material4`  = VALUES(`cost_material4`),
    `sort_order`      = VALUES(`sort_order`);

-- -------------------------------------------------
-- gv_expansion_gameobjects
-- -------------------------------------------------

INSERT INTO `gv_expansion_gameobjects`
    (`id`, `expansion_key`, `entry`, `map`, `position_x`, `position_y`, `position_z`,
     `orientation`, `rotation0`, `rotation1`, `rotation2`, `rotation3`,
     `spawntimesecs`, `faction`)
VALUES
    (719, 'teleporter', 990203, 37,
     113.003, 990.629, 296.39,
     -0.339317,
     0, 0, 0, 0,
     120, 0),
    (720, 'mailbox', 191955, 37,
     112.675, 1024.16, 296.214,
     5.5119,
     0, 0, 0, 1,
     0, 0)
ON DUPLICATE KEY UPDATE
    `expansion_key`  = VALUES(`expansion_key`),
    `entry`          = VALUES(`entry`),
    `map`            = VALUES(`map`),
    `position_x`     = VALUES(`position_x`),
    `position_y`     = VALUES(`position_y`),
    `position_z`     = VALUES(`position_z`),
    `orientation`    = VALUES(`orientation`),
    `rotation0`      = VALUES(`rotation0`),
    `rotation1`      = VALUES(`rotation1`),
    `rotation2`      = VALUES(`rotation2`),
    `rotation3`      = VALUES(`rotation3`),
    `spawntimesecs`  = VALUES(`spawntimesecs`),
    `faction`        = VALUES(`faction`);

-- -------------------------------------------------
-- gv_expansion_creatures
-- -------------------------------------------------

INSERT INTO `gv_expansion_creatures`
    (`id`, `expansion_key`, `entry`, `map`, `position_x`, `position_y`, `position_z`,
     `orientation`, `spawntimesecs`, `spawndist`, `movementtype`, `faction`)
VALUES
    (300, 'teleporter', 987447, 37,
     127.442, 976.121, 295.072,
     1.16472,
     300, 0, 0, 0),

    (301, 'teleporter', 987446, 37,
     109.794, 1006.94, 295.768,
     0.300778,
     300, 0, 0, 0)
ON DUPLICATE KEY UPDATE
    `expansion_key` = VALUES(`expansion_key`),
    `entry`         = VALUES(`entry`),
    `map`           = VALUES(`map`),
    `position_x`    = VALUES(`position_x`),
    `position_y`    = VALUES(`position_y`),
    `position_z`    = VALUES(`position_z`),
    `orientation`   = VALUES(`orientation`),
    `spawntimesecs` = VALUES(`spawntimesecs`),
    `spawndist`     = VALUES(`spawndist`),
    `movementtype`  = VALUES(`movementtype`),
    `faction`       = VALUES(`faction`);

-- -------------------------------------------------
-- gv_teleport_menu
-- -------------------------------------------------

INSERT INTO `gv_teleport_menu`
    (`id`, `teleporter_entry`, `label_cs`, `label_en`, `x`, `y`, `z`, `o`, `sort_index`)
VALUES
    (1, 990203, 'Do vesnice', 'To the Village', 1063.29, 271.808, 336.837, 4.39102, 10),
    (2, 990203, 'Kovárna mistra oceli', 'Forge of the Steel Master', 1051.56, 28.2456, 315.229, 4.69428, 20),
    (3, 990203, 'Dílna mistra kožešin', 'Workshop of the Fur Master', 738.18, 170.549, 274.603, 5.29509, 30),
    (4, 990203, 'Zahrada mistra bylin', 'Garden of the Herbal Master', 917.491, 348.518, 270.731, 1.68697, 40),
    (5, 990203, 'Základna Expedic', 'Expedition Base', 113.003, 990.629, 297.25, 0.446081, 50),
    (6, 990203, 'Thranok the Unyielding (Boss)', 'Thranok the Unyielding (Boss)', 399.661, -154.433, 266.946, 4.66128, 60),
    (7, 990203, 'Thalor the Lifebinder (Boss)', 'Thalor the Lifebinder (Boss)', 180.379, 2.1369, 238.02, 5.39172, 70),
    (8, 990203, 'Voltrix the Unbound (Boss)', 'Voltrix the Unbound (Boss)', -47.1828, 293.399, 290.626, 2.509, 80),
    (9, 990203, 'Thalgron the Earthshaker (Boss)', 'Thalgron the Earthshaker (Boss)', 164.313, 584.39, 271.354, 2.82002, 90)
ON DUPLICATE KEY UPDATE
    `teleporter_entry` = VALUES(`teleporter_entry`),
    `label_cs`         = VALUES(`label_cs`),
    `label_en`         = VALUES(`label_en`),
    `x`                = VALUES(`x`),
    `y`                = VALUES(`y`),
    `z`                = VALUES(`z`),
    `o`                = VALUES(`o`),
    `sort_index`       = VALUES(`sort_index`);

-- -------------------------------------------------
-- customs.gv_expedition_active
-- Aktuálně běžící expedice gildy
-- -------------------------------------------------

CREATE TABLE IF NOT EXISTS `customs`.`gv_expedition_active` (
    `id`              BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `guildId`         INT UNSIGNED    NOT NULL,
    `mission_name`    VARCHAR(100)    NOT NULL,
    `heroes_sent`     TINYINT UNSIGNED NOT NULL DEFAULT '0',
    `success_chance`  TINYINT UNSIGNED NOT NULL DEFAULT '100',
    `end_time`        DATETIME        NOT NULL,
    `resolved`        TINYINT UNSIGNED NOT NULL DEFAULT '0',
    `resolved_at`     DATETIME        DEFAULT NULL,
    `created_at`      TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    KEY `idx_guild`                 (`guildId`),
    KEY `idx_endtime`               (`end_time`),
    KEY `idx_resolved`              (`resolved`),
    KEY `idx_gv_active_resolved_at` (`resolved`,`resolved_at`)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_0900_ai_ci;


-- -------------------------------------------------
-- customs.gv_expedition_catalog
-- Seznam dostupných hrdinů / žoldáků na nákup
-- -------------------------------------------------

CREATE TABLE IF NOT EXISTS `customs`.`gv_expedition_catalog` (
    `slot`           TINYINT  UNSIGNED NOT NULL,
    `label_cs`       VARCHAR(64)       NOT NULL DEFAULT 'Hrdina',
    `label_en`       VARCHAR(64)       NOT NULL DEFAULT 'Hero',
    `cost_mat1`      INT UNSIGNED      NOT NULL DEFAULT '0',
    `cost_mat2`      INT UNSIGNED      NOT NULL DEFAULT '0',
    `cost_mat3`      INT UNSIGNED      NOT NULL DEFAULT '0',
    `cost_mat4`      INT UNSIGNED      NOT NULL DEFAULT '0',
    `cost_gold`      INT UNSIGNED      NOT NULL DEFAULT '0',
    `successChance`  TINYINT  UNSIGNED NOT NULL DEFAULT '100',
    PRIMARY KEY (`slot`)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_0900_ai_ci;

INSERT INTO `customs`.`gv_expedition_catalog`
    (`slot`, `label_cs`, `label_en`,
     `cost_mat1`, `cost_mat2`, `cost_mat3`, `cost_mat4`, `cost_gold`,
     `successChance`)
VALUES
    (1,  'Arendil',   'Arendil',   25,  25,  50,  50,   250,   50),
    (2,  'Morvyn',    'Morvyn',    25,  25,  50,  50,   250,   60),
    (3,  'Kaelthara', 'Kaelthara', 25,  25,  50,  50,   250,   70),
    (4,  'Dravon',    'Dravon',    25,  25,  50,  50,   250,   80),
    (5,  'Lysara',    'Lysara',    50,  50,  75,  75,   500,   90),
    (6,  'Vorthan',   'Vorthan',   75,  75, 100, 100,   750,   35),
    (7,  'Elurien',   'Elurien',   75,  75, 100, 100,   750,   45),
    (8,  'Sarthor',   'Sarthor',   75,  75, 100, 100,   750,   55),
    (9,  'Myrdel',    'Myrdel',    75,  75, 100, 100,   750,   75),
    (10, 'Thaliora',  'Thaliora', 100, 100, 125, 125,  1000,   85),
    (11, 'Radanor',   'Radanor',  125, 125, 150, 150,  1250,    5),
    (12, 'Velthar',   'Velthar',  125, 125, 150, 150,  1250,    5),
    (13, 'Serina',    'Serina',   125, 125, 150, 150,  1250,    5),
    (14, 'Korveth',   'Korveth',  125, 125, 150, 150,  1250,    5),
    (15, 'Malenra',   'Malenra',  125, 125, 150, 150,  1250,   15),
    (16, 'Dareth',    'Dareth',   125, 125, 150, 150,  1250,   15),
    (17, 'Isyra',     'Isyra',    125, 125, 150, 150,  1250,   15),
    (18, 'Thorgar',   'Thorgar',  125, 125, 150, 150,  1250,   15),
    (19, 'Nerya',     'Nerya',    125, 125, 150, 150,  1250,   15),
    (20, 'Keldros',   'Keldros',  125, 125, 150, 150,  1250,   50),
    (21, 'Althira',   'Althira',  125, 125, 150, 150,  1250,   50),
    (22, 'Fenrath',   'Fenrath',  125, 125, 150, 150,  1250,   55),
    (23, 'Sylven',    'Sylven',   175, 175, 200, 200,  1500,   60),
    (24, 'Garanir',   'Garanir',  225, 225, 250, 250,  2000,   65),
    (25, 'Veyra',     'Veyra',    275, 275, 300, 300,  2500,   70)
ON DUPLICATE KEY UPDATE
    `label_cs`      = VALUES(`label_cs`),
    `label_en`      = VALUES(`label_en`),
    `cost_mat1`     = VALUES(`cost_mat1`),
    `cost_mat2`     = VALUES(`cost_mat2`),
    `cost_mat3`     = VALUES(`cost_mat3`),
    `cost_mat4`     = VALUES(`cost_mat4`),
    `cost_gold`     = VALUES(`cost_gold`),
    `successChance` = VALUES(`successChance`);


-- -------------------------------------------------
-- customs.gv_expedition_gear_catalog
-- Náklady na vylepšení výbavy expedice (tier -> iLvl)
-- -------------------------------------------------

CREATE TABLE IF NOT EXISTS `customs`.`gv_expedition_gear_catalog` (
    `tier_ilvl`   SMALLINT UNSIGNED NOT NULL,
    `label_cs`    VARCHAR(64)       NOT NULL DEFAULT 'Výbava iLvl',
    `label_en`    VARCHAR(64)       NOT NULL DEFAULT 'Gear iLvl',
    `cost_mat1`   INT UNSIGNED      NOT NULL DEFAULT '0',
    `cost_mat2`   INT UNSIGNED      NOT NULL DEFAULT '0',
    `cost_mat3`   INT UNSIGNED      NOT NULL DEFAULT '0',
    `cost_mat4`   INT UNSIGNED      NOT NULL DEFAULT '0',
    `cost_gold`   INT UNSIGNED      NOT NULL DEFAULT '0',
    `enabled`     TINYINT(1)        NOT NULL DEFAULT '1',
    PRIMARY KEY (`tier_ilvl`)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_0900_ai_ci;

INSERT INTO `customs`.`gv_expedition_gear_catalog`
    (`tier_ilvl`, `label_cs`, `label_en`,
     `cost_mat1`, `cost_mat2`, `cost_mat3`, `cost_mat4`, `cost_gold`,
     `enabled`)
VALUES
    (200, 'Vybavení družiny iLvl 200', 'Party gear iLvl 200', 100,  75,  50,  50,    50,    1),
    (213, 'Vybavení družiny iLvl 213', 'Party gear iLvl 213', 150, 120,  75,  75,    75,    1),
    (219, 'Vybavení družiny iLvl 219', 'Party gear iLvl 219', 200, 150, 115, 115,   150,    1),
    (226, 'Vybavení družiny iLvl 226', 'Party gear iLvl 226', 250, 190, 185, 185,   300,    1),
    (232, 'Vybavení družiny iLvl 232', 'Party gear iLvl 232', 300, 230, 250, 250,   600,    1),
    (245, 'Vybavení družiny iLvl 245', 'Party gear iLvl 245', 350, 270, 310, 310,  1200,    1),
    (251, 'Vybavení družiny iLvl 251', 'Party gear iLvl 251', 400, 310, 400, 400,  2400,    1),
    (258, 'Vybavení družiny iLvl 258', 'Party gear iLvl 258', 450, 350, 580, 580,  4800,    1),
    (264, 'Vybavení družiny iLvl 264', 'Party gear iLvl 264', 500, 390, 760, 760,  9600,    1),
    (271, 'Vybavení družiny iLvl 271', 'Party gear iLvl 271', 550, 420,1000,1000, 17200,    1)
ON DUPLICATE KEY UPDATE
    `label_cs`   = VALUES(`label_cs`),
    `label_en`   = VALUES(`label_en`),
    `cost_mat1`  = VALUES(`cost_mat1`),
    `cost_mat2`  = VALUES(`cost_mat2`),
    `cost_mat3`  = VALUES(`cost_mat3`),
    `cost_mat4`  = VALUES(`cost_mat4`),
    `cost_gold`  = VALUES(`cost_gold`),
    `enabled`    = VALUES(`enabled`);


-- -------------------------------------------------
-- customs.gv_expedition_guild
-- Stav expedicní gildy (limit hrdinů, gear tier atd.)
-- -------------------------------------------------

CREATE TABLE IF NOT EXISTS `customs`.`gv_expedition_guild` (
    `guildId`           INT UNSIGNED     NOT NULL,
    `heroes_owned`      TINYINT UNSIGNED NOT NULL DEFAULT '0',
    `heroes_on_mission` TINYINT UNSIGNED NOT NULL DEFAULT '0',
    `heroes_max`        TINYINT UNSIGNED NOT NULL DEFAULT '25',
    `gear_level`        SMALLINT UNSIGNED NOT NULL DEFAULT '0',
    `last_update`       TIMESTAMP        NOT NULL
                         DEFAULT CURRENT_TIMESTAMP
                         ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`guildId`)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_0900_ai_ci;


-- -------------------------------------------------
-- customs.gv_expedition_loot
-- "Guild loot bank" pro expedice (stack by item)
-- -------------------------------------------------

CREATE TABLE IF NOT EXISTS `customs`.`gv_expedition_loot` (
    `id`        BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `guildId`   INT    UNSIGNED NOT NULL,
    `itemid`    INT    UNSIGNED NOT NULL,
    `amount`    INT    UNSIGNED NOT NULL DEFAULT '1',
    `created_at` TIMESTAMP      NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uniq_stack`              (`guildId`,`itemid`),
    UNIQUE KEY `uq_gv_loot_guild_item`   (`guildId`,`itemid`),
    KEY        `idx_guild`               (`guildId`)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_0900_ai_ci;


-- -------------------------------------------------
-- customs.gv_expedition_mission_cost
-- Cena spuštění mise (materiály / gold)
-- -------------------------------------------------

CREATE TABLE IF NOT EXISTS `customs`.`gv_expedition_mission_cost` (
    `mission_name` VARCHAR(100) NOT NULL,
    `cost_mat1`    INT UNSIGNED NOT NULL DEFAULT '0',
    `cost_mat2`    INT UNSIGNED NOT NULL DEFAULT '0',
    `cost_mat3`    INT UNSIGNED NOT NULL DEFAULT '0',
    `cost_mat4`    INT UNSIGNED NOT NULL DEFAULT '0',
    `cost_gold`    INT UNSIGNED NOT NULL DEFAULT '0',
    `last_update`  TIMESTAMP    NOT NULL
                     DEFAULT CURRENT_TIMESTAMP
                     ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`mission_name`)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_0900_ai_ci;

INSERT INTO `customs`.`gv_expedition_mission_cost`
    (`mission_name`, `cost_mat1`, `cost_mat2`, `cost_mat3`, `cost_mat4`,
     `cost_gold`, `last_update`)
VALUES
    ('AhnKahet',                         5,   5,   5,   5,    25, '2025-11-02 11:55:00'),
    ('AhnKahet (HC)',                   15,  15,  15,  15,    50, '2025-11-02 11:54:59'),
    ('Azjol Nerub',                      5,   5,   5,   5,    25, '2025-11-02 11:54:57'),
    ('Azjol Nerub (HC)',                15,  15,  15,  15,    50, '2025-11-02 11:54:56'),
    ('Culling of Stratholme',            5,   5,   5,   5,    25, '2025-11-02 11:54:55'),
    ('Culling of Stratholme (HC)',      15,  15,  15,  15,    50, '2025-11-02 11:54:54'),
    ('DrakTharon Keep',                  5,   5,   5,   5,    25, '2025-11-02 11:54:52'),
    ('DrakTharon Keep (HC)',            15,  15,  15,  15,    50, '2025-11-02 11:54:50'),
    ('Forge of Souls',                  75,  75,  75,  75,   750, '2025-11-02 11:51:47'),
    ('Forge of Souls (HC)',            125, 125, 125, 125,  1500, '2025-11-02 11:50:28'),
    ('Gundrak',                           5,   5,   5,   5,    25, '2025-11-02 11:54:30'),
    ('Gundrak (HC)',                    15,  15,  15,  15,    50, '2025-11-02 11:54:42'),
    ('Halls of Lightning',               5,   5,   5,   5,    25, '2025-11-02 11:54:28'),
    ('Halls of Lightning (HC)',         15,  15,  15,  15,    50, '2025-11-02 11:54:24'),
    ('Halls of Reflection',             75,  75,  75,  75,   750, '2025-11-02 11:51:40'),
    ('Halls of Reflection (HC)',       125, 125, 125, 125,  1500, '2025-11-02 11:50:08'),
    ('Halls of Stone',                    5,   5,   5,   5,    25, '2025-11-02 11:54:17'),
    ('Halls of Stone (HC)',             15,  15,  15,  15,    50, '2025-11-02 11:54:22'),
    ('IceCrown Citadel (10)',          175, 175, 175, 175,  2000, '2025-11-02 11:48:57'),
    ('IceCrown Citadel (25)',          225, 225, 225, 225,  2500, '2025-11-02 11:48:17'),
    ('Naxxramas (10)',                   25,  25,  25,  25,   250, '2025-11-02 11:52:32'),
    ('Naxxramas (25)',                   50,  50,  50,  50,   500, '2025-11-02 11:52:27'),
    ('Pit of Saron',                     75,  75,  75,  75,   750, '2025-11-02 11:51:37'),
    ('Pit of Saron (HC)',               125, 125, 125, 125,  1500, '2025-11-02 11:49:59'),
    ('The Eye of Eternity (10)',         25,  25,  25,  25,   250, '2025-11-02 11:52:34'),
    ('The Eye of Eternity (25)',         50,  50,  50,  50,   500, '2025-11-02 11:52:19'),
    ('The Nexus',                         5,   5,   5,   5,    25, '2025-11-02 11:54:05'),
    ('The Nexus (HC)',                   15,  15,  15,  15,    50, '2025-11-02 11:54:11'),
    ('The Obsidian Sanctum (10)',        25,  25,  25,  25,   250, '2025-11-02 11:52:39'),
    ('The Obsidian Sanctum (25)',        50,  50,  50,  50,   500, '2025-11-02 11:52:16'),
    ('The Oculus',                        5,   5,   5,   5,    25, '2025-11-02 11:53:00'),
    ('The Oculus (HC)',                  15,  15,  15,  15,    50, '2025-11-02 11:53:02'),
    ('The Ruby Sanctum (10)',           200, 200, 200, 200,  2250, '2025-11-02 11:48:41'),
    ('The Ruby Sanctum (25)',           250, 250, 250, 250,  2750, '2025-11-02 11:48:09'),
    ('Trial of the Champion',            15,  15,  15,  15,    50, '2025-11-02 11:53:43'),
    ('Trial of the Champion (HC)',       75,  75,  75,  75,   750, '2025-11-02 11:51:08'),
    ('Trial of the Crusader (10)',      125, 125, 125, 125,  1500, '2025-11-02 11:49:51'),
    ('Trial of the Crusader (25)',      150, 150, 150, 150,  1750, '2025-11-02 11:49:19'),
    ('Ulduar (10)',                       75,  75,  75,  75,   750, '2025-11-02 11:51:14'),
    ('Ulduar (25)',                      100, 100, 100, 100,  1250, '2025-11-02 11:50:45'),
    ('Utgarde Keep',                       5,   5,   5,   5,    25, '2025-11-02 11:53:54'),
    ('Utgarde Keep (HC)',                 15,  15,  15,  15,    50, '2025-11-02 11:53:45'),
    ('Utgarde Pinnacle',                   5,   5,   5,   5,    25, '2025-11-02 11:53:53'),
    ('Utgarde Pinnacle (HC)',             15,  15,  15,  15,    50, '2025-11-02 11:53:48'),
    ('Violet Hold',                        5,   5,   5,   5,    25, '2025-11-02 11:53:51'),
    ('Violet Hold (HC)',                  15,  15,  15,  15,    50, '2025-11-02 11:53:49')
ON DUPLICATE KEY UPDATE
    `cost_mat1`   = VALUES(`cost_mat1`),
    `cost_mat2`   = VALUES(`cost_mat2`),
    `cost_mat3`   = VALUES(`cost_mat3`),
    `cost_mat4`   = VALUES(`cost_mat4`),
    `cost_gold`   = VALUES(`cost_gold`),
    `last_update` = VALUES(`last_update`);


-- -------------------------------------------------
-- customs.gv_expedition_requirements
-- Požadavky mise (min ilvl družiny, min počet hrdinů)
-- -------------------------------------------------

CREATE TABLE IF NOT EXISTS `customs`.`gv_expedition_requirements` (
    `mission_name` VARCHAR(128) NOT NULL,
    `ilvl`         INT UNSIGNED NOT NULL DEFAULT '0',
    `heroes`       INT UNSIGNED NOT NULL DEFAULT '0',
    PRIMARY KEY (`mission_name`)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_0900_ai_ci;

INSERT INTO `customs`.`gv_expedition_requirements`
    (`mission_name`, `ilvl`, `heroes`)
VALUES
    ('AhnKahet',                         182,  1),
    ('AhnKahet (HC)',                    200,  1),
    ('Azjol Nerub',                      182,  1),
    ('Azjol Nerub (HC)',                 200,  1),
    ('Culling of Stratholme',            182,  1),
    ('Culling of Stratholme (HC)',       200,  1),
    ('DrakTharon Keep',                  182,  1),
    ('DrakTharon Keep (HC)',             200,  1),
    ('Forge of Souls',                   219,  5),
    ('Forge of Souls (HC)',              232,  5),
    ('Gundrak',                          182,  1),
    ('Gundrak (HC)',                     200,  1),
    ('Halls of Lightning',               182,  1),
    ('Halls of Lightning (HC)',          200,  1),
    ('Halls of Reflection',              219,  5),
    ('Halls of Reflection (HC)',         232,  5),
    ('Halls of Stone',                   182,  1),
    ('Halls of Stone (HC)',              200,  1),
    ('IceCrown Citadel (10)',            251,  6),
    ('IceCrown Citadel (25)',            264, 11),
    ('Naxxramas (10)',                   200,  6),
    ('Naxxramas (25)',                   213, 11),
    ('Pit of Saron',                     219,  5),
    ('Pit of Saron (HC)',                232,  5),
    ('The Eye of Eternity (10)',         200,  6),
    ('The Eye of Eternity (25)',         213, 11),
    ('The Nexus',                        182,  1),
    ('The Nexus (HC)',                   200,  1),
    ('The Obsidian Sanctum (10)',        200,  6),
    ('The Obsidian Sanctum (25)',        213, 11),
    ('The Oculus',                       182,  1),
    ('The Oculus (HC)',                  200,  1),
    ('The Ruby Sanctum (10)',            258,  6),
    ('The Ruby Sanctum (25)',            271, 11),
    ('Trial of the Champion',            200,  1),
    ('Trial of the Champion (HC)',       219,  5),
    ('Trial of the Crusader (10)',       232,  6),
    ('Trial of the Crusader (25)',       245, 11),
    ('Ulduar (10)',                      219,  6),
    ('Ulduar (25)',                      226, 11),
    ('Utgarde Keep',                     182,  1),
    ('Utgarde Keep (HC)',                200,  1),
    ('Utgarde Pinnacle',                 182,  1),
    ('Utgarde Pinnacle (HC)',            200,  1),
    ('Violet Hold',                      182,  1),
    ('Violet Hold (HC)',                 200,  1)
ON DUPLICATE KEY UPDATE
    `ilvl`   = VALUES(`ilvl`),
    `heroes` = VALUES(`heroes`);


-- -------------------------------------------------
-- customs.gv_expedition_member_watch
-- Watchlist členů gild (kdy vstoupili do gildy)
-- -------------------------------------------------

CREATE TABLE IF NOT EXISTS `customs`.`gv_expedition_member_watch` (
    `guildId`    INT UNSIGNED NOT NULL,
    `playerGuid` INT UNSIGNED NOT NULL,
    `join_time`  INT UNSIGNED NOT NULL,
    PRIMARY KEY (`guildId`, `playerGuid`),
    KEY `idx_playerGuid` (`playerGuid`)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_0900_ai_ci;

-- Seed join_time pro stávající členy gild
-- (Všichni dostanou fixní "předpokládaný vstup" = 2025-10-01)
SET @SEED_JOIN := UNIX_TIMESTAMP('2025-10-01 00:00:00');

INSERT INTO `customs`.`gv_expedition_member_watch`
    (`guildId`, `playerGuid`, `join_time`)
SELECT
    gm.guildid  AS `guildId`,
    gm.guid     AS `playerGuid`,
    @SEED_JOIN  AS `join_time`
FROM `acore_characters`.`guild_member` gm
ON DUPLICATE KEY UPDATE
    `join_time` = VALUES(`join_time`);


-- =========================================================
-- Done.
-- =========================================================
