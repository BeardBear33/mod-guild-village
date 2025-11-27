-- Exportování struktury pro tabulku customs.gv_expedition_gear_catalog
DROP TABLE IF EXISTS `customs`.`gv_expedition_gear_catalog`;
CREATE TABLE IF NOT EXISTS `customs`.`gv_expedition_gear_catalog` (
  `tier_ilvl` smallint unsigned NOT NULL,
  `label_cs` varchar(64) NOT NULL DEFAULT 'Výbava iLvl',
  `label_en` varchar(64) NOT NULL DEFAULT 'Gear iLvl',
  `cost_mat1` int unsigned NOT NULL DEFAULT '0',
  `cost_mat2` int unsigned NOT NULL DEFAULT '0',
  `cost_mat3` int unsigned NOT NULL DEFAULT '0',
  `cost_mat4` int unsigned NOT NULL DEFAULT '0',
  `cost_gold` int unsigned NOT NULL DEFAULT '0',
  `enabled` tinyint(1) NOT NULL DEFAULT '1',
  PRIMARY KEY (`tier_ilvl`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

-- Exportování dat pro tabulku customs.gv_expedition_gear_catalog
INSERT INTO `customs`.`gv_expedition_gear_catalog` (`tier_ilvl`, `label_cs`, `label_en`, `cost_mat1`, `cost_mat2`, `cost_mat3`, `cost_mat4`, `cost_gold`, `enabled`) VALUES
	(200, 'Vybavení družiny iLvl 200', 'Party gear iLvl 200', 100, 75, 50, 50, 50, 1),
	(213, 'Vybavení družiny iLvl 213', 'Party gear iLvl 213', 150, 120, 75, 75, 75, 1),
	(219, 'Vybavení družiny iLvl 219', 'Party gear iLvl 219', 200, 150, 115, 115, 150, 1),
	(226, 'Vybavení družiny iLvl 226', 'Party gear iLvl 226', 250, 190, 185, 185, 300, 1),
	(232, 'Vybavení družiny iLvl 232', 'Party gear iLvl 232', 300, 230, 250, 250, 600, 1),
	(245, 'Vybavení družiny iLvl 245', 'Party gear iLvl 245', 350, 270, 310, 310, 1200, 1),
	(251, 'Vybavení družiny iLvl 251', 'Party gear iLvl 251', 400, 310, 400, 400, 2400, 1),
	(258, 'Vybavení družiny iLvl 258', 'Party gear iLvl 258', 450, 350, 580, 580, 4800, 1),
	(264, 'Vybavení družiny iLvl 264', 'Party gear iLvl 264', 500, 390, 760, 760, 9600, 1),
	(271, 'Vybavení družiny iLvl 271', 'Party gear iLvl 271', 550, 420, 1000, 1000, 17200, 1);