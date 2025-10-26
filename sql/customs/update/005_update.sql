-- ===================================
--  GUILD VILLAGE: GV-Fabricator 01
--  Inserts for acore_world + customs
-- ===================================

-- -----------------------------------
--  CUSTOMS
-- -----------------------------------
USE `customs`;

INSERT INTO `gv_upgrade_poi` (
    `id`, `expansion_key`, `faction`, `map`, `pos_x`, `pos_y`, `icon`, `flags`, `poi_id`, `name_cs`, `name_en`
) VALUES
(48, 'material_production', 0, 37, 1066, 279.232, 7, 0, 1247, 'Výrobní Jednotka', 'Production Unit');

INSERT INTO `gv_expansion_creatures` (
    `id`, `expansion_key`, `entry`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
    `spawntimesecs`, `spawndist`, `movementtype`, `faction`
) VALUES
(283, 'material_production', 987448, 37, 1066, 279.232, 336.87, 4.41546, 300, 0, 0, 0);

INSERT INTO `gv_upgrade_catalog` (
    `id`, `category`, `expansion_key`, `label_cs`, `label_en`,
    `cost_material1`, `cost_material2`, `cost_material3`, `cost_material4`, `sort_order`
) VALUES
(63, 'others', 'material_production', 'Výrobní Jednotka', 'Production Unit', 200, 200, 200, 200, 1);
