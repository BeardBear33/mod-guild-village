-- ================================================
-- Update struktury tabulky gv_upgrade_catalog
-- Ze starých názvů cost_timber/stone/iron/crystal
-- na nové cost_material1/2/3/4
-- ================================================

USE `customs`;


ALTER TABLE `gv_upgrade_catalog`
    ADD COLUMN `cost_material1` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `label_en`,
    ADD COLUMN `cost_material2` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `cost_material1`,
    ADD COLUMN `cost_material3` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `cost_material2`,
    ADD COLUMN `cost_material4` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `cost_material3`;


UPDATE `gv_upgrade_catalog`
SET
    cost_material1 = cost_timber,
    cost_material2 = cost_stone,
    cost_material3 = cost_iron,
    cost_material4 = cost_crystal;


ALTER TABLE `gv_upgrade_catalog`
    DROP COLUMN `cost_timber`,
    DROP COLUMN `cost_stone`,
    DROP COLUMN `cost_iron`,
    DROP COLUMN `cost_crystal`;

