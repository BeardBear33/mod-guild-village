-- ================================================
-- Guild Village: BG / Arena expansion
-- ================================================
USE `customs`;

-- ------------------------------------------------
-- gv_expansion_creatures
-- ------------------------------------------------
DELETE FROM `gv_expansion_creatures`
WHERE `id` BETWEEN 294 AND 299;

INSERT INTO `gv_expansion_creatures` (
    `id`,
    `expansion_key`,
    `entry`,
    `map`,
    `position_x`,
    `position_y`,
    `position_z`,
    `orientation`,
    `spawntimesecs`,
    `spawndist`,
    `movementtype`,
    `faction`
) VALUES
    (294, 'bg_arena', 34095, 37, 1003.060, 344.981, 370.537, 3.20827, 300, 0, 0, 0),
    (295, 'bg_arena', 33938, 37, 1014.000, 334.359, 346.405, 2.25794, 300, 0, 0, 0),
    (296, 'bg_arena', 33936, 37, 1012.110, 356.803, 346.405, 4.22143, 300, 0, 0, 0),
    (297, 'bg_arena', 33937, 37, 1012.900, 345.334, 346.412, 0.07454, 300, 0, 0, 0),
    (298, 'bg_arena', 29534, 37, 1001.820, 348.264, 333.013, 4.84190, 300, 0, 0, 0),
    (299, 'bg_arena', 29533, 37,  995.952, 344.615, 333.013, 6.26740, 300, 0, 0, 0);

-- ------------------------------------------------
-- gv_upgrade_catalog
-- ------------------------------------------------
DELETE FROM `gv_upgrade_catalog`
WHERE `id` = 64;

INSERT INTO `gv_upgrade_catalog` (
    `id`,
    `category`,
    `expansion_key`,
    `label_cs`,
    `label_en`,
    `cost_material1`,
    `cost_material2`,
    `cost_material3`,
    `cost_material4`,
    `sort_order`
) VALUES
    (64, 'others', 'bg_arena', 'PvP', 'PvP', 0, 0, 18, 24, 1);

-- ------------------------------------------------
-- gv_upgrade_poi
-- ------------------------------------------------
DELETE FROM `gv_upgrade_poi`
WHERE `id` = 49;

INSERT INTO `gv_upgrade_poi` (
    `id`,
    `expansion_key`,
    `faction`,
    `map`,
    `pos_x`,
    `pos_y`,
    `icon`,
    `flags`,
    `poi_id`,
    `name_cs`,
    `name_en`
) VALUES
    (49, 'bg_arena', 0, 37, 997.877, 344.597, 7, 0, 1248, 'PvP', 'PvP');
